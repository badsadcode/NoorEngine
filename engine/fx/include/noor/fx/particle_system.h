#pragma once
// Effect particles in a running game (Milestone 8B, part 1, ADR-035): the runtime system that gives every entity with
// an `emitter` component its Emitter, and steps them all at the World's fixed 60 Hz, from where their entities are,
// in the scene's wind (the environment's "wind_speed", m/s, and "wind_direction", degrees: where it blows towards,
// 0 = -Z, 90 = +X, like the clouds'). Settings come from the component, or a `.noorfx` asset it points at with
// overrides. Each step looks at the scene's emitter components, so one added, changed or removed while running (a
// script, the editor) takes effect on the next step.

#include "noor/fx/particles.h"
#include "noor/runtime/world.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

namespace noor::fx {

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

    // The emitters, by entity (drawing, part 2; scripts, part 6).
    [[nodiscard]] const std::unordered_map<Guid, Emitter>& emitters() const noexcept { return emitters_; }
    [[nodiscard]] Emitter* find(const Guid& entity);

    struct Stats {
        std::size_t emitters = 0, particles = 0;
        std::uint64_t fingerprint = 0;  // every emitter's particles, in entity id order: equal runs, equal fingerprints
        double step_ms = 0;             // the last frame's fixed steps (CPU)
    };
    [[nodiscard]] Stats stats() const;

private:
    [[nodiscard]] asset::Json resolved(const asset::Json& component) const;
    [[nodiscard]] math::Vec3 wind(runtime::World& world) const;

    Options options_;
    std::unordered_map<Guid, Emitter> emitters_;
    std::unordered_map<Guid, asset::Json> components_;  // what each emitter was built from (a change rebuilds it)
    double step_ms_ = 0;
    std::uint64_t timed_frame_ = ~0ull;  // the frame step_ms_ is adding up
};

} // namespace noor::fx
