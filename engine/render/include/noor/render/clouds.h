#pragma once
// Layered clouds (Milestone 8A, part 4, ADR-033): the maths, in C++ for tests and as the reference for
// shaders/clouds.hlsli (the sky pass, the scene's cloud shadows, the live sky's lighting). Keep in step.
//
// A layer is a thin shell of cloud at an altitude. Where a ray meets it, a deterministic noise (value
// noise, five octaves, from the layer's seed) decides how much cloud there is: `coverage` moves the
// threshold (0 clear, 1 overcast), `density` is the optical thickness of a full cloud (8 a heavy cumulus,
// 1 a thin cirrus veil), `scale` the size of its features. The wind slides the pattern: the same seed, wind
// and time always give the same sky.

#include "noor/foundation/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace noor::render {

struct CloudLayer {
    bool enabled = false;
    float coverage = 0.5f;        // 0 clear .. 1 overcast
    float density = 6.0f;         // optical thickness of a full cloud
    float altitude_km = 1.5f;     // above the ground
    float scale_km = 2.5f;        // feature size
    float wind_speed = 10.0f;     // metres per second
    float wind_direction = 0.0f;  // degrees: where the wind blows towards, 0 = -Z, 90 = +X
    std::uint32_t seed = 1;
};

inline constexpr std::size_t kCloudLayers = 2;

// The pattern's drift after `time` seconds (km, x and z): wind × time along its direction.
[[nodiscard]] inline math::Vec2 cloud_offset(const CloudLayer& layer, float time) noexcept {
    const float a = math::radians(layer.wind_direction);
    const float km = layer.wind_speed * time * 0.001f;
    return {std::sin(a) * km, -std::cos(a) * km};
}

namespace detail {
inline std::uint32_t cloud_hash(std::uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline float cloud_lattice(std::int32_t ix, std::int32_t iy, std::uint32_t seed) noexcept {
    const std::uint32_t h = cloud_hash(cloud_hash(static_cast<std::uint32_t>(ix) + seed * 0x9e3779b9u) ^ static_cast<std::uint32_t>(iy) * 0x85ebca6bu);
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}
inline float cloud_value_noise(float x, float y, std::uint32_t seed) noexcept {
    const float fx = std::floor(x), fy = std::floor(y);
    const auto ix = static_cast<std::int32_t>(fx), iy = static_cast<std::int32_t>(fy);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);  // quintic: smooth slopes
    const float sy = ty * ty * ty * (ty * (ty * 6.0f - 15.0f) + 10.0f);
    const float a = cloud_lattice(ix, iy, seed), b = cloud_lattice(ix + 1, iy, seed);
    const float c = cloud_lattice(ix, iy + 1, seed), d = cloud_lattice(ix + 1, iy + 1, seed);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}
} // namespace detail

// Five octaves of value noise, 0..1 (mean about 0.5). `footprint`: how much of the pattern one pixel covers
// (in units of the first octave's cells; 0 = all detail): octaves finer than that fade to their mean, so far
// clouds don't shimmer.
[[nodiscard]] inline float cloud_fbm(float x, float y, std::uint32_t seed, float footprint = 0.0f) noexcept {
    float sum = 0.0f, amplitude = 0.5f, total = 0.0f, cell = 1.0f;
    for (std::uint32_t o = 0; o < 5; ++o) {
        const float detail = std::clamp(1.5f - 2.0f * footprint / cell, 0.0f, 1.0f);  // 1 while the cell is well above a pixel
        sum += amplitude * (0.5f + (detail::cloud_value_noise(x, y, seed + o * 1013u) - 0.5f) * detail);
        cell *= 0.5f;
        total += amplitude;
        // Each octave twice as fine, turned and shifted so the grid doesn't show.
        const float nx = 1.6f * x + 1.2f * y + 17.3f, ny = -1.2f * x + 1.6f * y + 5.1f;
        x = nx;
        y = ny;
        amplitude *= 0.5f;
    }
    return sum / total;
}

// How cloudy the layer is (0..1) at a point of it, `x_km`, `z_km` (scene axes) after drifting by `offset`;
// `footprint_km`: the size of a pixel there (0: full detail).
[[nodiscard]] inline float cloud_cover_at(const CloudLayer& layer, float x_km, float z_km, math::Vec2 offset, float footprint_km = 0.0f) noexcept {
    if (!layer.enabled || layer.coverage <= 0.0f) return 0.0f;
    const float s = std::max(0.05f, layer.scale_km);
    const float n = cloud_fbm((x_km - offset.x) / s, (z_km - offset.y) / s, layer.seed, footprint_km / s);
    const float threshold = 1.05f + (-0.25f - 1.05f) * std::clamp(layer.coverage, 0.0f, 1.0f);  // 1.05 clear .. -0.25 overcast
    const float t = std::clamp((n - (threshold - 0.1f)) / 0.3f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The light that gets through a cloud of optical depth `tau` scattered (diffuse), the two-stream approximation for a
// thick cloud: 1 / (1 + 0.75 tau (1 - g)), with cloud droplets' g of 0.85. An overcast deck (tau 8) lets through about
// half, a storm cloud (tau 40) under a fifth: why thick clouds are dark underneath, yet overcast days stay bright.
[[nodiscard]] inline float cloud_diffuse_transmittance(float tau) noexcept { return 1.0f / (1.0f + 0.75f * std::max(0.0f, tau) * 0.15f); }

// The direct sunlight that gets through the layers to a point of the scene (metres, +Y up) for a sun
// towards `to_sun` (unit): exp(-optical depth) of the cloud each layer has where the sun ray crosses it.
// Below the horizon: 1 (the sun is off anyway).
[[nodiscard]] inline float cloud_sun_transmittance(const std::array<CloudLayer, kCloudLayers>& layers, math::Vec3 point, math::Vec3 to_sun,
                                                   float time) noexcept {
    if (to_sun.y <= 0.01f) return 1.0f;
    float tau = 0.0f;
    for (const CloudLayer& layer : layers) {
        if (!layer.enabled) continue;
        const float rise = layer.altitude_km * 1000.0f - point.y;
        if (rise <= 0.0f) continue;  // above the layer
        const float t = rise / to_sun.y;
        const math::Vec3 at = point + to_sun * t;
        tau += cloud_cover_at(layer, at.x * 0.001f, at.z * 0.001f, cloud_offset(layer, time)) * std::max(0.0f, layer.density);
    }
    return std::exp(-tau);
}

} // namespace noor::render
