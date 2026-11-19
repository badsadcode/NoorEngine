#pragma once
// Built-in effect presets (Milestone 8B, part 4, ADR-035): starting points for the Effects panel, like the environment
// presets (ADR-033). They're never written: applying one copies it into the project (Assets/Effects/<name>.noorfx) and
// that copy remembers which preset it came from ("preset"), so it can be reset to it.

#include "noor/asset/json.h"

#include <string>
#include <string_view>
#include <vector>

namespace noor::fx {

struct EffectPreset {
    std::string name;
    std::string description;
    asset::Json emitter;  // an emitter component's settings (particles.h), "render" included
};

// In gallery order: Fire, Smoke, Sparks, Dust, Rain splash, Magic.
[[nodiscard]] const std::vector<EffectPreset>& effect_presets();
[[nodiscard]] const EffectPreset* find_effect_preset(std::string_view name);

} // namespace noor::fx
