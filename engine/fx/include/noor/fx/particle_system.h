#pragma once
// Effect particles in a running game (Milestone 8B, ADR-035).
//
// EmitterSet: emitters by entity, stepped from their (resolved) settings and where their entities are. The runtime
// system below (games, the editor's Play) and the editor's edit-mode preview (part 4) both use it.
//
// ParticleSystem: the runtime system that gives every entity with an `emitter` component its Emitter and steps them at
// the World's fixed 60 Hz, from where their entities are, in the scene's wind (the environment's "wind_speed", m/s, and
// "wind_direction", degrees: where it blows towards, 0 = -Z, 90 = +X, like the clouds'). Settings come from the
// component, or a `.noorfx` asset it points at with overrides. Each step looks at the scene's emitter components, so
// one added, changed or removed while running (a script, the editor) takes effect on the next step.

#include "noor/fx/particles.h"
#include "noor/runtime/world.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>

namespace noor::fx {

// A stable salt from an entity's id (its text: the same on every machine and build), so two identical emitters differ.
[[nodiscard]] std::uint64_t emitter_salt(const Guid& entity);

class EmitterSet {
public:
    struct Input {
        Guid entity;
        const asset::Json* settings = nullptr;  // resolved (asset and overrides merged); a change rebuilds the settings
        math::Mat4 world = math::Mat4::identity();
    };
    // One fixed step: emitters for new inputs are made, changed settings taken, missing entities' emitters dropped.
    void step(std::span<const Input> inputs, float dt, math::Vec3 wind);
    void restart(const Guid& entity);  // a fresh emitter (time 0, no particles) on the next step
    void clear();

    [[nodiscard]] const std::unordered_map<Guid, Emitter>& emitters() const noexcept { return emitters_; }
    [[nodiscard]] Emitter* find(const Guid& entity);

    struct Stats {
        std::size_t emitters = 0, particles = 0;
        std::uint64_t fingerprint = 0;  // every emitter's particles, in entity id order: equal runs, equal fingerprints
    };
    [[nodiscard]] Stats stats() const;

private:
    std::unordered_map<Guid, Emitter> emitters_;
    std::unordered_map<Guid, asset::Json> settings_;  // what each was built from
};

class ParticleSystem final : public runtime::System {
public:
    struct Options {
        // An asset id to its file (effect assets, environment assets for the wind); empty: inline settings only.
        std::function<std::optional<std::filesystem::path>(const Guid&)> resolve_asset;
    };
    explicit ParticleSystem(Options options = {});

    [[nodiscard]] std::string_view name() const override { return "Particles"; }
    void fixed_update(runtime::World& world, double fixed_dt) override;
    void entity_destroyed(runtime::World& world, const scene::Entity& entity) override;
    void shutdown(runtime::World& world) override;

    // The emitters, by entity (drawing; scripts, part 6).
    [[nodiscard]] const EmitterSet& set() const noexcept { return set_; }
    [[nodiscard]] const std::unordered_map<Guid, Emitter>& emitters() const noexcept { return set_.emitters(); }
    [[nodiscard]] Emitter* find(const Guid& entity) { return set_.find(entity); }

    struct Stats {
        std::size_t emitters = 0, particles = 0;
        std::uint64_t fingerprint = 0;
        double step_ms = 0;  // the last frame's fixed steps (CPU)
    };
    [[nodiscard]] Stats stats() const;

private:
    [[nodiscard]] math::Vec3 wind(runtime::World& world) const;

    Options options_;
    EmitterSet set_;
    struct Resolved {
        asset::Json component;  // the component it was resolved from
        asset::Json settings;   // the asset's settings with the component's on top
    };
    std::unordered_map<Guid, Resolved> resolved_;
    double step_ms_ = 0;
    std::uint64_t timed_frame_ = ~0ull;  // the frame step_ms_ is adding up
};

// An emitter component's settings: its .noorfx asset's (found by `resolve`), then the component's own fields on top.
[[nodiscard]] asset::Json resolve_emitter(const asset::Json& component,
                                          const std::function<std::optional<std::filesystem::path>(const Guid&)>& resolve);

} // namespace noor::fx
