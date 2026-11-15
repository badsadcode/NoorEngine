#pragma once
// Built-in runtime behaviours: what Play runs before scripting exists (ADR-026).
//
// Milestone 3 has exactly one, `rotator`, so play mode can be shown and tested to actually
// tick. Scripts (C#/Lua, Milestone 4) replace this as the way to write behaviour; built-ins stay
// for trivially common cases.
//
//   "rotator": {"axis": [0, 1, 0], "degrees_per_second": 45}
//     Turns the entity about `axis` (in its parent's space) at the given rate. Defaults: +Y, 45.

#include "noor/scene/scene.h"

namespace noor::scene {

// Advances every behaviour in `scene` by `seconds`. Returns how many entities were updated.
std::size_t update_behaviours(Scene& scene, double seconds);

} // namespace noor::scene
