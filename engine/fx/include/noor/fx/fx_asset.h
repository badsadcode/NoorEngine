#pragma once
// Effect assets (Milestone 8B, part 1, ADR-035): a `.noorfx` file holds an emitter's settings, so effects can be
// shared, previewed and placed many times. An `emitter` component points at one with {"asset": "<id>"}; any other
// field beside it overrides the file's, field by field (like environment assets, ADR-033).
//
//   {"noor_fx": 1, "name": "Campfire smoke", "preset": "Smoke", "emitter": {"rate": 12, ...}}

#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <filesystem>
#include <string_view>

namespace noor::fx {

inline constexpr std::string_view kFxExtension = ".noorfx";
inline constexpr int kFxFormat = 1;

// A `.noorfx` file's JSON.
[[nodiscard]] asset::Json fx_file_json(std::string_view name, const asset::Json& emitter, std::string_view preset = {});

// Reads and checks a `.noorfx` file (the format, an "emitter" object). Returns the whole file's JSON.
[[nodiscard]] Result<asset::Json> load_fx_file(const std::filesystem::path& file);

// The settings an emitter component ends up with: the asset's, then the component's own (everything but "asset").
[[nodiscard]] asset::Json merge_emitter(const asset::Json& asset_emitter, const asset::Json& component);

} // namespace noor::fx
