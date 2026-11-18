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

// The settings a scene's environment component ends up with: the asset's fields, then the component's own
// (everything but "asset") on top.
[[nodiscard]] Json merge_environment(const Json& asset_environment, const Json& component);

} // namespace noor::asset
