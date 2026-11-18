#pragma once
// Terrain sculpting brushes (Milestone 6, part 6; ADR-031). Editor and tools only: games read the
// sculpt layer these write (noor/terrain), never this code (the runtime-boundary test checks).
//
// A brush works on the sculpt layer only: the generated base never changes, so regenerating it (a new
// seed, another heightmap) keeps every manual edit on top.
#include "noor/foundation/math.h"
#include "noor/terrain/terrain.h"

#include <optional>
#include <string_view>

namespace noor::terrain_edit {

enum class Tool : std::uint8_t { Raise, Lower, Smooth, Flatten };
[[nodiscard]] std::optional<Tool> tool_from_name(std::string_view name);
[[nodiscard]] std::string_view tool_name(Tool tool);

struct Brush {
    Tool tool = Tool::Raise;
    float radius = 8.0f;    // metres
    // Raise/Lower/Flatten: metres per second at the brush's centre. Smooth: how much of the way to the
    // local average per second (1 = all the way in one second).
    float strength = 2.0f;
    float falloff = 0.5f;   // 0: a hard edge; 1: soft all the way from the centre
    float flatten_height = 0.0f;  // Flatten: the height to move towards (where the stroke began)
};

// How much of the brush reaches distance d from its centre: 1 inside, easing to 0 at the radius.
[[nodiscard]] float brush_weight(const Brush& brush, float distance);

// One dab of the brush at `centre` (the terrain's space, x and z) for `seconds`. Reads the current
// heights (base + layer) and changes `layer`. Returns the samples it changed, or nothing when the brush
// missed the terrain. The caller recomposes the heights there (Heights::recompose).
std::optional<terrain::SampleRect> apply(const terrain::Heights& current, terrain::SculptLayer& layer, const Brush& brush,
                                         math::Vec2 centre, float seconds);

} // namespace noor::terrain_edit
