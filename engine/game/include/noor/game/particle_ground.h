#pragma once
// The ground effect particles collide with (Milestone 8B, part 7): the scene's terrains, as the same surface the
// terrain is drawn and collided with (terrain::Heights::height splits each cell like the mesh and physics do). Games
// (game::make_world) and the editor's effect preview both build one.

#include "noor/fx/particles.h"
#include "noor/terrain/terrain.h"

#include <memory>
#include <vector>

namespace noor::game {

class ParticleGround {
public:
    struct Piece {
        math::Vec3 position;                             // the terrain's centre (rotation and scale are ignored, as drawn)
        std::shared_ptr<const terrain::Heights> heights;
    };
    void set(std::vector<Piece> pieces) { pieces_ = std::move(pieces); }
    [[nodiscard]] bool empty() const noexcept { return pieces_.empty(); }
    // The highest terrain over (x, z), with its normal there; nullopt off every terrain.
    [[nodiscard]] std::optional<fx::GroundHit> at(float x, float z) const;
    // A query that keeps these pieces alive (a copy of the list; the heights are shared).
    [[nodiscard]] fx::GroundQuery query() const;

private:
    std::vector<Piece> pieces_;
};

} // namespace noor::game
