#pragma once
// Height fog (Milestone 8A, part 3, ADR-033): the maths, in C++ for tests and as the reference for
// shaders/mesh.hlsl and shaders/sky.hlsl, which compute the same per pixel. Keep them in step.
//
// The fog is a medium at full density below `height` that thins exponentially above it:
//     density(h) = density * exp(-falloff * max(0, h - height))      (extinction per metre)
// so valleys fill evenly and hilltops poke out. Along a straight ray the optical depth has a closed form, so it
// costs a few exponentials per pixel and no marching. The light it scatters towards the camera is the
// sun (through a Henyey-Greenstein phase: fog glows towards the sun) plus the sky's average radiance,
// assumed constant along the ray, which makes the in-scattering exact for that lighting:
//     colour = surface * T + albedo * (1 - T) * (sun * phase(cos) + ambient),   T = exp(-optical depth).
// No shadows inside the fog (volumetric shadows are a later phase).

#include "noor/foundation/math.h"

#include <algorithm>
#include <cmath>

namespace noor::render {

struct FogParams {
    float density = 0.0f;      // extinction per metre at `height` (0: no fog); 0.01 is thick, 0.001 a light haze
    float falloff = 0.05f;     // per metre: the density halves every ln 2 / falloff metres of height (0.05: about 14 m)
    float height = 0.0f;       // metres: where `density` applies
    float anisotropy = 0.6f;   // Henyey-Greenstein g: 0 scatters evenly, towards 1 glows round the sun
    math::Vec3 albedo{1, 1, 1};  // the fraction of light the fog scatters rather than absorbs (a tint)
};

[[nodiscard]] inline float fog_density_at(const FogParams& f, float h) noexcept {
    return f.density * std::exp(-f.falloff * std::max(0.0f, h - f.height));
}

namespace detail {
// ∫ density along a ray that starts at height h0 >= height and stays above it, for `length` metres,
// climbing `dy` metres per metre (dy may be negative as long as the ray stays above `height`).
inline float fog_depth_above(const FogParams& f, float h0, float dy, float length) noexcept {
    const float start = fog_density_at(f, h0);
    const float k = f.falloff * dy;
    if (std::abs(k * length) < 1e-4f) return start * length;
    return start * (1.0f - std::exp(-k * length)) / k;
}
} // namespace detail

// Optical depth from `origin` along unit direction `dir` for `distance` metres (closed form, in two
// pieces: the part of the ray below `height` at full density, the part above thinning).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4723)  // "potential divide by zero": only in the level-ray branch, which never divides
#endif
[[nodiscard]] inline float fog_optical_depth(const FogParams& f, math::Vec3 origin, math::Vec3 dir, float distance) noexcept {
    if (f.density <= 0.0f || distance <= 0.0f) return 0.0f;
    const float h0 = origin.y, dy = dir.y;
    // Where the ray crosses the fog's height (if it does within the distance).
    const float cross = std::abs(dy) > 1e-7f ? (f.height - h0) / dy : -1.0f;
    if (cross <= 0.0f || cross >= distance) {  // all on one side
        return h0 <= f.height && (h0 < f.height || dy <= 0.0f) ? f.density * distance : detail::fog_depth_above(f, h0, dy, distance);
    }
    if (h0 < f.height)  // starts below, climbs out
        return f.density * cross + detail::fog_depth_above(f, f.height, dy, distance - cross);
    return detail::fog_depth_above(f, h0, dy, cross) + f.density * (distance - cross);  // starts above, sinks in
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// To the end of the world: a ray climbing out of the fog sees a finite depth; level or falling rays see as
// much as `far_distance` (the sky behind them is fully fogged in practice).
[[nodiscard]] inline float fog_optical_depth_to_infinity(const FogParams& f, math::Vec3 origin, math::Vec3 dir,
                                                         float far_distance = 100000.0f) noexcept {
    if (f.density <= 0.0f) return 0.0f;
    if (dir.y > 1e-6f && f.falloff > 0.0f) {
        const float below = origin.y < f.height ? (f.height - origin.y) / dir.y : 0.0f;
        return f.density * below + fog_density_at(f, std::max(origin.y, f.height)) / (f.falloff * dir.y);
    }
    return fog_optical_depth(f, origin, dir, far_distance);
}

// Henyey-Greenstein phase function (integrates to 1 over the sphere).
[[nodiscard]] inline float fog_phase(float cos_angle, float g) noexcept {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * math::kPi * std::pow(std::max(1e-6f, 1.0f + g2 - 2.0f * g * cos_angle), 1.5f));
}

struct FogResult {
    float transmittance = 1.0f;          // how much of what's behind gets through
    math::Vec3 inscatter{0, 0, 0};       // radiance added by the fog
};

// The fog between the camera at `origin` and a point `distance` metres along unit `dir` (pass
// infinity = true for the sky), lit by `sun_radiance` (colour × illuminance) towards `to_sun`, and the
// sky's average radiance `ambient`.
[[nodiscard]] inline FogResult fog(const FogParams& f, math::Vec3 origin, math::Vec3 dir, float distance, math::Vec3 to_sun,
                                   math::Vec3 sun_radiance, math::Vec3 ambient, bool infinity = false) noexcept {
    FogResult r;
    const float tau = infinity ? fog_optical_depth_to_infinity(f, origin, dir) : fog_optical_depth(f, origin, dir, distance);
    r.transmittance = std::exp(-tau);
    const math::Vec3 light = sun_radiance * fog_phase(math::dot(dir, to_sun), f.anisotropy) + ambient;
    const float scattered = 1.0f - r.transmittance;
    r.inscatter = {f.albedo.x * light.x * scattered, f.albedo.y * light.y * scattered, f.albedo.z * light.z * scattered};
    return r;
}

} // namespace noor::render
