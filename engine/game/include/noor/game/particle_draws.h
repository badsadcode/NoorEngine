#pragma once
// Effect particles to the renderer (Milestone 8B, parts 2 and 4, ADR-035): one render::ParticleDraw per emitter, from
// its `render` settings (blend, facing, lit, emission, stretch, texture and flipbook), its particles in world space with
// their size, colour and alpha from their life's curves, alpha-blended ones sorted back to front for `eye`. Emitters in
// entity id order, so a frame doesn't depend on a hash map's order. Games (SceneRenderer::particles) and the editor's
// preview (the viewport) both use it.

#include "noor/fx/particle_system.h"
#include "noor/render/renderer.h"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace noor::game {

struct ParticleDrawStorage {
    std::vector<std::vector<render::ParticleVertex>> vertices;  // per draw
    std::vector<render::ParticleDraw> draws;
};

struct ParticleDrawSources {
    std::function<std::optional<math::Mat4>(const Guid&)> world_of;   // local-space emitters: where their entity is now
    std::function<render::TextureHandle(const Guid&)> texture;        // a texture asset id to its GPU texture
    std::function<bool(const Guid&)> shown;                           // empty: every emitter (the editor: solo and mute)
};

// The spans stay valid until `storage` is used again.
[[nodiscard]] std::span<const render::ParticleDraw> particle_draws(const fx::EmitterSet& set, math::Vec3 eye, const ParticleDrawSources& sources,
                                                                   ParticleDrawStorage& storage);

} // namespace noor::game
