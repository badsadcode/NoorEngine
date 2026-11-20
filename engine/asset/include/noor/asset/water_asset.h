#pragma once
// Water's look (Milestone 8D, part 1, ADR-037): the settings a `water` component draws with, from its `.noorwater` asset
// (or a built-in preset) with the component's own fields over them, as environments and effects do.
//
//   {"noor_water": 1, "name": "Lake", "preset": "Lake",
//    "water": {"tint": "#3A8C80", "clarity": 3.0, "scatter": "#1F4A48", "refraction": 0.06, "shore_fade": 0.12,
//              "roughness": 0.06, "ripple": 1.0, "ripple_scale": 1.2}}
//
// Colour by depth comes from absorption: light loses a share of each colour per metre. Say it either way:
//   "absorption": [r, g, b] per metre (linear), or
//   "tint" (the colour white light has become after "clarity" metres of water) and "clarity" (metres):
//   absorption = -ln(tint) / clarity, per channel (sRGB tint, linearised first).
// "scatter": the colour light scattered back from inside the water takes on (an albedo; dark: clear water, light:
// milky). "refraction": how far what's under is bent (a fraction of the screen, at a metre deep). "shore_fade": metres
// of depth over which the water fades in at the shore. "roughness": blurs the sky's reflection. "ripple" (0: a mirror)
// and "ripple_scale" (metres): small ripples. The component adds "size": [x, z] (metres, before the entity's scale).
// Waves (part 2): "wave_height" (metres, crest to trough of the main wave; 0: none), "wave_length" (metres),
// "wave_direction" (degrees towards which they travel, 0 = -Z, 90 = +X, as the wind), "choppiness" (0 round .. 0.9
// sharp crests) and "wind" (0..1: how much the environment's ground wind raises them). Three smaller waves come with
// the main one (game/water.h).
// Shorelines and reflections (part 3): "foam" (0..1: how much foam there is at all), "foam_depth" (metres of shallow
// water where foam gathers at the shore and round things standing in it), "crest_foam" (0..1: how readily steep wave
// crests foam; 0: never), "wet" (0..1: how much darker wet ground is) and "wet_height" (metres above the water's reach
// that are still wet), "reflections" (0..1: nearby things reflected by screen-space reflection; 0: the sky only).
// Underwater (part 4): "caustics" (0..4: the sun's light gathered by the waves into a net on what's below; 0: none,
// 1: as strong as the default) and "shafts" (0..1: light shafts in the water, seen from inside it).

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace noor::asset {

inline constexpr std::string_view kWaterExtension = ".noorwater";
inline constexpr int kWaterFormat = 1;

struct WaterLook {
    math::Vec3 absorption{0.45f, 0.09f, 0.06f};  // per metre, linear
    math::Vec3 scatter{0.02f, 0.05f, 0.06f};     // linear albedo
    float refraction = 0.04f;
    float shore_fade = 0.12f;
    float roughness = 0.06f;
    float ripple = 1.0f;
    float ripple_scale = 1.2f;
    float wave_height = 0.0f;
    float wave_length = 8.0f;
    float wave_direction = 0.0f;  // degrees
    float choppiness = 0.5f;
    float wind = 0.0f;
    float foam = 0.5f;
    float foam_depth = 0.3f;   // metres
    float crest_foam = 0.0f;
    float wet = 0.35f;
    float wet_height = 0.12f;  // metres
    float reflections = 1.0f;
    float caustics = 1.0f;
    float shafts = 0.5f;

    // Fields present in `settings` replace these (unknown fields are ignored; bad values keep the default).
    void apply(const Json& settings);
};

// The built-in presets: [{"name", "description", "water"}] (never written to): "Lake", "Sea", "Deep sea".
[[nodiscard]] const Json& water_presets();
[[nodiscard]] const Json* water_preset(std::string_view name);

// A .noorwater file (checked: its format and that "water" is an object).
[[nodiscard]] Result<Json> load_water_file(const std::filesystem::path& file);

// How much of each colour gets through `metres` of water (exp(-absorption * metres)).
[[nodiscard]] math::Vec3 water_transmittance(const WaterLook& look, float metres);

// Absorption from a tint and a clarity (see above).
[[nodiscard]] math::Vec3 absorption_from_tint(math::Vec3 tint_linear, float clarity);

// The environment's underwater settings (8D part 4, flat keys so weather zones blend them like fog): the water's look
// seen from inside it, over each body's own while the camera is in it: "underwater_tint" + "underwater_clarity" (or
// "underwater_absorption": [r, g, b]), "underwater_scatter", "underwater_caustics", "underwater_shafts". Nullopt when
// the environment has none of them.
struct UnderwaterLook {
    math::Vec3 absorption;
    math::Vec3 scatter;
    float caustics;
    float shafts;
};
[[nodiscard]] std::optional<UnderwaterLook> underwater_look(const Json& environment, const WaterLook& base);

} // namespace noor::asset
