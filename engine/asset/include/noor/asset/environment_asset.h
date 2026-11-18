#pragma once
// Environment assets (Milestone 8A, part 6, ADR-033): a `.noorenv` file holds every setting of a scene's
// `environment` component (sky, time of day, fog, clouds, night, exposure), so environments can be shared,
// previewed and swapped. A scene points at one with {"asset": "<id>"} on its environment component; any other
// field written beside "asset" overrides the file's, field by field.
//
//   {"noor_environment": 1, "name": "Golden Evening", "preset": "Golden Evening",
//    "environment": {"sky": "atmosphere", "time_of_day": 19.3, ..., "ev100": "auto"}}
//
// Nine presets are built into the engine (environment_presets()): they're never written, so editing or duplicating
// an environment can't change them. Applying one writes a copy into the project (Assets/Environments) that
// remembers which preset it came from ("preset"), so it can be reset to it.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noor::asset {

inline constexpr std::string_view kEnvironmentExtension = ".noorenv";
inline constexpr int kEnvironmentFormat = 1;

struct EnvironmentPreset {
    std::string name;
    std::string description;
    Json environment;  // the environment component's fields
};

// The built-in presets, in gallery order: Clear Noon, Fair Weather, Golden Evening, Dusk, Overcast, Foggy Dawn, Pink Mist,
// Moonlit Night, Stormy Coast.
[[nodiscard]] const std::vector<EnvironmentPreset>& environment_presets();
[[nodiscard]] const EnvironmentPreset* find_environment_preset(std::string_view name);

// A `.noorenv` file's JSON.
[[nodiscard]] Json environment_file_json(std::string_view name, const Json& environment, std::string_view preset = {});

// Reads and checks a `.noorenv` file (the format, an "environment" object). Returns the whole file's JSON.
[[nodiscard]] Result<Json> load_environment_file(const std::filesystem::path& file);

// Blends two environments' settings (Milestone 8A, part 7: weather zones). w = 0 gives `from`, 1 gives `to`:
//   - numbers blend linearly; time_of_day (24 h), moon_phase (1), day_of_year (365) and north (360) the short way
//     round; seeds switch at w = 0.5;
//   - colours ("#RRGGBB") blend in linear light;
//   - anything else (sky mode, switches, "auto") switches at w = 0.5;
//   - a setting only one side has blends from its neutral value where it has one (no fog, no cloud cover, turbidity 1,
//     aerial scale 1, star brightness 1), and is taken as it is where it hasn't (a cloud layer's altitude, the sky
//     mode): a zone that only says "fog_density" keeps the scene's sky, time and sun, even fully on.
[[nodiscard]] Json blend_environments(const Json& from, const Json& to, float w);

// The settings a scene's environment component ends up with: the asset's fields, then the component's own
// (everything but "asset") on top.
[[nodiscard]] Json merge_environment(const Json& asset_environment, const Json& component);

} // namespace noor::asset
