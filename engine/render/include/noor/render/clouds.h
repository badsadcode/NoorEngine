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
    // Part 7b (ADR-034): a volume instead of a thin layer: the coverage map extruded into a slab this thick, from
    // `altitude_km` up. Off: the flat layer, exactly as before.
    bool volume = false;
    float thickness_km = 1.5f;
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

// ---- Volumetric layers (Milestone 8A, part 7b, ADR-034). The reference for clouds.hlsli's cloud_volume_*. ----

namespace detail {
inline float cloud_lattice3(std::int32_t ix, std::int32_t iy, std::int32_t iz, std::uint32_t seed) noexcept {
    const std::uint32_t h = cloud_hash(cloud_hash(cloud_hash(static_cast<std::uint32_t>(ix) + seed * 0x9e3779b9u) ^ static_cast<std::uint32_t>(iy) * 0x85ebca6bu) ^
                                       static_cast<std::uint32_t>(iz) * 0xc2b2ae35u);
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}
inline float cloud_quintic(float t) noexcept { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline std::int32_t cloud_wrap(std::int32_t i, std::int32_t period) noexcept {
    const std::int32_t m = i % period;
    return m < 0 ? m + period : m;
}
// Value noise whose lattice repeats every `period` cells (so it tiles: the GPU bakes it into a texture).
inline float cloud_value_noise3(float x, float y, float z, std::int32_t period, std::uint32_t seed) noexcept {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const auto ix = static_cast<std::int32_t>(fx), iy = static_cast<std::int32_t>(fy), iz = static_cast<std::int32_t>(fz);
    const float sx = cloud_quintic(x - fx), sy = cloud_quintic(y - fy), sz = cloud_quintic(z - fz);
    const auto at = [&](std::int32_t i, std::int32_t j, std::int32_t k) {
        return cloud_lattice3(cloud_wrap(i, period), cloud_wrap(j, period), cloud_wrap(k, period), seed);
    };
    float plane[2];
    for (int k = 0; k < 2; ++k) {
        const float a = at(ix, iy, iz + k), b = at(ix + 1, iy, iz + k);
        const float c = at(ix, iy + 1, iz + k), d = at(ix + 1, iy + 1, iz + k);
        const float top = a + (b - a) * sx;
        plane[k] = top + ((c + (d - c) * sx) - top) * sy;
    }
    return plane[0] + (plane[1] - plane[0]) * sz;
}
inline float cloud_smooth01(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace detail

// The look, in one place (clouds.hlsli has the same numbers; chosen from rendered variants, "B", 2026-11-19): how hard
// the 3D noise erodes edges and how fine it is (a fraction of the layer's scale); where the dome starts rounding off
// (a fraction of the cloud's top); the forward lobe's eccentricity (the silver lining); the weights of direct sun,
// light diffused through the cloud, and the sky's light at the base (the top gets all of it). Darker bellies and a
// brighter lining than the first version: clouds read as solid.
inline constexpr float kVolumeErosion = 0.55f;
inline constexpr float kVolumeDetailScale = 0.12f;
inline constexpr float kVolumeDome = 0.55f;
inline constexpr float kVolumeSilver = 0.75f;
inline constexpr float kVolumeDirect = 0.35f;
inline constexpr float kVolumeDiffuse = 0.06f;
inline constexpr float kVolumeAmbientBase = 0.3f;
// Puffs (round two): how hard round lumps carve the cloud (0: none) and how big they are (a fraction of the layer's
// scale). Cellular noise, inverted: high at each cell's centre, so what's left is lumps on lumps. Chosen from
// rendered variants ("P4, cauliflower", 2026-11-19).
inline constexpr float kVolumePuff = 0.85f;
inline constexpr float kVolumePuffScale = 0.28f;
// Edges (round three, "F3", 2026-11-19; finer erosion above too): how far into the cloud the density keeps fading in (0: off). Low densities are weakened
// further, so every edge has a thin, see-through fringe whose width follows the noise, instead of the same soft band
// everywhere. And how much the erosion turns wispy (inverted noise: strands) towards the base, billowy above.
inline constexpr float kVolumeEdge = 0.35f;
inline constexpr float kVolumeWispy = 1.0f;
// Where the lumps and the erosion fade out with distance, as a fraction of their first-octave cell per pixel: they
// fade only once a pixel covers this much of a cell (so the shapes survive at the distances clouds are seen from).
inline constexpr float kVolumeDetailFade = 1.0f;

// The volume noises repeat every kCloudNoisePeriod units (a unit is the first octave's cell), so the GPU can bake
// them once into a 3D texture (shaders/clouds_volume.hlsl, CSNoise) and read them instead of computing them; a
// layer's seed picks where in the tile it reads (cloud_noise_offset).
inline constexpr std::int32_t kCloudNoisePeriod = 4;

// Three octaves of 3D value noise, 0..1: what erodes a volume's edges. Tiles every kCloudNoisePeriod.
[[nodiscard]] inline float cloud_detail(float x, float y, float z) noexcept {
    float sum = 0.0f, amplitude = 0.5f, total = 0.0f;
    for (std::int32_t o = 0; o < 3; ++o) {
        sum += amplitude * detail::cloud_value_noise3(x, y, z, kCloudNoisePeriod << o, 7919u + static_cast<std::uint32_t>(o) * 2029u);
        total += amplitude;
        x = 2.0f * x + 11.7f, y = 2.0f * y + 7.3f, z = 2.0f * z + 3.9f;  // twice as fine, shifted (the period doubles too)
        amplitude *= 0.5f;
    }
    return sum / total;
}

// Where in the tiles a layer reads its noise, from its seed: different seeds, different puffs.
[[nodiscard]] inline math::Vec3 cloud_noise_offset(std::uint32_t seed) noexcept {
    const auto p = static_cast<float>(kCloudNoisePeriod);
    return {detail::cloud_lattice3(static_cast<std::int32_t>(seed), 1, 2, 0x51ed27u) * p,
            detail::cloud_lattice3(static_cast<std::int32_t>(seed), 3, 4, 0x51ed27u) * p,
            detail::cloud_lattice3(static_cast<std::int32_t>(seed), 5, 6, 0x51ed27u) * p};
}

namespace detail {
// Distance to the nearest of one random point per unit cell (Worley's cellular noise, F1), 0..about 1; the points
// repeat every `period` cells.
inline float cloud_worley3(float x, float y, float z, std::int32_t period, std::uint32_t seed) noexcept {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const auto ix = static_cast<std::int32_t>(fx), iy = static_cast<std::int32_t>(fy), iz = static_cast<std::int32_t>(fz);
    float best = 9.0f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const std::int32_t cx = ix + dx, cy = iy + dy, cz = iz + dz;
                const std::int32_t wx = cloud_wrap(cx, period), wy = cloud_wrap(cy, period), wz = cloud_wrap(cz, period);
                const float px = static_cast<float>(cx) + cloud_lattice3(wx, wy, wz, seed) - x;
                const float py = static_cast<float>(cy) + cloud_lattice3(wx, wy, wz, seed + 1u) - y;
                const float pz = static_cast<float>(cz) + cloud_lattice3(wx, wy, wz, seed + 2u) - z;
                best = std::min(best, px * px + py * py + pz * pz);
            }
    return std::sqrt(best);
}
} // namespace detail

// Billows, 0..1: three octaves of inverted cellular noise (1 at a cell's centre, falling to 0 between cells), larger
// lumps carrying smaller ones. Tiles every kCloudNoisePeriod.
[[nodiscard]] inline float cloud_billow(float x, float y, float z) noexcept {
    float sum = 0.0f;
    constexpr float kWeights[3] = {0.625f, 0.25f, 0.125f};
    for (std::int32_t o = 0; o < 3; ++o) {
        sum += kWeights[o] * std::clamp(1.0f - detail::cloud_worley3(x, y, z, kCloudNoisePeriod << o, 4111u + static_cast<std::uint32_t>(o) * 3301u), 0.0f, 1.0f);
        x = 2.0f * x + 5.3f, y = 2.0f * y + 1.7f, z = 2.0f * z + 9.1f;
    }
    return sum;
}

// The shape across a volume's height: `h` 0 at its base, 1 at its top; `cover` the coverage map there. Flat bases (a
// quick rise over the bottom 8%) and rounded tops, and cloudier places grow taller (a top at 30% of the slab for a
// wisp, the whole slab for a full cloud).
[[nodiscard]] inline float cloud_height_profile(float h, float cover) noexcept {
    const float top = 0.3f + 0.7f * std::clamp(cover, 0.0f, 1.0f);
    if (h <= 0.0f || h >= top) return 0.0f;
    const float base = detail::cloud_smooth01(h / 0.08f);
    const float t = std::clamp((h - kVolumeDome * top) / ((1.0f - kVolumeDome) * top), 0.0f, 1.0f);
    return base * (1.0f - t * t);
}

// The mean of cloud_height_profile over a full cloud's column (cover 1): about 0.81 (0.04 for the base's rise, 0.47
// flat, 0.30 for the dome). Extinction is divided by it, so a column through a full cloud has the layer's `density`
// as its optical thickness, which is what cloud shadows and the exposure meter assume for both styles.
inline constexpr float kCloudColumnMean = 0.81f;

// How much cloud a volume has (0..1) at a point: `x_km`, `z_km` along the ground, `height_km` above it; `offset` the
// wind's drift; `footprint_km` a pixel's size there (far clouds lose the fine octaves and the erosion, as flat
// layers lose detail, so the horizon doesn't sparkle). `detail` false skips the erosion: the march towards the sun
// only needs how much cloud is in the way, not its puffs.
[[nodiscard]] inline float cloud_volume_density(const CloudLayer& layer, float x_km, float height_km, float z_km, math::Vec2 offset,
                                                float footprint_km = 0.0f, bool detail = true) noexcept {
    if (!layer.enabled || layer.coverage <= 0.0f) return 0.0f;
    const float thickness = std::max(0.05f, layer.thickness_km);
    const float h = (height_km - layer.altitude_km) / thickness;
    if (h <= 0.0f || h >= 1.0f) return 0.0f;
    const float cover = cloud_cover_at(layer, x_km, z_km, offset, footprint_km);
    float base = cover * cloud_height_profile(h, cover);
    if (base <= 0.0f) return 0.0f;
    // Puffs: the gaps between lumps are carved away (in the march towards the sun too, so each lump shades the next).
    const float ps = std::max(0.05f, layer.scale_km) * kVolumePuffScale;
    const float keep_puffs = std::clamp(1.5f - 2.0f * footprint_km / (kVolumeDetailFade * ps), 0.0f, 1.0f);
    if (kVolumePuff > 0.0f && keep_puffs > 0.0f) {
        const math::Vec3 o = cloud_noise_offset(layer.seed);
        const float carve = kVolumePuff * keep_puffs * (1.0f - cloud_billow((x_km - offset.x) / ps + o.x, height_km / ps + o.y, (z_km - offset.y) / ps + o.z));
        base = std::clamp((base - carve) / std::max(1e-3f, 1.0f - carve), 0.0f, 1.0f);
        if (base <= 0.0f) return 0.0f;
    }
    // Erosion: noise eats into the thin parts and leaves the cores, so edges break into puffs.
    const float s = std::max(0.05f, layer.scale_km) * kVolumeDetailScale;
    const float keep = detail ? std::clamp(1.5f - 2.0f * footprint_km / (kVolumeDetailFade * s), 0.0f, 1.0f) : 0.0f;
    const auto edge = [](float d) { return kVolumeEdge > 0.0f ? d * std::clamp(d / kVolumeEdge, 0.0f, 1.0f) : d; };
    if (keep <= 0.0f) return edge(base);
    const math::Vec3 o = cloud_noise_offset(layer.seed + 1u);
    const float n = cloud_detail((x_km - offset.x) / s + o.x, height_km / s + o.y, (z_km - offset.y) / s + o.z);
    const float billowy = std::clamp(h * 4.0f, 0.0f, 1.0f) * kVolumeWispy + (1.0f - kVolumeWispy);  // 0 at the base: wispy
    const float erosion = kVolumeErosion * keep * ((1.0f - n) + (n - (1.0f - n)) * billowy);
    return edge(std::clamp((base - erosion) / (1.0f - erosion), 0.0f, 1.0f));
}

// The extinction (per km) of a volume where its density is `d`.
[[nodiscard]] inline float cloud_volume_extinction(const CloudLayer& layer, float d) noexcept {
    return d * std::max(0.0f, layer.density) / (std::max(0.05f, layer.thickness_km) * kCloudColumnMean);
}

// The Henyey-Greenstein phase function (per steradian; 1 / 4 pi when g is 0).
[[nodiscard]] inline float cloud_hg(float cos_angle, float g) noexcept {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * 3.14159265f * std::pow(std::max(1e-6f, 1.0f + g2 - 2.0f * g * cos_angle), 1.5f));
}

struct CloudVolumeSample {
    math::Vec3 radiance{0, 0, 0};  // relative to a sun of illuminance 1, like `sun` and `ambient`
    float transmittance = 1.0f;    // of the background behind the cloud
};

// A volume seen along unit `view` from `eye_km` (x and z along the ground in km, y the height above it in km), over a
// planet of radius `planet_km`. `sun`: the sunlight arriving at the layer (relative), `ambient`: the sky's light there.
// Up to `steps` samples along the view (one per sixteenth of the slab's thickness, at least 24: a look straight up
// needs few, a look along the horizon many), each placed at `jitter` 0..1 within its step (a fixed per-pixel pattern
// on the GPU); `light_steps` towards the sun. Seen from below or inside the slab; from above, nothing yet. Far cloud
// dissolves into the haze sample by sample (kCloudHazeKm), so the horizon doesn't turn into noise.
//
// Light: single scattering of the sun through the cloud between the sample and the sun, plus multiple scattering by
// Wrenninge's approximation (octaves with half the extinction, half the contribution and half the phase's
// eccentricity each), plus the light diffused through the cloud between the sample and the sun (the two-stream term
// flat layers use: why bellies are grey, not black), a little from the sunlit ground, and the sky's light, more at
// the top than at the base. Scattering albedo 1, so each step adds S * (1 - e^(-sigma dt)) and lets e^(-sigma dt)
// through.
inline constexpr float kCloudHazeKm = 50.0f;  // a volume's extinction fades as e^(-distance / this)
inline constexpr float kCloudMarchKm = 30.0f;  // the most of a slab one look crosses

[[nodiscard]] inline CloudVolumeSample cloud_volume_march(const CloudLayer& layer, math::Vec3 eye_km, float planet_km, math::Vec3 view,
                                                          math::Vec3 to_sun, math::Vec3 sun, math::Vec3 ambient, math::Vec2 offset,
                                                          int steps = 64, int light_steps = 4, float jitter = 0.5f,
                                                          float pixel_angle = 0.0f) noexcept {
    CloudVolumeSample out;
    if (!layer.enabled || !layer.volume || layer.coverage <= 0.0f || layer.density <= 0.0f) return out;
    const float thickness = std::max(0.05f, layer.thickness_km);
    const float r = planet_km + std::max(0.0f, eye_km.y);
    const float inner = planet_km + layer.altitude_km, outer = inner + thickness;
    if (r >= outer) return out;  // above the volume: not drawn from above (yet)
    // Where the view enters and leaves the slab (sphere roots from radius r, cosine view.y with the local up).
    const auto far_root = [&](float radius) {
        const float disc = r * r * (view.y * view.y - 1.0f) + radius * radius;
        return disc < 0.0f ? -1.0f : -r * view.y + std::sqrt(disc);
    };
    float t0 = 0.0f, t1 = far_root(outer);
    if (r < inner) {
        t0 = far_root(inner);
    } else {  // inside: a downward look may leave through the base
        const float disc = r * r * (view.y * view.y - 1.0f) + inner * inner;
        if (view.y < 0.0f && disc >= 0.0f) t1 = -r * view.y - std::sqrt(disc);
    }
    if (t1 <= t0 || t1 <= 0.0f) return out;
    t1 = std::min(t1, t0 + kCloudMarchKm);
    const int n = std::clamp(static_cast<int>(std::ceil((t1 - t0) / (thickness / 16.0f))), std::min(24, std::max(1, steps)), std::max(1, steps));
    const float dt = (t1 - t0) / static_cast<float>(n);
    const float c = math::dot(view, to_sun);
    // A pixel's footprint per km of distance: like a flat layer's, it stretches at grazing angles (a pixel near the
    // horizon spans kilometres of cloud along the view), so the fine octaves fade there instead of sparkling.
    const float spread = pixel_angle / std::max(0.05f, std::abs(view.y) + 0.02f);
    float transmittance = 1.0f;
    math::Vec3 radiance{0, 0, 0};
    for (int i = 0; i < n && transmittance > 0.01f; ++i) {
        const float t = t0 + (static_cast<float>(i) + jitter) * dt;
        const float x = eye_km.x + view.x * t, z = eye_km.z + view.z * t;
        const float height = std::sqrt(std::max(0.0f, t * t + r * r + 2.0f * r * t * view.y)) - planet_km;
        const float d = cloud_volume_density(layer, x, height, z, offset, t * spread);
        if (d <= 0.0f) continue;
        const float sigma = cloud_volume_extinction(layer, d) * std::exp(-t / kCloudHazeKm);
        // The cloud between here and the sun: a short march up to the slab's top (or 3 thicknesses for a low sun).
        float tau_sun = 0.0f;
        if (to_sun.y > -0.2f) {
            const float to_top = (layer.altitude_km + thickness - height) / std::max(0.1f, to_sun.y + 0.05f);
            const float ds = std::min(to_top, 3.0f * thickness) / static_cast<float>(std::max(1, light_steps));
            for (int j = 0; j < light_steps; ++j) {
                const float s = (static_cast<float>(j) + 0.5f) * ds;
                const float lh = height + to_sun.y * s;
                tau_sun += cloud_volume_extinction(layer, cloud_volume_density(layer, x + to_sun.x * s, lh, z + to_sun.z * s, offset, t * spread, false)) * ds;
            }
        }
        float direct = 0.0f, a = 1.0f, b = 1.0f, k = 1.0f;
        for (int o = 0; o < 3; ++o) {  // Wrenninge's octaves
            const float phase = 0.7f * cloud_hg(c, kVolumeSilver * k) + 0.3f * cloud_hg(c, -0.3f * k);
            direct += a * phase * std::exp(-b * tau_sun);
            a *= 0.5f, b *= 0.5f, k *= 0.5f;
        }
        const float hf = std::clamp((height - layer.altitude_km) / thickness, 0.0f, 1.0f);
        const math::Vec3 light = sun * (direct * 4.0f * 3.14159265f * kVolumeDirect + kVolumeDiffuse * cloud_diffuse_transmittance(tau_sun) + 0.01f) +
                                 ambient * (kVolumeAmbientBase + (1.0f - kVolumeAmbientBase) * hf);
        const float step_t = std::exp(-sigma * dt);
        radiance = radiance + light * (transmittance * (1.0f - step_t));
        transmittance *= step_t;
    }
    out.radiance = radiance;
    out.transmittance = transmittance;
    return out;
}

} // namespace noor::render
