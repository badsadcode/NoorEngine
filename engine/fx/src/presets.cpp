#include "noor/fx/presets.h"

namespace noor::fx {

using asset::Json;

const std::vector<EffectPreset>& effect_presets() {
    static const std::vector<EffectPreset> presets = [] {
        std::vector<EffectPreset> p;
        p.push_back({"Fire", "Flames rising from a small fire: additive, glowing, short-lived.",
                     Json::parse(R"({"rate": 90, "shape": {"type": "cone", "angle": 15, "radius": 0.25}, "lifetime": [0.4, 0.8],
                       "speed": [0.8, 1.6], "gravity": -0.3, "drag": 1.0, "size": [[0, 0.5], [1, 0.12]],
                       "color": [[0, "#FFE0A0"], [0.5, "#FF7020"], [1, "#801000"]], "alpha": [[0, 1], [1, 0]], "seed": 1,
                       "max_particles": 500, "render": {"blend": "additive", "emission": 8000}})")});
        p.push_back({"Smoke", "Grey smoke that rises, grows and drifts with the wind.",
                     Json::parse(R"({"rate": 10, "shape": {"type": "sphere", "radius": 0.15}, "lifetime": [2.5, 4.0], "speed": [0.2, 0.5],
                       "gravity": -0.08, "drag": 0.9, "wind": 1, "size": [[0, 0.3], [1, 1.6]], "color": "#8A8580",
                       "alpha": [[0, 0.0], [0.15, 0.6], [1, 0.0]], "seed": 3, "max_particles": 400, "render": {"blend": "alpha"}})")});
        p.push_back({"Sparks", "Bursts of hot sparks that fly out, fall and streak.",
                     Json::parse(R"({"rate": 0, "bursts": [{"time": 0.2, "count": 12, "interval": 0.5, "cycles": 0}], "shape": {"type": "point"},
                       "lifetime": [0.6, 1.2], "speed": [2, 4], "gravity": 1, "drag": 0.3, "size": 0.03, "color": "#FFC060",
                       "seed": 2, "max_particles": 300, "render": {"blend": "additive", "facing": "velocity", "stretch": 0.04, "emission": 30000}})")});
        p.push_back({"Dust", "Puffs of dust kicked up and settling: brown, slow, blown about.",
                     Json::parse(R"({"rate": 0, "bursts": [{"time": 0, "count": 25, "interval": 1.5, "cycles": 0}], "shape": {"type": "sphere", "radius": 0.4},
                       "lifetime": [1.5, 2.5], "speed": [0.3, 0.8], "gravity": 0.05, "drag": 1.5, "wind": 1, "size": [[0, 0.3], [1, 1.0]],
                       "color": "#9A8468", "alpha": [[0, 0.0], [0.1, 0.45], [1, 0.0]], "seed": 4, "max_particles": 300, "render": {"blend": "alpha"}})")});
        p.push_back({"Rain splash", "Small splashes where rain hits the ground, over a few square metres.",
                     Json::parse(R"({"rate": 140, "shape": {"type": "box", "size": [6, 0, 6]}, "lifetime": [0.2, 0.35], "speed": [0.8, 1.6],
                       "gravity": 1, "size": [[0, 0.03], [1, 0.015]], "color": "#C8D4E0", "alpha": [[0, 0.7], [1, 0.0]], "seed": 5,
                       "max_particles": 200, "render": {"blend": "alpha", "facing": "velocity", "stretch": 0.03}})")});
        p.push_back({"Magic", "A glowing swirl of violet and cyan motes that float up and fade.",
                     Json::parse(R"({"rate": 50, "shape": {"type": "sphere", "radius": 0.5, "surface": true}, "lifetime": [1.0, 2.0],
                       "speed": [0.1, 0.4], "gravity": -0.15, "drag": 1.2, "size": [[0, 0.02], [0.3, 0.08], [1, 0.0]],
                       "color": [[0, "#B070FF"], [1, "#40E0FF"]], "alpha": [[0, 0], [0.2, 1], [1, 0]], "seed": 6, "max_particles": 300,
                       "render": {"blend": "additive", "emission": 5000}})")});
        return p;
    }();
    return presets;
}

const EffectPreset* find_effect_preset(std::string_view name) {
    for (const auto& p : effect_presets())
        if (p.name == name) return &p;
    return nullptr;
}

} // namespace noor::fx
