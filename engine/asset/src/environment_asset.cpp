#include "noor/asset/environment_asset.h"

#include "noor/foundation/file_io.h"

#include <format>

namespace noor::asset {

namespace {

Json base(double time_of_day, double day_of_year) {
    return Json{{"sky", "atmosphere"}, {"time_of_day", time_of_day}, {"latitude", 50}, {"day_of_year", day_of_year},
                {"turbidity", 1.5},    {"ground_albedo", 0.25},      {"stars", true},   {"background", true},
                {"ev100", "auto"}};
}

std::vector<EnvironmentPreset> make_presets() {
    std::vector<EnvironmentPreset> out;
    {
        Json e = base(12.5, 172);
        out.push_back({"Clear Noon", "A cloudless midsummer noon: a deep blue sky and hard shadows.", std::move(e)});
    }
    {
        Json e = base(19.3, 220);
        e["turbidity"] = 2.5;
        e.update({{"fog_density", 0.0015}, {"fog_height", 2}, {"fog_falloff", 0.08}, {"fog_anisotropy", 0.7}, {"fog_color", "#FFFFFF"},
                  {"aerial_scale", 10},
                  {"cloud2_coverage", 0.3}, {"cloud2_density", 1}, {"cloud2_altitude", 8000}, {"cloud2_scale", 6000},
                  {"cloud2_wind_speed", 20}, {"cloud2_wind_direction", 80}, {"cloud2_seed", 9}});
        out.push_back({"Golden Evening", "The sun low in a late-summer evening: warm light, long shadows, a thin high veil.", std::move(e)});
    }
    {
        Json e = base(13.0, 300);
        e["turbidity"] = 3.0;
        e.update({{"cloud_coverage", 1.0}, {"cloud_density", 8}, {"cloud_altitude", 1200}, {"cloud_scale", 2000},
                  {"cloud_wind_speed", 6}, {"cloud_wind_direction", 60}, {"cloud_seed", 4},
                  {"fog_density", 0.0008}, {"fog_height", 0}, {"fog_falloff", 0.03}, {"fog_anisotropy", 0.3}, {"fog_color", "#FFFFFF"}});
        out.push_back({"Overcast", "A grey autumn day under a full cloud deck: soft light, no shadows.", std::move(e)});
    }
    {
        Json e = base(6.4, 260);
        e["turbidity"] = 3.0;
        e.update({{"fog_density", 0.02}, {"fog_height", 4}, {"fog_falloff", 0.12}, {"fog_anisotropy", 0.75}, {"fog_color", "#FFFFFF"},
                  {"aerial_scale", 10}});
        out.push_back({"Foggy Dawn", "Sunrise in September with mist lying in the hollows.", std::move(e)});
    }
    {
        Json e = base(23.0, 230);
        e.update({{"moon", true}, {"moon_phase", 0.5}, {"fog_density", 0.002}, {"fog_height", 1}, {"fog_falloff", 0.15},
                  {"fog_anisotropy", 0.5}, {"fog_color", "#FFFFFF"}});
        out.push_back({"Moonlit Night", "Late evening under a full moon, stars overhead.", std::move(e)});
    }
    {
        Json e = base(14.5, 290);
        e["turbidity"] = 4.0;
        e.update({{"cloud_coverage", 0.6}, {"cloud_density", 40}, {"cloud_altitude", 900}, {"cloud_scale", 1800},
                  {"cloud_wind_speed", 18}, {"cloud_wind_direction", 110}, {"cloud_seed", 7},
                  {"cloud2_coverage", 0.6}, {"cloud2_density", 2}, {"cloud2_altitude", 6000}, {"cloud2_scale", 5000},
                  {"cloud2_wind_speed", 30}, {"cloud2_wind_direction", 100}, {"cloud2_seed", 3},
                  {"fog_density", 0.004}, {"fog_height", 1}, {"fog_falloff", 0.05}, {"fog_anisotropy", 0.4}, {"fog_color", "#C8D2DC"}});
        out.push_back({"Stormy Coast", "A blustery afternoon: heavy, fast clouds with gaps of hard light, sea haze.", std::move(e)});
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

Json merge_environment(const Json& asset_environment, const Json& component) {
    Json merged = asset_environment.is_object() ? asset_environment : Json::object();
    if (component.is_object())
        for (const auto& [key, value] : component.items())
            if (key != "asset") merged[key] = value;
    return merged;
}

} // namespace noor::asset
