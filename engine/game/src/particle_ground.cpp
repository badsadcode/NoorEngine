#include "noor/game/particle_ground.h"

#include <cmath>

namespace noor::game {

std::optional<fx::GroundHit> ParticleGround::at(float x, float z) const {
    std::optional<fx::GroundHit> best;
    for (const Piece& p : pieces_) {
        if (!p.heights) continue;
        const terrain::Heights& h = *p.heights;
        const float lx = x - p.position.x, lz = z - p.position.z;
        const math::Vec2 o = h.origin();
        if (lx < o.x || lz < o.y || lx > -o.x || lz > -o.y) continue;  // off this terrain (it's centred)
        const float y = p.position.y + h.height(lx, lz);
        if (best && best->height >= y) continue;
        // The normal from the surface itself, half a cell either way (smooth over the triangles' edges).
        const float e = 0.5f * h.cell_size();
        const float dx = h.height(lx + e, lz) - h.height(lx - e, lz), dz = h.height(lx, lz + e) - h.height(lx, lz - e);
        math::Vec3 n{-dx, 2.0f * e, -dz};
        n = math::normalize(n);
        best = fx::GroundHit{y, n};
    }
    return best;
}

fx::GroundQuery ParticleGround::query() const {
    if (pieces_.empty()) return {};
    return [ground = *this](float x, float z) { return ground.at(x, z); };
}

} // namespace noor::game
