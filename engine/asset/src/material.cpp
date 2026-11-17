#include "noor/asset/material.h"

#include "noor/asset/texture.h"
#include "noor/foundation/file_io.h"

#include <algorithm>
#include <format>
#include <set>

namespace noor::asset {
namespace stdfs = std::filesystem;

namespace {

bool read_floats(const Json& j, std::size_t n, float* out) {
    if (!j.is_array() || j.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!j[i].is_number()) return false;
        out[i] = j[i].get<float>();
    }
    return true;
}

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Words of a file stem split on _ - . and space, with where each starts.
std::vector<std::pair<std::string, std::size_t>> words_of(const std::string& stem) {
    std::vector<std::pair<std::string, std::size_t>> out;
    std::string word;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= stem.size(); ++i) {
        const char c = i < stem.size() ? stem[i] : '_';
        if (c == '_' || c == '-' || c == '.' || c == ' ') {
            if (!word.empty()) out.emplace_back(lower(word), start);
            word.clear();
            start = i + 1;
        } else {
            word += c;
        }
    }
    return out;
}

TextureRole role_of_word(const std::string& w, const std::string& next) {
    if (w == "base" && (next == "color" || next == "colour")) return TextureRole::BaseColor;
    if (w == "albedo" || w == "basecolor" || w == "basecolour" || w == "diffuse" || w == "diff" || w == "color" || w == "col")
        return TextureRole::BaseColor;
    if (w == "normal" || w == "normals" || w == "nrm" || w == "nor" || w == "norm" || w == "normalgl" || w == "normaldx")
        return TextureRole::Normal;
    if (w == "orm" || w == "arm") return TextureRole::Orm;
    if (w == "metallicroughness" || w == "metalroughness") return TextureRole::MetallicRoughness;
    if (w == "roughness" || w == "rough") return TextureRole::Roughness;
    if (w == "metallic" || w == "metalness" || w == "metal") return TextureRole::Metallic;
    if (w == "ao" || w == "occlusion" || w == "ambientocclusion") return TextureRole::Occlusion;
    if (w == "emissive" || w == "emission" || w == "emit") return TextureRole::Emissive;
    if (w == "height" || w == "displacement" || w == "disp") return TextureRole::Height;
    return TextureRole::None;
}

// The role and where the set name ends in the stem; previews and thumbnails have no role.
// The **last** role word decides: set names can contain role words themselves
// ("wooden_rough_planks_diff_1k", "blue_metal_plate_arm_1k"), the role comes after them.
std::pair<TextureRole, std::size_t> role_and_cut(const stdfs::path& file) {
    const std::string stem = file.stem().string();
    const auto words = words_of(stem);
    for (const auto& [w, start] : words)
        if (w == "preview" || w == "thumb" || w == "thumbnail") return {TextureRole::None, 0};
    for (std::size_t i = words.size(); i-- > 1;) {  // a set name must come first: never word 0
        const auto role = role_of_word(words[i].first, i + 1 < words.size() ? words[i + 1].first : "");
        if (role == TextureRole::None) continue;
        // "base color": the role starts at "base".
        if (role == TextureRole::BaseColor && (words[i].first == "color" || words[i].first == "colour") && i > 1 &&
            words[i - 1].first == "base")
            return {role, words[i - 1].second};
        return {role, words[i].second};
    }
    return {TextureRole::None, 0};
}

std::string set_name_of(const stdfs::path& file, std::size_t cut) {
    std::string name = file.stem().string().substr(0, cut);
    while (!name.empty() && (name.back() == '_' || name.back() == '-' || name.back() == '.' || name.back() == ' ')) name.pop_back();
    return name;
}

} // namespace

std::string_view alpha_name(AlphaMode mode) noexcept {
    return mode == AlphaMode::Cutout ? "cutout" : mode == AlphaMode::Blend ? "blend" : "opaque";
}

void apply_material_fields(const Json& j, Material& m, const AssetFileResolver& resolve, std::vector<std::string>* warnings) {
    if (!j.is_object()) return;
    float v[4];
    if (j.contains("base_color")) {
        if (read_floats(j["base_color"], 4, v)) m.base_color = {v[0], v[1], v[2], v[3]};
        else if (read_floats(j["base_color"], 3, v)) m.base_color = {v[0], v[1], v[2], m.base_color.w};
    }
    if (j.contains("emissive") && read_floats(j["emissive"], 3, v)) m.emissive = {v[0], v[1], v[2]};
    if (j.contains("uv_scale") && read_floats(j["uv_scale"], 2, v)) m.uv_scale = {v[0], v[1]};
    if (j.contains("uv_offset") && read_floats(j["uv_offset"], 2, v)) m.uv_offset = {v[0], v[1]};
    const auto number = [&](const char* key, float& out, float lo, float hi) {
        if (j.contains(key) && j[key].is_number()) out = std::clamp(j[key].get<float>(), lo, hi);
    };
    number("metallic", m.metallic, 0, 1);
    number("roughness", m.roughness, 0, 1);
    number("normal_scale", m.normal_scale, -10, 10);
    number("occlusion_strength", m.occlusion_strength, 0, 1);
    number("emissive_strength", m.emissive_strength, 0, 1e6f);
    number("alpha_cutoff", m.alpha_cutoff, 0, 1);
    if (j.contains("double_sided") && j["double_sided"].is_boolean()) m.double_sided = j["double_sided"].get<bool>();
    if (j.contains("alpha") && j["alpha"].is_string()) {
        const std::string a = j["alpha"].get<std::string>();
        m.alpha = a == "cutout" ? AlphaMode::Cutout : a == "blend" ? AlphaMode::Blend : AlphaMode::Opaque;
    }
    const std::pair<const char*, TextureRef*> maps[] = {
        {"base_color_map", &m.base_color_map}, {"metallic_roughness_map", &m.metallic_roughness_map},
        {"roughness_map", &m.roughness_map},   {"metallic_map", &m.metallic_map},
        {"normal_map", &m.normal_map},         {"occlusion_map", &m.occlusion_map},
        {"emissive_map", &m.emissive_map},
    };
    for (const auto& [key, ref] : maps) {
        if (!j.contains(key)) continue;
        if (j[key].is_null()) {
            *ref = {};  // an instance can remove its parent's map
            continue;
        }
        const auto id = guid_field(j, key);
        auto file = id && resolve ? resolve(*id) : std::nullopt;
        if (!file) {
            if (warnings) warnings->push_back(std::format("{}: texture {} not found", key, j[key].dump()));
            *ref = {};
            continue;
        }
        *ref = {std::move(*file), -1};
    }
}

Result<Material> load_material(const stdfs::path& file, const AssetFileResolver& resolve, std::vector<std::string>* warnings) {
    // Collect the chain from this file up to the root material, then apply root first.
    std::vector<Json> chain;
    std::set<std::string> seen;
    stdfs::path current = file;
    for (int depth = 0;; ++depth) {
        if (depth >= 16) return fail(ErrorCode::InvalidArgument, std::format("'{}': material parents are more than 16 deep", file.generic_string()));
        const std::string key = stdfs::weakly_canonical(current).generic_string();
        if (!seen.insert(key).second)
            return fail(ErrorCode::InvalidArgument, std::format("'{}': material parents form a loop (at {})", file.generic_string(), current.filename().string()));
        auto text = fs::read_file(current, 4 << 20);
        if (!text) return std::unexpected(text.error());
        auto json = parse_json(*text, current.generic_string());
        if (!json) return std::unexpected(json.error());
        if (!json->is_object() || json->value("noor_material", 0) <= 0)
            return fail(ErrorCode::ParseError, std::format("'{}' is not a .noormat material", current.generic_string()));
        if (json->value("noor_material", 0) > kMaterialFormat)
            return fail(ErrorCode::VersionMismatch, std::format("'{}' needs a newer NoorEngine", current.generic_string()));
        const bool instance = json->contains("parent") && !(*json)["parent"].is_null();
        chain.push_back(instance ? json->value("overrides", Json::object()) : *json);
        if (!instance) break;
        const auto parent = guid_field(*json, "parent");
        auto parent_file = parent && resolve ? resolve(*parent) : std::nullopt;
        if (!parent_file)
            return fail(ErrorCode::NotFound, std::format("'{}': parent material {} not found", current.generic_string(), (*json)["parent"].dump()));
        current = *parent_file;
    }
    Material m;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) apply_material_fields(*it, m, resolve, warnings);
    return m;
}

// ---- texture sets ----

std::string_view role_name(TextureRole role) noexcept {
    switch (role) {
    case TextureRole::BaseColor: return "base colour";
    case TextureRole::Normal: return "normal";
    case TextureRole::Roughness: return "roughness";
    case TextureRole::Metallic: return "metallic";
    case TextureRole::Occlusion: return "occlusion";
    case TextureRole::Orm: return "ORM";
    case TextureRole::MetallicRoughness: return "metallic-roughness";
    case TextureRole::Emissive: return "emissive";
    case TextureRole::Height: return "height";
    case TextureRole::None: break;
    }
    return "none";
}

TextureRole texture_role(const stdfs::path& file) { return role_and_cut(file).first; }

std::optional<stdfs::path> TextureSet::find(TextureRole role) const {
    for (const auto& [r, p] : maps)
        if (r == role) return p;
    return std::nullopt;
}

TextureSet find_texture_set(const stdfs::path& any_map) {
    TextureSet set;
    const auto [role, cut] = role_and_cut(any_map);
    set.name = role == TextureRole::None ? any_map.stem().string() : set_name_of(any_map, cut);
    if (role == TextureRole::None) {
        set.maps.emplace_back(TextureRole::BaseColor, any_map);  // a lone picture: its colour
        return set;
    }
    std::error_code ec;
    std::vector<stdfs::path> files;
    for (const auto& e : stdfs::directory_iterator(any_map.parent_path(), ec))
        if (e.is_regular_file() && is_texture_file(e.path())) files.push_back(e.path());
    std::ranges::sort(files);
    for (const auto& f : files) {
        const auto [r, c] = role_and_cut(f);
        if (r == TextureRole::None || lower(set_name_of(f, c)) != lower(set.name)) continue;
        if (!set.find(r)) set.maps.emplace_back(r, f);  // first one per role
    }
    return set;
}

Json material_json_for(const TextureSet& set, const std::function<std::optional<Guid>(const stdfs::path&)>& id_of) {
    Json m{{"noor_material", kMaterialFormat}, {"base_color", {1, 1, 1, 1}}, {"metallic", 0.0}, {"roughness", 0.5}};
    const auto put = [&](const char* key, TextureRole role) {
        if (auto file = set.find(role))
            if (auto id = id_of(*file)) {
                m[key] = id->to_string();
                return true;
            }
        return false;
    };
    put("base_color_map", TextureRole::BaseColor);
    put("normal_map", TextureRole::Normal);
    put("emissive_map", TextureRole::Emissive) ? void(m["emissive"] = {1, 1, 1}) : void();
    if (put("metallic_roughness_map", TextureRole::Orm)) {
        put("occlusion_map", TextureRole::Orm);  // ORM: R occlusion, G roughness, B metallic
        m["metallic"] = 1.0;
        m["roughness"] = 1.0;  // the factors multiply the map
    } else {
        if (put("metallic_roughness_map", TextureRole::MetallicRoughness)) {
            m["metallic"] = 1.0;
            m["roughness"] = 1.0;
        }
        if (put("roughness_map", TextureRole::Roughness)) m["roughness"] = 1.0;
        if (put("metallic_map", TextureRole::Metallic)) m["metallic"] = 1.0;
        put("occlusion_map", TextureRole::Occlusion);
    }
    return m;
}

} // namespace noor::asset
