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
//                                    collision nodes are imported this way; a CSG model: its cooked triangles)
//                optional: "offset": [x, y, z], "trigger": true (detects, doesn't push)
//   "rigidbody": {"type": "dynamic" | "kinematic" | "static", "mass": 1, "friction": 0.5,
//                 "restitution": 0, "linear_damping": 0.05, "gravity_scale": 1}
//   "terrain":   heightfield tiles (noor/terrain/terrain.h) collide unless "collision": false: one
//                static height field per tile, from the host's terrain_tiles.
//   "character": {"radius": 0.3, "height": 1.8, "max_slope": 50, "step_height": 0.4, "mass": 70}
//                A capsule character controller (Jolt CharacterVirtual) with its feet at the entity's
//                origin: slides along walls, walks up steps and slopes, sticks to the floor going down,
//                jumps. Scripts steer it each frame with character_move / character_jump. Raycasts and
//                triggers see it (an inner body). It moves its entity; it doesn't need a collider.
//                Feel (2D part 2, ADR-040; all off by default, so 3D characters move as they always have):
//                "plane": "xy" keeps it in its XY plane (2D: it moves along X only, never drifts in Z);
//                "acceleration" / "deceleration" / "air_acceleration" (m/s2; 0: reaches the asked speed at once);
//                "gravity_scale" (1); "coyote_time" (s: a jump still works this long after walking off a ledge);
//                "jump_buffer" (s: a jump asked this long before landing happens on landing); "jump_cut" (0.5:
//                letting go of jump while rising keeps this share of the upward speed: character_jump_release).
//   "collider" + "one_way": true: a level platform characters stand on but jump up through from below and from the
//                sides, and drop through with character_drop.
//   "platformer": {"speed": 6, "jump_height": 1.5, "axis": "move_x", "jump": "jump", "down": "move_y",
//                  "animations": {"idle": ..., "run": ..., "jump": ..., "fall": ...}}  (2D part 2)
//                Built-in platformer controls for a character: the axis walks it, the jump action jumps (to
//                jump_height when held, shorter when let go early), down + jump drops through a one-way platform. It
//                turns the entity's sprite to face the way it moves and plays the named sprite animations (from how it
//                actually moves, so a script may steer it with character_move / character_jump instead of the keys).
// An entity with a collider and no rigidbody is static. Sizes are multiplied by the entity's world
// scale when play starts. Dynamic bodies move their entity; kinematic bodies follow it (scripts move
// them); static bodies stay where they started.
//
// Events (runtime::Event): collision enter/exit between bodies, trigger enter/exit with sensors,
// posted for both entities during the fixed step.
//
// Water (Milestone 8D, part 5, ADR-037), through the World's water query (game::WaterSystem):
//   - Dynamic bodies float. Each is cut into cells (a box 3 x 3 x 3; a sphere, capsule or hull: the cells of a 5 x 5 x 5
//     grid over it that are inside it, scaled to its volume); each cell under the surface is pushed up by the water it
//     displaces (1000 kg/m3 x g x its volume x how much of its height is under) at its centre, and slowed by the water
//     (drag on the cell's velocity, in proportion). So a body settles where its density says (a 500 kg/m3 crate half
//     under), tips upright, and rides the waves. "rigidbody": {"buoyancy": 1 (0: sinks like a stone), "water_drag": 1}.
//   - Characters wade and swim: with the water above their feet they slow down ("wade_speed": the speed's share left at
//     swimming depth, 0.5), and once it's deeper than "swim_depth" (0.65 of their height) they swim: no gravity, held with
//     their chest at the surface, moving at "swim_speed" (0.6) of what's asked. Their state ("dry", "wading", "swimming",
//     "under": the head under the surface) and the water's depth at their feet and head are character_water(); each
//     change is an Event::Kind::Water for the character, its name the new state.

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

// One terrain tile's heights for a static height field, in the entity's space.
struct HeightTile {
    math::Vec3 corner;          // sample (0, 0)
    float cell_size = 1.0f;
    std::uint32_t samples = 0;  // per side
    std::vector<float> heights;  // samples x samples, row by row (z rows of x samples)
};

// A static collision shape in world space for a scattered instance (Milestone 7, part 8): a tree's
// trunk as an upright capsule (`height` from end to end, caps included, centred on `centre`), or a
// rock's piece as a convex hull of `points`.
struct StaticShape {
    enum class Kind : std::uint8_t { Capsule, Convex } kind = Kind::Capsule;
    math::Vec3 centre;
    float radius = 0.3f;
    float height = 2.0f;
    std::vector<math::Vec3> points;  // Convex: world positions (the hull of them collides)
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
        // Triangles of a CSG model (the entity with `csg_model`), in the model's space: cooked data.
        std::function<std::optional<Triangles>(const Guid& model)> csg_triangles;
        // Height field tiles of a `terrain` component.
        std::function<std::vector<HeightTile>(const asset::Json& terrain_component)> terrain_tiles;
        // Collision of a `scatter` entity's instances (their models' capsule and convex collision nodes), in world space.
        std::function<std::vector<StaticShape>(const scene::Entity& scatter, const runtime::World& world)> scatter_shapes;
    };

    explicit PhysicsSystem(Options options);
    ~PhysicsSystem() override;
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    [[nodiscard]] std::string_view name() const override { return "physics"; }
    void entity_started(runtime::World& world, scene::Entity& entity) override;
    void update(runtime::World& world, double dt) override;  // built-in platformer controls
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
    // Jumps with this upward speed (m/s) at the next fixed step, if the character stands on ground (or left it less
    // than coyote_time ago, or lands within jump_buffer).
    void character_jump(const Guid& entity, float speed);
    // The jump button let go (2D part 2): if it's still rising from a jump, it keeps jump_cut of its upward speed.
    void character_jump_release(const Guid& entity);
    // Falls through the one-way platform it stands on (ignores one-way platforms for a quarter of a second).
    void character_drop(const Guid& entity);
    // Its velocity as the controller last moved it (m/s, world).
    [[nodiscard]] std::optional<math::Vec3> character_velocity(const Guid& entity) const;
    [[nodiscard]] bool is_grounded(const Guid& entity) const;
    // In the water (8D part 5): its state and the water's depth at its feet and at the top of its head (positive: under).
    enum class WaterState : std::uint8_t { Dry, Wading, Swimming, Under };
    struct CharacterWater {
        WaterState state = WaterState::Dry;
        float feet_depth = 0.0f;
        float head_depth = 0.0f;
    };
    [[nodiscard]] std::optional<CharacterWater> character_water(const Guid& entity) const;
    [[nodiscard]] static const char* water_state_name(WaterState s) noexcept;

    void late_update(runtime::World& world, double dt) override;

    [[nodiscard]] std::size_t body_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::physics
