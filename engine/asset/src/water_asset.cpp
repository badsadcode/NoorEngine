#include "noor/asset/water_asset.h"

#include "noor/foundation/file_io.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace noor::asset {
namespace {

// "#RRGGBB" (sRGB) as linear rgb; nullopt if it isn't one.
std::optional<math::Vec3> hex_linear(const Json& j) {
    if (!j.is_string()) return std::nullopt;
    const std::string hex = j.get<std::string>();
    if (hex.size() != 7 || hex[0] != '#') return std::nullopt;
    math::Vec3 out;
    float* c[3] = {&out.x, &out.y, &out.z};
    for (int i = 0; i < 3; ++i) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            const char ch = hex[static_cast<std::size_t>(1 + 2 * i + k)];
            const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d < 0) return std::nullopt;
            v = v * 16 + static_cast<unsigned>(d);
        }
        const float s = static_cast<float>(v) / 255.0f;
        *c[i] = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    }
    return out;
}

std::optional<math::Vec3> vec3(const Json& j) {
    if (!j.is_array() || j.size() != 3) return std::nullopt;
    for (const auto& v : j)
        if (!v.is_number()) return std::nullopt;
    return math::Vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

void number(const Json& s, const char* key, float& out, float lo, float hi) {
    if (s.contains(key) && s[key].is_number()) out = std::clamp(s[key].get<float>(), lo, hi);
}

} // namespace

math::Vec3 absorption_from_tint(math::Vec3 tint, float clarity) {
    const float c = std::max(clarity, 0.01f);
    const auto a = [&](float t) { return -std::log(std::clamp(t, 1e-4f, 1.0f)) / c; };
    return {a(tint.x), a(tint.y), a(tint.z)};
}

void WaterLook::apply(const Json& s) {
    if (!s.is_object()) return;
    if (s.contains("absorption"))
        if (const auto a = vec3(s["absorption"])) absorption = {std::max(a->x, 0.0f), std::max(a->y, 0.0f), std::max(a->z, 0.0f)};
    if (s.contains("tint"))
        if (const auto t = hex_linear(s["tint"])) {
            float clarity = 3.0f;
            number(s, "clarity", clarity, 0.01f, 1000.0f);
            absorption = absorption_from_tint(*t, clarity);
        }
    if (s.contains("scatter"))
        if (const auto c = hex_linear(s["scatter"])) scatter = *c;
    number(s, "refraction", refraction, 0.0f, 0.5f);
    number(s, "shore_fade", shore_fade, 0.001f, 10.0f);
    number(s, "roughness", roughness, 0.0f, 1.0f);
    number(s, "ripple", ripple, 0.0f, 10.0f);
    number(s, "ripple_scale", ripple_scale, 0.01f, 100.0f);
    number(s, "wave_height", wave_height, 0.0f, 30.0f);
    number(s, "wave_length", wave_length, 0.5f, 1000.0f);
    number(s, "wave_direction", wave_direction, -3600.0f, 3600.0f);
    number(s, "choppiness", choppiness, 0.0f, 0.9f);
    number(s, "wind", wind, 0.0f, 1.0f);
}

const Json& water_presets() {
    static const Json presets = Json::array({
        {{"name", "Lake"},
         {"description", "A calm, clear green lake: you see the bottom near the shore, deep green further out"},
         {"water", {{"tint", "#6FB7A8"}, {"clarity", 4.0}, {"scatter", "#1F4A48"}, {"refraction", 0.06}, {"shore_fade", 0.12},
                    {"roughness", 0.06}, {"ripple", 1.0}, {"ripple_scale", 1.2}, {"wave_height", 0.0}}}},
        {{"name", "Sea"},
         {"description", "Open sea: a long swell with smaller waves on it, blue-green and deep, rougher in the wind"},
         {"water", {{"tint", "#4F9FB0"}, {"clarity", 6.0}, {"scatter", "#0E3A4A"}, {"refraction", 0.05}, {"shore_fade", 0.2},
                    {"roughness", 0.08}, {"ripple", 0.8}, {"ripple_scale", 1.5}, {"wave_height", 0.6}, {"wave_length", 14.0},
                    {"wave_direction", 160.0}, {"choppiness", 0.55}, {"wind", 0.3}}}},
    });
    return presets;
}

const Json* water_preset(std::string_view name) {
    for (const auto& p : water_presets())
        if (p["name"].get<std::string>() == name) return &p;
    return nullptr;
}

Result<Json> load_water_file(const std::filesystem::path& file) {
    auto text = fs::read_file(file, 1 << 20);
    if (!text) return std::unexpected(text.error());
    auto json = parse_json(*text, file.generic_string());
    if (!json) return std::unexpected(json.error());
    if (!json->is_object() || !json->contains("noor_water") || !(*json)["noor_water"].is_number_integer())
        return fail(ErrorCode::ParseError, std::format("{}: not a water file (no \"noor_water\")", file.filename().string()));
    if ((*json)["noor_water"].get<int>() > kWaterFormat)
        return fail(ErrorCode::ParseError, std::format("{}: made by a newer NoorEngine (format {})", file.filename().string(), (*json)["noor_water"].get<int>()));
    if (!json->contains("water") || !(*json)["water"].is_object())
        return fail(ErrorCode::ParseError, std::format("{}: no \"water\" settings", file.filename().string()));
    return std::move(*json);
}

math::Vec3 water_transmittance(const WaterLook& look, float metres) {
    const float m = std::max(metres, 0.0f);
    return {std::exp(-look.absorption.x * m), std::exp(-look.absorption.y * m), std::exp(-look.absorption.z * m)};
}

} // namespace noor::asset
