#pragma once
// The star field (Milestone 8A, part 5, ADR-033): the reference for shaders/stars.hlsli (keep them in step).
//
// Deterministic from a seed: the celestial sphere is cut into cells (six cube faces of kStarGrid x kStarGrid);
// a cell holds at most one star, at a jittered spot, with a magnitude drawn so the whole sky matches the real
// counts down to the naked-eye limit: N(brighter than m) = 15 * 10^(0.5 (m - 1)), about 15 stars brighter than
// magnitude 1, 4,800 than 6, 8,400 than 6.5. Brightness: a magnitude-0 star gives 2.08e-6 lux. The sky turns about
// the celestial pole (at the latitude's height above the north horizon) 15 degrees an hour.

#include "noor/foundation/math.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace noor::render {

inline constexpr std::uint32_t kStarGrid = 256;    // cells per cube face edge
inline constexpr float kStarLimit = 6.5f;          // the faintest magnitude drawn
inline constexpr float kBrightestStar = -1.5f;     // no star brighter (Sirius is -1.46)

// How many stars on the whole sky are brighter than magnitude `m` (the model above).
[[nodiscard]] inline float star_count_brighter(float m) noexcept { return 15.0f * std::pow(10.0f, 0.5f * (m - 1.0f)); }

// The illuminance (lux) a star of magnitude `m` gives outside the air.
[[nodiscard]] inline float star_illuminance(float m) noexcept { return 2.08e-6f * std::pow(10.0f, -0.4f * m); }

namespace detail {
inline std::uint32_t star_hash(std::uint32_t x) noexcept {
    x ^= x >> 17;
    x *= 0xed5ad4bbu;
    x ^= x >> 11;
    x *= 0xac4c1b51u;
    x ^= x >> 15;
    x *= 0x31848babu;
    x ^= x >> 14;
    return x;
}
inline float star_unit(std::uint32_t h) noexcept { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

// A cube face's point (u, v in -1..1) as a direction (Vulkan's face order, like asset::cube_direction).
inline math::Vec3 star_face_direction(std::uint32_t face, float u, float v) noexcept {
    math::Vec3 d;
    switch (face) {
    case 0: d = {1, -v, -u}; break;
    case 1: d = {-1, -v, u}; break;
    case 2: d = {u, 1, v}; break;
    case 3: d = {u, -1, -v}; break;
    case 4: d = {u, -v, 1}; break;
    default: d = {-u, -v, -1}; break;
    }
    return math::normalize(d);
}
} // namespace detail

struct Star {
    math::Vec3 direction;  // in the star field's own frame (see star_frame)
    float magnitude;
    float temperature;     // kelvin (3,000 red .. 12,000 blue-white): its colour
};

// The star in a cell, if there is one.
[[nodiscard]] inline std::optional<Star> star_in_cell(std::uint32_t face, std::uint32_t ix, std::uint32_t iy, std::uint32_t seed) noexcept {
    const std::uint32_t cell = (face * kStarGrid + iy) * kStarGrid + ix;
    const std::uint32_t h0 = detail::star_hash(cell * 4u + seed * 0x9e3779b9u);
    const float total = star_count_brighter(kStarLimit);
    const float p = total / static_cast<float>(6u * kStarGrid * kStarGrid);  // chance a cell holds a star
    const float r0 = detail::star_unit(h0);
    if (r0 >= p) return std::nullopt;
    // Re-use r0 inside [0, p) as the star's rank among all stars: an even spread over the counts.
    const float rank = (r0 / p) * total;
    const float magnitude = std::max(kBrightestStar, 1.0f + 2.0f * std::log10(std::max(rank, 1e-3f) / 15.0f));
    const std::uint32_t h1 = detail::star_hash(h0 ^ 0x68e31da4u), h2 = detail::star_hash(h1 ^ 0xb5297a4du);
    const float u = ((static_cast<float>(ix) + 0.15f + 0.7f * detail::star_unit(h1)) / kStarGrid) * 2.0f - 1.0f;
    const float v = ((static_cast<float>(iy) + 0.15f + 0.7f * detail::star_unit(h2)) / kStarGrid) * 2.0f - 1.0f;
    const float temperature = 3000.0f + 9000.0f * std::pow(detail::star_unit(detail::star_hash(h2 ^ 0x1b873593u)), 1.5f);
    return Star{detail::star_face_direction(face, u, v), magnitude, temperature};
}

// From the scene's axes (+Y up, north -Z, east +X) into the star field's frame: tilt so the celestial pole (at
// `latitude_deg` above the north horizon) is the frame's +Y, then turn with the sky (15 degrees an hour of
// `sidereal_hours`). Stars therefore circle the pole as the night goes on.
[[nodiscard]] inline math::Vec3 star_frame(math::Vec3 world, float latitude_deg, float sidereal_hours) noexcept {
    const float colat = math::radians(90.0f - latitude_deg);
    // Tilt about +X (east) by the colatitude: the pole (0, sin lat, -cos lat) goes to +Y.
    const float c = std::cos(colat), s = std::sin(colat);
    const math::Vec3 t{world.x, world.y * c - world.z * s, world.y * s + world.z * c};
    // Turn about the pole (+Y) with the sky.
    const float a = math::radians(15.0f * sidereal_hours), ca = std::cos(a), sa = std::sin(a);
    return {t.x * ca + t.z * sa, t.y, -t.x * sa + t.z * ca};
}

} // namespace noor::render
