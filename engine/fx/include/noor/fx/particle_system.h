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
// The id a sub-emitter's particles are kept under: derived from its parent's entity and its index (part 7).
[[nodiscard]] Guid sub_emitter_id(const Guid& parent, std::size_t index);

class EmitterSet {
public:
    struct Input {
        Guid entity;
        const asset::Json* settings = nullptr;  // resolved (asset and overrides merged); a change rebuilds the settings
        math::Mat4 world = math::Mat4::identity();
    };
    // One fixed step: emitters for new inputs are made, changed settings taken, missing entities' emitters dropped.
    // Each emitter's sub-emitters (part 7) step right after it and burst where its particles hit or died. `ground`:
    // what particles that collide stand on; `budget`: the most particles the whole set may hold (handed out in the
    // inputs' order, so the same scene throttles the same way every run).
    void step(std::span<const Input> inputs, float dt, math::Vec3 wind, const GroundQuery* ground = nullptr,
              std::uint32_t budget = 0xffffffffu);
    void restart(const Guid& entity);  // a fresh emitter (time 0, no particles) on the next step
    void clear();

    [[nodiscard]] const std::unordered_map<Guid, Emitter>& emitters() const noexcept { return emitters_; }
    [[nodiscard]] Emitter* find(const Guid& entity);
    // The entity an emitter belongs to: itself, or for a sub-emitter its parent's entity (solo, mute, selection).
    [[nodiscard]] Guid owner(const Guid& id) const;

    struct Stats {
        std::size_t emitters = 0, particles = 0;
        std::uint64_t fingerprint = 0;  // every emitter's particles, in entity id order: equal runs, equal fingerprints
        std::uint64_t hits = 0;         // collision events so far (part 7)
        std::uint64_t throttled = 0;    // particles the budget held back so far
    };
    [[nodiscard]] Stats stats() const;

private:
    Emitter& ensure(const Guid& id, const asset::Json& settings, bool sub);
    std::unordered_map<Guid, Emitter> emitters_;
    std::unordered_map<Guid, asset::Json> settings_;             // what each was built from
    std::unordered_map<Guid, std::vector<Guid>> children_;       // an entity's sub-emitters
};

class ParticleSystem final : public runtime::System {
public:
    struct Options {
        // An asset id to its file (effect assets, environment assets for the wind); empty: inline settings only.
        std::function<std::optional<std::filesystem::path>(const Guid&)> resolve_asset;
        // The ground colliding particles stand on, for this step (the game gives the scene's terrains); empty: none.
        std::function<GroundQuery(runtime::World&)> ground;
        // The most effect particles the scene may have at once (part 7): spawning stops there until some die.
        std::uint32_t max_particles = 100000;
    };
    explicit ParticleSystem(Options options = {});

    [[nodiscard]] std::string_view name() const override { return "Particles"; }
    // Registers "emitter.burst" ({"count": n}): n particles from that entity's emitter at the next fixed step.
    void attached(runtime::World& world) override;
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
        std::uint64_t hits = 0, throttled = 0;
    };
    [[nodiscard]] Stats stats() const;

private:
    [[nodiscard]] math::Vec3 wind(runtime::World& world) const;
    std::vector<std::pair<Guid, int>> bursts_;  // from scripts, for the next fixed step

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
