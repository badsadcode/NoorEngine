#include "noor/asset/environment_asset.h"

#include "noor/foundation/file_io.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <optional>

namespace noor::asset {

namespace {

Json base(double time_of_day, double day_of_year) {
    return Json{{"sky", "atmosphere"}, {"time_of_day", time_of_day}, {"latitude", 50}, {"day_of_year", day_of_year},
                {"turbidity", 1.5},    {"ground_albedo", 0.25},      {"stars", true},   {"background", true},
                {"ev100", "auto"}};
}

// The presets in gallery order. Tuned on 2026-11-18 from three rendered variants of each (on Hillside): where more than
// one was good, both shipped (Fair Weather, Dusk, Pink Mist).
std::vector<EnvironmentPreset> make_presets() {
    std::vector<EnvironmentPreset> out;
    const Json cumulus{{"cloud_coverage", 0.3},   {"cloud_density", 6},        {"cloud_altitude", 1500}, {"cloud_scale", 2200},
                       {"cloud_wind_speed", 8},   {"cloud_wind_direction", 60}, {"cloud_seed", 4}};
    const Json evening_veil{{"cloud2_coverage", 0.3},  {"cloud2_density", 1},         {"cloud2_altitude", 8000}, {"cloud2_scale", 6000},
                            {"cloud2_wind_speed", 20}, {"cloud2_wind_direction", 80}, {"cloud2_seed", 9}};
    const Json evening_haze{{"fog_density", 0.0015}, {"fog_height", 2}, {"fog_falloff", 0.08}, {"fog_anisotropy", 0.7}, {"fog_color", "#FFFFFF"},
                            {"aerial_scale", 10}};
    {
        Json e = base(12.5, 172);
        out.push_back({"Clear Noon", "A cloudless midsummer noon: a deep blue sky and hard shadows.", std::move(e)});
    }
    {
        Json e = base(12.5, 172);
        e.update(cumulus);
        out.push_back({"Fair Weather", "A summer noon with fair-weather clouds drifting over.", std::move(e)});
    }
    {
        Json e = base(19.3, 220);
        e["turbidity"] = 2.5;
        e.update(evening_haze);
        e.update(evening_veil);
        out.push_back({"Golden Evening", "The sun low in a late-summer evening: warm light, long shadows, a thin high veil.", std::move(e)});
    }
    {
        Json e = base(19.8, 220);
        e["turbidity"] = 2.5;
        e.update(evening_haze);
        e.update(evening_veil);
        out.push_back({"Dusk", "Just after sunset: a violet sky, the land going dark.", std::move(e)});
    }
    {
        Json e = base(13.0, 300);
        e["turbidity"] = 4.0;
        e.update({{"cloud_coverage", 1.0}, {"cloud_density", 14}, {"cloud_altitude", 1200}, {"cloud_scale", 2000},
                  {"cloud_wind_speed", 6}, {"cloud_wind_direction", 60}, {"cloud_seed", 4},
                  {"fog_density", 0.002}, {"fog_height", 0}, {"fog_falloff", 0.03}, {"fog_anisotropy", 0.3}, {"fog_color", "#D8DCE0"}});
        out.push_back({"Overcast", "A grey autumn day under a heavy cloud deck: soft light, no shadows.", std::move(e)});
    }
    {
        Json e = base(6.4, 260);
        e["turbidity"] = 3.0;
        e.update({{"fog_density", 0.02}, {"fog_height", 4}, {"fog_falloff", 0.12}, {"fog_anisotropy", 0.75}, {"fog_color", "#FFFFFF"},
                  {"aerial_scale", 10}});
        out.push_back({"Foggy Dawn", "Sunrise in September with mist lying in the hollows.", std::move(e)});
    }
    {
        Json e = base(5.9, 260);
        e["turbidity"] = 3.0;
        e.update({{"fog_density", 0.03}, {"fog_height", 4}, {"fog_falloff", 0.12}, {"fog_anisotropy", 0.75}, {"fog_color", "#FFFFFF"},
                  {"aerial_scale", 10}});
        out.push_back({"Pink Mist", "Before sunrise: pink twilight glowing through the mist.", std::move(e)});
    }
    {
        Json e = base(21.5, 230);
        e.update({{"moon", true}, {"moon_phase", 0.25}, {"star_brightness", 2}, {"fog_density", 0.002}, {"fog_height", 1},
                  {"fog_falloff", 0.15}, {"fog_anisotropy", 0.5}, {"fog_color", "#FFFFFF"}});
        out.push_back({"Moonlit Night", "A half moon in the last of the twilight, the sky full of stars (twice as bright as real).", std::move(e)});
    }
    {
        Json e = base(17.3, 290);
        e["turbidity"] = 5.0;
        e.update({{"cloud_coverage", 0.7}, {"cloud_density", 60}, {"cloud_altitude", 900}, {"cloud_scale", 1800},
                  {"cloud_wind_speed", 18}, {"cloud_wind_direction", 110}, {"cloud_seed", 7},
                  {"cloud2_coverage", 0.8}, {"cloud2_density", 4}, {"cloud2_altitude", 6000}, {"cloud2_scale", 5000},
                  {"cloud2_wind_speed", 30}, {"cloud2_wind_direction", 100}, {"cloud2_seed", 3},
                  {"fog_density", 0.004}, {"fog_height", 1}, {"fog_falloff", 0.05}, {"fog_anisotropy", 0.4}, {"fog_color", "#C8D2DC"}});
        out.push_back({"Stormy Coast", "A storm rolling in at the end of the day: dark clouds, a red sky where the low sun breaks through.", std::move(e)});
    }
    return out;
}

} // namespace

const std::vector<EnvironmentPreset>& environment_presets() {
    static const std::vector<EnvironmentPreset> presets = make_presets();
    return presets;
}

const EnvironmentPreset* find_environment_preset(std::string_view name) {
    for (const auto& p : environment_presets())
        if (p.name == name) return &p;
    return nullptr;
}

Json environment_file_json(std::string_view name, const Json& environment, std::string_view preset) {
    Json j{{"noor_environment", kEnvironmentFormat}, {"name", std::string(name)}};
    if (!preset.empty()) j["preset"] = std::string(preset);
    j["environment"] = environment;
    return j;
}

Result<Json> load_environment_file(const std::filesystem::path& file) {
    auto text = fs::read_file(file, 4 << 20);
    if (!text) return std::unexpected(text.error());
    auto json = parse_json(*text, file.generic_string());
    if (!json) return std::unexpected(json.error());
    if (!json->is_object() || !json->contains("noor_environment") || !(*json)["noor_environment"].is_number_integer())
        return fail(ErrorCode::ParseError, std::format("{}: not an environment file (no \"noor_environment\")", file.filename().string()));
    if ((*json)["noor_environment"].get<int>() > kEnvironmentFormat)
        return fail(ErrorCode::ParseError, std::format("{}: made by a newer NoorEngine (format {})", file.filename().string(),
                                                       (*json)["noor_environment"].get<int>()));
    if (!json->contains("environment") || !(*json)["environment"].is_object())
        return fail(ErrorCode::ParseError, std::format("{}: no \"environment\" settings", file.filename().string()));
    return std::move(*json);
}

namespace {

// A setting's neutral value: what an environment without it behaves as.
const Json* neutral(const std::string& key) {
    static const Json values = Json{{"fog_density", 0.0}, {"cloud_coverage", 0.0}, {"cloud2_coverage", 0.0}, {"turbidity", 1.0},
                                    {"aerial_scale", 1.0}, {"star_brightness", 1.0}, {"ground_albedo", 0.3}, {"altitude", 0.0}};
    const auto it = values.find(key);
    return it == values.end() ? nullptr : &*it;
}

// The period of a setting that goes round (0 for none).
double period(const std::string& key) {
    if (key == "time_of_day") return 24.0;
    if (key == "moon_phase") return 1.0;
    if (key == "day_of_year") return 365.0;
    if (key == "north") return 360.0;
    // Wind directions (degrees): the ground wind (8B) and the cloud layers' (8A; missed until 8B, when a zone could
    // swing the clouds' wind the long way round).
    if (key.ends_with("wind_direction")) return 360.0;
    return 0.0;
}

std::optional<std::array<double, 3>> linear_color(const Json& v) {
    if (!v.is_string()) return std::nullopt;
    const std::string hex = v.get<std::string>();
    if (hex.size() != 7 || hex[0] != '#') return std::nullopt;
    std::array<double, 3> c{};
    for (int i = 0; i < 3; ++i) {
        const auto digits = hex.substr(static_cast<std::size_t>(1 + 2 * i), 2);
        if (!std::isxdigit(static_cast<unsigned char>(digits[0])) || !std::isxdigit(static_cast<unsigned char>(digits[1]))) return std::nullopt;
        const double s = std::stoi(digits, nullptr, 16) / 255.0;
        c[static_cast<std::size_t>(i)] = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    }
    return c;
}

std::string hex_of(const std::array<double, 3>& linear) {
    std::string out = "#";
    for (const double l : linear) {
        const double s = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
        out += std::format("{:02X}", static_cast<int>(std::lround(std::clamp(s, 0.0, 1.0) * 255.0)));
    }
    return out;
}

Json blend_value(const std::string& key, const Json& a, const Json& b, double w) {
    if (a.is_number() && b.is_number() && key.find("seed") == std::string::npos) {
        const double x = a.get<double>(), y = b.get<double>();
        if (const double p = period(key); p > 0.0) {
            double d = std::fmod(y - x, p);  // the short way round
            if (d > p / 2) d -= p;
            if (d < -p / 2) d += p;
            double r = std::fmod(x + d * w, p);
            if (r < 0) r += p;
            return r;
        }
        return x + (y - x) * w;
    }
    if (const auto ca = linear_color(a), cb = linear_color(b); ca && cb) {
        std::array<double, 3> c{};
        for (std::size_t i = 0; i < 3; ++i) c[i] = (*ca)[i] + ((*cb)[i] - (*ca)[i]) * w;
        return hex_of(c);
    }
    return w < 0.5 ? a : b;
}

} // namespace

Json blend_environments(const Json& from, const Json& to, float weight) {
    const double w = std::clamp(static_cast<double>(weight), 0.0, 1.0);
    const Json a = from.is_object() ? from : Json::object(), b = to.is_object() ? to : Json::object();
    if (w <= 0.0) return a;
    // Fully on is the same rule as nearly on (not just `to`): what the zone doesn't mention without a neutral value
    // (the sky mode, the time of day) stays the scene's. A fog zone over an atmosphere sky keeps the atmosphere.
    const bool full = w >= 1.0;
    Json out = Json::object();
    for (const auto& [key, va] : a.items()) {
        if (b.contains(key)) out[key] = full ? b[key] : blend_value(key, va, b[key], w);
        else if (const Json* n = neutral(key)) out[key] = full ? *n : blend_value(key, va, *n, w);
        else out[key] = va;
    }
    for (const auto& [key, vb] : b.items()) {
        if (a.contains(key)) continue;
        if (const Json* n = neutral(key)) out[key] = full ? vb : blend_value(key, *n, vb, w);
        else out[key] = vb;
    }
    return out;
}

Json merge_environment(const Json& asset_environment, const Json& component) {
    Json merged = asset_environment.is_object() ? asset_environment : Json::object();
    if (component.is_object())
        for (const auto& [key, value] : component.items())
            if (key != "asset") merged[key] = value;
    return merged;
}

} // namespace noor::asset
