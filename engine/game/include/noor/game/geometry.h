#pragma once
// Built-in meshes for `primitive` components (and editor helpers): vertices and triangle indices.

#include "noor/foundation/math.h"
#include "noor/render/renderer.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace noor::game {

using Geometry = std::pair<std::vector<render::Vertex>, std::vector<std::uint32_t>>;

// An axis-aligned box from lo to hi, one color, counter-clockwise faces.
[[nodiscard]] Geometry make_box(math::Vec3 color, math::Vec3 lo = {-1, -1, -1}, math::Vec3 hi = {1, 1, 1});

// A sphere (height <= 2 * radius) or a capsule along Y with that total height, centred on the origin.
[[nodiscard]] Geometry make_round(float radius, float height);

} // namespace noor::game
