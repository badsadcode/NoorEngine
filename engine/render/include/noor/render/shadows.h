#pragma once
// Cascaded shadow maps for the sun (ADR-012, Milestone 5 part 4): pure maths, no GPU, so it can be
// tested. The view distance up to `shadow_distance` is cut into kShadowCascades slices (near ones
// short and sharp, far ones long), each covered by one orthographic shadow map seen from the sun.
//
// Stability: each slice is wrapped in a sphere, so its shadow map's size never changes when the
// camera turns, and the map's position is snapped to whole texels, so shadow edges don't crawl
// when the camera moves ("shimmering").
#include "noor/foundation/math.h"

#include <array>
#include <cstdint>

namespace noor::render {

inline constexpr std::uint32_t kShadowCascades = 4;

struct Camera;

struct ShadowCascade {
    math::Mat4 clip_from_world;  // world -> shadow map clip space (x, y in -1..1, depth 0..1)
    float split_far = 0.0f;      // where this cascade ends, in metres along the camera's view
    float texel_world = 0.0f;    // size of one shadow-map texel in metres (for normal-offset bias)
};

struct ShadowCascadeParams {
    float shadow_distance = 80.0f;  // shadows end here (metres from the camera)
    std::uint32_t resolution = 2048;
    float split_lambda = 0.75f;     // 0: equal slices, 1: logarithmic (finer near the camera)
    float caster_reach = 150.0f;    // also catch casters this far behind a slice, towards the sun
};

// The cascades for a camera (view matrix, fov, near plane; `aspect` = width / height) and a sun
// shining along `sun_direction`.
[[nodiscard]] std::array<ShadowCascade, kShadowCascades> compute_cascades(const Camera& camera, float aspect, math::Vec3 sun_direction,
                                                                          const ShadowCascadeParams& params);

// Can a caster with this world-space bounding sphere put a shadow into the cascade? Tests the
// cascade's sides only (casters between it and the sun, however far, still count).
[[nodiscard]] bool sphere_touches_cascade(const ShadowCascade& cascade, math::Vec3 center, float radius) noexcept;

} // namespace noor::render
