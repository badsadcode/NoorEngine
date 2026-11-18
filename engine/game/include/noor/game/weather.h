#pragma once
// Weather zones (Milestone 8A, part 7, ADR-033): places where the environment is different, blended in by where the
// camera is. A `weather_zone` component:
//
//   {"shape": "box" | "sphere", "size": [40, 20, 40] (metres, box), "radius": 20 (sphere), "blend": 15 (metres),
//    "priority": 0, "environment": "<.noorenv id>" or {settings}, "weight": 0..1 (optional)}
//
// The zone sits on its entity (position and rotation; the size is in metres, not scaled by the entity). Inside it the
// zone's environment is fully on; outside it fades out smoothly over `blend` metres. A "weight" field, when present,
// replaces that (a script rolling a storm in). Zones apply in order of priority (lower first, so higher wins), each
// blended over the result so far with asset::blend_environments.

#include "noor/asset/json.h"
#include "noor/foundation/math.h"

namespace noor::game {

// How much of a zone applies to a viewer at `viewer` (world): 1 inside, smoothly 0 by `blend` metres outside.
[[nodiscard]] float weather_zone_weight(const asset::Json& zone, const math::Mat4& world, math::Vec3 viewer);

} // namespace noor::game
