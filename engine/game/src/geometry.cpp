#include "noor/game/geometry.h"

#include <algorithm>
#include <cmath>

namespace noor::game {

using math::Vec3;

// An axis-aligned box from lo to hi, one color, counter-clockwise faces.
Geometry make_box(Vec3 color, Vec3 lo, Vec3 hi) {
    struct Face { Vec3 n, u, v; };
    const Face faces[] = {{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                          {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
                          {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}};
    const Vec3 c = (lo + hi) * 0.5f, h = (hi - lo) * 0.5f;
    Geometry g;
    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(g.first.size());
        for (const Vec3 p : {f.n - f.u - f.v, f.n + f.u - f.v, f.n + f.u + f.v, f.n - f.u + f.v})
            g.first.push_back({{c.x + p.x * h.x, c.y + p.y * h.y, c.z + p.z * h.z}, f.n, color});
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) g.second.push_back(base + i);
    }
    return g;
}

// A sphere of radius 0.5 (primitives), or a capsule along Y (total height h, radius r) when h > 2r.
Geometry make_round(float radius, float height) {
    constexpr int kRings = 16, kSegments = 24;
    const float half = std::max(0.0f, height * 0.5f - radius);  // cylinder half length
    Geometry g;
    // Rings from the top pole to the bottom pole; the upper hemisphere is shifted up by `half`.
    for (int i = 0; i <= kRings; ++i) {
        const float v = static_cast<float>(i) / kRings;
        const float phi = v * math::kPi;
        const float y = std::cos(phi) * radius;
        const float r = std::sin(phi) * radius;
        const float shift = i <= kRings / 2 ? half : -half;
        for (int j = 0; j <= kSegments; ++j) {
            const float theta = 2.0f * math::kPi * static_cast<float>(j) / kSegments;
            const Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            g.first.push_back({{r * std::cos(theta), y + shift, r * std::sin(theta)}, n, {1, 1, 1}});
        }
        if (i == kRings / 2 && half > 0) {  // duplicate the equator ring for the lower hemisphere
            for (int j = 0; j <= kSegments; ++j) {
                const float theta = 2.0f * math::kPi * static_cast<float>(j) / kSegments;
                g.first.push_back({{radius * std::cos(theta), -half, radius * std::sin(theta)}, {std::cos(theta), 0, std::sin(theta)}, {1, 1, 1}});
            }
        }
    }
    const int rows = static_cast<int>(g.first.size()) / (kSegments + 1);
    for (int i = 0; i + 1 < rows; ++i) {
        for (int j = 0; j < kSegments; ++j) {
            const auto a = static_cast<std::uint32_t>(i * (kSegments + 1) + j);
            const auto b = a + kSegments + 1;
            for (const std::uint32_t k : {a, a + 1, b, b, a + 1, b + 1}) g.second.push_back(k);
        }
    }
    return g;
}

} // namespace noor::game
