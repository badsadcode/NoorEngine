#pragma once
// Physics (ADR-010: Jolt) as a runtime system (ADR-027).
//
// Components:
//   "collider":  {"shape": "box",     "size": [1, 1, 1]}
//                {"shape": "sphere",  "radius": 0.5}
//                {"shape": "capsule", "radius": 0.3, "height": 1.8}   (total height, ends included)
//                {"shape": "mesh"}   static only: the triangles of the entity's own `mesh`
//                                    component and of the meshes below it (a whole GLB model),
//                                    or, with "asset" + "mesh_index", that mesh (Noor Interchange
//                                    collision nodes are imported this way)
//                optional: "offset": [x, y, z], "trigger": true (detects, doesn't push)
//   "rigidbody": {"type": "dynamic" | "kinematic" | "static", "mass": 1, "friction": 0.5,
//                 "restitution": 0, "linear_damping": 0.05, "gravity_scale": 1}
//   "character": {"radius": 0.3, "height": 1.8, "max_slope": 50, "step_height": 0.4, "mass": 70}
//                A capsule character controller (Jolt CharacterVirtual) with its feet at the entity's
//                origin: slides along walls, walks up steps and slopes, sticks to the floor going down,
//                jumps. Scripts steer it each frame with character_move / character_jump. Raycasts and
//                triggers see it (an inner body). It moves its entity; it doesn't need a collider.
// An entity with a collider and no rigidbody is static. Sizes are multiplied by the entity's world
// scale when play starts. Dynamic bodies move their entity; kinematic bodies follow it (scripts move
// them); static bodies stay where they started.
//
// Events (runtime::Event): collision enter/exit between bodies, trigger enter/exit with sensors,
// posted for both entities during the fixed step.

#include "noor/runtime/world.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace noor::physics {

struct Triangles {
    std::vector<math::Vec3> positions;
    std::vector<std::uint32_t> indices;  // triangle list
};

struct RayHit {
    Guid entity;
    math::Vec3 point;
    math::Vec3 normal;
    float distance = 0;
};

class PhysicsSystem final : public runtime::System {
public:
    struct Options {
        math::Vec3 gravity{0, -9.81f, 0};
        // Triangles of a `mesh` component (for mesh colliders), from the host's asset system.
        std::function<std::optional<Triangles>(const asset::Json& mesh_component)> mesh_triangles;
    };

    explicit PhysicsSystem(Options options);
    ~PhysicsSystem() override;
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    [[nodiscard]] std::string_view name() const override { return "physics"; }
    void entity_started(runtime::World& world, scene::Entity& entity) override;
    void fixed_update(runtime::World& world, double dt) override;
    void entity_destroyed(runtime::World& world, const scene::Entity& entity) override;

    // ---- Queries and forces (the scripting API) ----
    [[nodiscard]] std::optional<RayHit> raycast(math::Vec3 origin, math::Vec3 direction, float max_distance,
                                                std::optional<Guid> ignore = std::nullopt) const;
    [[nodiscard]] bool has_body(const Guid& entity) const;
    [[nodiscard]] std::optional<math::Vec3> velocity(const Guid& entity) const;
    void set_velocity(const Guid& entity, math::Vec3 v);
    void add_impulse(const Guid& entity, math::Vec3 impulse);  // kg·m/s, now
    void add_force(const Guid& entity, math::Vec3 force);      // newtons, during the next step

    // ---- Characters ----
    [[nodiscard]] bool has_character(const Guid& entity) const;
    // Horizontal velocity (m/s, world) wanted this frame; the vertical part is ignored. Reset after
    // each frame, so scripts call it every frame they want to move.
    void character_move(const Guid& entity, math::Vec3 velocity);
    // Jumps with this upward speed (m/s) at the next fixed step, if the character stands on ground.
    void character_jump(const Guid& entity, float speed);
    [[nodiscard]] bool is_grounded(const Guid& entity) const;

    void late_update(runtime::World& world, double dt) override;

    [[nodiscard]] std::size_t body_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::physics
