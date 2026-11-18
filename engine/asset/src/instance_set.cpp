#include "noor/asset/instance_set.h"

#include "noor/asset/json.h"
#include "noor/foundation/file_io.h"

#include <charconv>
#include <cmath>
#include <format>

namespace noor::asset {

std::string instance_id_text(std::uint64_t id) { return std::format("{:016x}", id); }

std::optional<std::uint64_t> parse_instance_id(std::string_view text) {
    if (text.empty() || text.size() > 16) return std::nullopt;
    std::uint64_t v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v, 16);
    if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return v;
}

std::string InstanceSet::to_text() const {
    Json j;
    j["noor_instances"] = kInstanceSetFormat;
    j["space"] = {{"up", "+Y"}, {"handedness", "right"}, {"units", "meters"}};
    Json a = Json::array();
    for (const auto& g : assets) a.push_back(g.to_string());
    j["assets"] = std::move(a);
    j["fields"] = {"asset", "id", "x", "y", "z", "qx", "qy", "qz", "qw", "scale"};
    // One line per instance (a forest of thousands stays readable and diffs well).
    std::string out = asset::to_text(j);
    out.pop_back();  // the closing "}\n"
    out.pop_back();
    out += ",\n  \"instances\": [";
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        const Json row{it.asset, instance_id_text(it.id), it.position.x, it.position.y, it.position.z,
                       it.rotation.x, it.rotation.y, it.rotation.z, it.rotation.w, it.scale};
        out += (i == 0 ? "\n    " : ",\n    ") + row.dump();
    }
    out += items.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

Result<InstanceSet> InstanceSet::from_text(std::string_view text, std::string_view source) {
    auto parsed = parse_json(text, source);
    if (!parsed) return std::unexpected(parsed.error());
    const Json& j = *parsed;
    if (!j.is_object() || !j.contains("noor_instances"))
        return fail(ErrorCode::ParseError, std::format("'{}' isn't an instance set (no \"noor_instances\")", source));
    if (j.value("noor_instances", 0) > kInstanceSetFormat)
        return fail(ErrorCode::VersionMismatch, std::format("'{}' needs a newer NoorEngine", source));
    InstanceSet set;
    for (const auto& a : j.value("assets", Json::array())) {
        auto g = a.is_string() ? Guid::parse(a.get<std::string>()) : Result<Guid>(fail(ErrorCode::ParseError, "not a string"));
        if (!g) return fail(ErrorCode::ParseError, std::format("'{}': an asset isn't a UUID", source));
        set.assets.push_back(*g);
    }
    const auto& rows = j.value("instances", Json::array());
    set.items.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Json& r = rows[i];
        const auto bad = [&](const char* why) { return fail(ErrorCode::ParseError, std::format("'{}': instance {} {}", source, i, why)); };
        if (!r.is_array() || r.size() != 10) return bad("isn't a row of 10 values");
        Item it;
        if (!r[0].is_number_unsigned() || r[0].get<std::uint64_t>() >= set.assets.size()) return bad("names an asset that isn't listed");
        it.asset = r[0].get<std::uint32_t>();
        const auto id = r[1].is_string() ? parse_instance_id(r[1].get<std::string>()) : std::nullopt;
        if (!id) return bad("has no valid id");
        it.id = *id;
        float v[8];
        for (int k = 0; k < 8; ++k) {
            if (!r[2 + k].is_number()) return bad("has a value that isn't a number");
            v[k] = r[2 + k].get<float>();
            if (!std::isfinite(v[k])) return bad("has a value that isn't finite");
        }
        it.position = {v[0], v[1], v[2]};
        it.rotation = {v[3], v[4], v[5], v[6]};
        it.scale = v[7];
        set.items.push_back(it);
    }
    return set;
}

Result<InstanceSet> InstanceSet::load(const std::filesystem::path& file) {
    auto text = fs::read_file(file, 512ull << 20);
    if (!text) return std::unexpected(text.error());
    return from_text(*text, file.generic_string());
}

} // namespace noor::asset
