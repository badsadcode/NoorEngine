#include "noor/physics/physics.h"

#include "noor/foundation/log.h"
#include "noor/runtime/script_api.h"

// Jolt.h must come first among Jolt headers.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <map>
#include <mutex>
#include <unordered_map>

namespace noor::physics {
namespace {

using asset::Json;
namespace api = runtime::api;

// ---- Layers: static things never test against each other ----
namespace layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::uint kCount = 2;
} // namespace layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return layers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == JPH::BroadPhaseLayer(0) ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        return layer != layers::kStatic || bp == JPH::BroadPhaseLayer(layers::kMoving);
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a != layers::kStatic || b == layers::kMoving;
    }
};

void jolt_trace(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    NOOR_LOG_DEBUG("Physics", "Jolt: {}", buffer);
}

void init_jolt_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &jolt_trace;
        JPH::Factory::sInstance = new JPH::Factory();  // process lifetime, like Jolt's samples
        JPH::RegisterTypes();
    });
}

JPH::Vec3 to_j(math::Vec3 v) { return {v.x, v.y, v.z}; }
math::Vec3 from_j(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Quat to_j(math::Quat q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
math::Quat from_j(JPH::Quat q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

float number(const Json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}

// What a dynamic body may do (ADR-040): `"plane": "xy"` moves in X and Y and turns about Z (2D); `lock_position` and
// `lock_rotation` (lists of "x", "y", "z") take axes away, in 2D or 3D. Everything locked would leave Jolt nothing to
// move, so at least one translation stays.
JPH::EAllowedDOFs allowed_dofs(const Json& rb) {
    using D = JPH::EAllowedDOFs;
    D allowed = D::All;
    const auto take = [&](D axis) { allowed = static_cast<D>(static_cast<std::uint8_t>(allowed) & ~static_cast<std::uint8_t>(axis)); };
    if (rb.value("plane", std::string()) == "xy") allowed = D::Plane2D;
    const auto locks = [&](const char* key, D x, D y, D z) {
        const auto it = rb.find(key);
        if (it == rb.end() || !it->is_array()) return;
        for (const auto& a : *it) {
            if (!a.is_string()) continue;
            const std::string s = a.get<std::string>();
            if (s == "x") take(x);
            else if (s == "y") take(y);
            else if (s == "z") take(z);
        }
    };
    locks("lock_position", D::TranslationX, D::TranslationY, D::TranslationZ);
    locks("lock_rotation", D::RotationX, D::RotationY, D::RotationZ);
    if ((static_cast<std::uint8_t>(allowed) & 0b111) == 0) allowed = static_cast<D>(static_cast<std::uint8_t>(allowed) | static_cast<std::uint8_t>(D::TranslationY));
    return allowed;
}

math::Vec3 vec3(const Json& j, const char* key, math::Vec3 fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3) return fallback;
    for (const auto& v : *it)
        if (!v.is_number()) return fallback;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}

math::Vec3 scale_of(const math::Mat4& m) {
    return {math::length(math::transform_vector(m, {1, 0, 0})), math::length(math::transform_vector(m, {0, 1, 0})),
            math::length(math::transform_vector(m, {0, 0, 1}))};
}

} // namespace

struct PhysicsSystem::Impl {
    Options options;
    BroadPhaseLayers broad_phase_layers;
    ObjectVsBroadPhase object_vs_broad_phase;
    ObjectPairs object_pairs;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
    JPH::PhysicsSystem system;

    enum class Type { Static, Dynamic, Kinematic };
    // Buoyancy (8D part 5): a dynamic body cut into cells, in its own space.
    struct Cell {
        math::Vec3 centre;   // body space, from its centre of mass
        float volume;        // m3
    };
    struct Body {
        JPH::BodyID id;
        Type type;
        bool sensor;
        std::vector<Cell> cells;
        math::Vec3 cell_half{0, 0, 0};  // half a cell's size (body space)
        float bound = 0.0f;             // the farthest any of it reaches from the body's origin
        float buoyancy = 1.0f, water_drag = 1.0f;
        float last_water = 0.0f;        // the water's height summed over its wet cells last step (waves wake it up)
        bool floating = false;          // partly under the water (not allowed to sleep)
    };
    std::unordered_map<Guid, Body> bodies;
    std::unordered_map<Guid, std::vector<JPH::BodyID>> terrain_bodies;  // static bodies per entity: terrain tiles, scattered trunks
    struct Character {
        JPH::Ref<JPH::CharacterVirtual> controller;
        float step_height = 0.4f;
        math::Vec3 wanted{0, 0, 0};  // horizontal velocity this frame
        float jump = 0.0f;           // pending jump speed
        // In the water (8D part 5).
        float height = 1.8f, swim_depth = 1.17f, wade_speed = 0.5f, swim_speed = 0.6f;
        CharacterWater water;
        // Feel (2D part 2, ADR-040). Zeros mean "as before": instant speed, no coyote time, no buffer.
        bool plane = false;  // "plane": "xy": moves along X only, held at plane_z
        float plane_z = 0.0f;
        float acceleration = 0.0f, deceleration = 0.0f, air_acceleration = 0.0f, gravity_scale = 1.0f;
        float coyote_time = 0.0f, jump_buffer = 0.0f, jump_cut = 0.5f;
        math::Vec3 horizontal{0, 0, 0};  // the horizontal velocity it's moving with (approaches wanted)
        float buffered_jump = 0.0f, buffer_left = 0.0f;  // a jump asked for, waiting for ground
        float since_ground = 1e9f;                      // seconds since it last stood on ground
        bool jumped = false;                            // has jumped since it last stood on ground
        bool release = false;                           // the jump button was let go
        bool cut = false;                               // this jump's rise has been cut already
        float drop_left = 0.0f;                         // ignoring one-way platforms this much longer
        math::Vec3 velocity{0, 0, 0};                   // what it moved with last step
        // Built-in platformer controls ("platformer" component): the animation it last asked for, and its facing.
        std::string animation;
        bool facing_left = false;
    };
    std::unordered_map<Guid, Character> characters;
    // One-way platforms (2D part 2): each one's top (world y), by body. Characters stand on them only from above.
    std::unordered_map<JPH::uint32, float> one_way_tops;
    struct CharacterListener final : JPH::CharacterContactListener {
        Impl* d = nullptr;
        bool OnContactValidate(const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact) override {
            const auto top = d->one_way_tops.find(contact.mBodyB.GetIndexAndSequenceNumber());
            if (top == d->one_way_tops.end()) return true;
            const auto ud = character->GetUserData();
            if (ud > 0 && ud <= d->entity_of.size()) {
                const auto c = d->characters.find(d->entity_of[static_cast<std::size_t>(ud - 1)]);
                if (c != d->characters.end() && c->second.drop_left > 0.0f) return false;  // dropping through
            }
            // From above only: its feet at or over the top (a little slack for standing on it), and not rising.
            return character->GetPosition().GetY() >= top->second - 0.05f && character->GetLinearVelocity().GetY() <= 0.01f;
        }
    } character_listener;
    // The last two fixed-step poses of everything physics moves (dynamic bodies, characters), so
    // late_update can tell the World where to draw them between steps.
    struct Pose {
        math::Vec3 prev, curr;
        math::Quat prev_rot, curr_rot;
        bool character = false;  // rotation comes from the entity (scripts turn characters every frame)
    };
    std::unordered_map<Guid, Pose> poses;
    void moved(const Guid& entity, math::Vec3 p, math::Quat q, bool character) {
        auto [it, added] = poses.try_emplace(entity, Pose{p, p, q, q, character});
        if (!added) {
            it->second.curr = p;
            it->second.curr_rot = q;
        }
    }
    std::vector<Guid> entity_of;  // body user data - 1 -> entity
    // Jolt drops the contacts of bodies that fall asleep and re-adds them when they wake. To
    // scripts the resting crate never left the floor: such pairs are parked here, not reported.
    std::map<std::pair<Guid, Guid>, bool> sleeping_pairs;  // (a, b) ordered, value unused

    // Contacts: Jolt reports sub-shape pairs; count them per body pair to report enter/exit once.
    struct Listener final : JPH::ContactListener {
        std::mutex mutex;
        std::map<std::pair<JPH::uint32, JPH::uint32>, int> pair_counts;
        struct Change {
            JPH::BodyID a, b;
            bool entered;
        };
        std::vector<Change> changes;
        static std::pair<JPH::uint32, JPH::uint32> key(JPH::BodyID a, JPH::BodyID b) {
            return std::minmax(a.GetIndexAndSequenceNumber(), b.GetIndexAndSequenceNumber());
        }
        void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override {
            std::scoped_lock lock(mutex);
            if (pair_counts[key(a.GetID(), b.GetID())]++ == 0) changes.push_back({a.GetID(), b.GetID(), true});
        }
        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            std::scoped_lock lock(mutex);
            const auto k = key(pair.GetBody1ID(), pair.GetBody2ID());
            const auto it = pair_counts.find(k);
            if (it == pair_counts.end()) return;
            if (--it->second <= 0) {
                pair_counts.erase(it);
                changes.push_back({pair.GetBody1ID(), pair.GetBody2ID(), false});
            }
        }
    } listener;

    explicit Impl(Options o) : options(std::move(o)) {
        init_jolt_once();
        temp = std::make_unique<JPH::TempAllocatorImpl>(16u << 20);
        jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        system.Init(65536, 0, 65536, 16384, broad_phase_layers, object_vs_broad_phase, object_pairs);
        system.SetGravity(to_j(options.gravity));
        system.SetContactListener(&listener);
        character_listener.d = this;
    }

    ~Impl() {
        characters.clear();  // removes their inner bodies
        auto& bi = system.GetBodyInterface();
        for (auto& [entity, body] : bodies) {
            bi.RemoveBody(body.id);
            bi.DestroyBody(body.id);
        }
    }

    // Cuts a dynamic body's shape into cells (relative to its centre of mass): its local bounds in a grid, the cells whose centres are inside the shape,
    // each given an equal share of the shape's volume (a box: 3 x 3 x 3, all inside; anything else 5 x 5 x 5).
    void make_cells(Body& body, const JPH::Shape& shape) {
        const JPH::AABox box = shape.GetLocalBounds();
        const bool is_box = shape.GetSubType() == JPH::EShapeSubType::Box;
        const int n = is_box ? 3 : 5;
        const JPH::Vec3 size = box.GetSize();
        const math::Vec3 cell{size.GetX() / n, size.GetY() / n, size.GetZ() / n};
        std::vector<math::Vec3> inside;
        for (int z = 0; z < n; ++z)
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    const JPH::Vec3 p = box.mMin + JPH::Vec3((x + 0.5f) * cell.x, (y + 0.5f) * cell.y, (z + 0.5f) * cell.z);
                    if (!is_box) {
                        // Inside: the shape's own point query (bounds and points are relative to its centre of mass).
                        JPH::AllHitCollisionCollector<JPH::CollidePointCollector> hits;
                        shape.CollidePoint(p, JPH::SubShapeIDCreator(), hits);
                        if (hits.mHits.empty()) continue;
                    }
                    inside.push_back(from_j(p));
                }
        if (inside.empty()) return;
        const float volume = shape.GetVolume() / static_cast<float>(inside.size());
        for (const auto& p : inside) {
            body.cells.push_back({p, volume});
            body.bound = std::max(body.bound, math::length(p) + 0.5f * math::length(cell));
        }
        body.cell_half = cell * 0.5f;
    }

    // Buoyancy and drag on the dynamic bodies in water, before the step (8D part 5).
    void float_bodies(runtime::World& world) {
        constexpr float kWaterDensity = 1000.0f;
        constexpr float kDrag = 3.0f;  // 1/s for a cell fully under: about critically damped for a body a metre across
        auto& bi = system.GetBodyInterface();
        const float g = -system.GetGravity().GetY();
        for (auto& [entity, body] : bodies) {
            if (body.type != Type::Dynamic || body.cells.empty() || body.buoyancy <= 0.0f) continue;
            const math::Vec3 pos = from_j(bi.GetCenterOfMassPosition(body.id));
            const math::Quat rot = from_j(bi.GetRotation(body.id));
            // Nothing to do far above any water (one query at its middle first).
            const auto here = world.water_at(pos.x, pos.z);
            if (!here || pos.y - body.bound > here->height + 3.0f) continue;
            const math::Vec3 com = pos;
            const math::Vec3 v = from_j(bi.GetLinearVelocity(body.id));
            const math::Vec3 w = from_j(bi.GetAngularVelocity(body.id));
            // A cell's height in the world (its box turned): what's under the surface is measured over it.
            const math::Vec3 ax = math::rotate(rot, {body.cell_half.x, 0, 0}), ay = math::rotate(rot, {0, body.cell_half.y, 0}),
                             az = math::rotate(rot, {0, 0, body.cell_half.z});
            const float half_h = std::abs(ax.y) + std::abs(ay.y) + std::abs(az.y);
            float water_sum = 0.0f, under_cells = 0.0f;
            bool wet = false;
            for (const Cell& cell : body.cells) {
                const math::Vec3 p = pos + math::rotate(rot, cell.centre);
                const auto s = world.water_at(p.x, p.z);
                if (!s) continue;
                const float under = std::clamp((s->height - (p.y - half_h)) / (2.0f * half_h), 0.0f, 1.0f);
                if (under <= 0.0f) continue;
                wet = true;
                water_sum += s->height;
                under_cells += under;
                const math::Vec3 up{0, kWaterDensity * g * cell.volume * under * body.buoyancy, 0};
                // Slowed towards the water's own velocity: a river's current carries it along (8D part 6).
                const math::Vec3 vp = v + math::cross(w, p - com) - s->flow;
                const float mass_share = kWaterDensity * cell.volume * under;
                const math::Vec3 drag = vp * (-kDrag * body.water_drag * mass_share);
                bi.AddForce(body.id, to_j(up + drag), to_j(p), JPH::EActivation::DontActivate);
            }
            // Floating (partly under) it isn't allowed to sleep: Jolt would freeze it a few millimetres off as it slows
            // (measured: 8.5 mm on a light plank), and woken again it would repeat that for ever. The water's drag settles
            // it instead. Fully under (sunk, or out of the water) it may sleep; the water moving under it wakes it.
            const bool floating = wet && under_cells < static_cast<float>(body.cells.size()) - 1e-3f;
            if (floating != body.floating) {
                const JPH::BodyLockWrite lock(system.GetBodyLockInterface(), body.id);
                if (lock.Succeeded()) lock.GetBody().SetAllowSleeping(!floating);
                body.floating = floating;
            }
            if (wet && !bi.IsActive(body.id) && (floating || std::abs(water_sum - body.last_water) > 1e-3f)) bi.ActivateBody(body.id);
            body.last_water = water_sum;
        }
    }

    std::optional<Guid> entity_for(JPH::BodyID id) {
        const JPH::uint64 data = system.GetBodyInterfaceNoLock().GetUserData(id);
        if (data == 0 || data > entity_of.size()) return std::nullopt;
        return entity_of[data - 1];
    }

    // Collects the triangles of the entity's own mesh and every mesh below it, in the entity's
    // space with its world scale applied (the body has the entity's world position and rotation).
    std::optional<Triangles> model_triangles(runtime::World& world, const scene::Entity& root, const Json& collider) {
        if (!options.mesh_triangles) return std::nullopt;
        const auto root_world = api::world_matrix(world, root.id);
        const auto root_rot = api::world_rotation(world, root.id);
        if (!root_world || !root_rot) return std::nullopt;
        const math::Vec3 root_pos = root_world->transform_point({0, 0, 0});
        const math::Quat inv_rot = math::conjugate(*root_rot);

        Triangles out;
        if (collider.contains("asset")) {
            // A collision mesh of its own (Noor Interchange `-colonly` nodes): {"asset", "mesh_index"}.
            auto tris = options.mesh_triangles(collider);
            if (!tris) return std::nullopt;
            for (const auto& p : tris->positions) out.positions.push_back(math::rotate(inv_rot, root_world->transform_point(p) - root_pos));
            out.indices = std::move(tris->indices);
            return out.indices.empty() ? std::nullopt : std::optional(std::move(out));
        }
        std::vector<Guid> stack{root.id};
        std::vector<Guid> subtree;
        while (!stack.empty()) {
            const Guid id = stack.back();
            stack.pop_back();
            subtree.push_back(id);
            for (const auto& e : world.scene().entities)
                if (e.parent == id) stack.push_back(e.id);
        }
        for (const Guid& id : subtree) {
            const scene::Entity* e = world.find(id);
            if (!e) continue;
            std::optional<Triangles> tris;
            if (e->has("mesh") && options.mesh_triangles) tris = options.mesh_triangles(e->components["mesh"]);
            else if (e->has("csg_model") && options.csg_triangles) tris = options.csg_triangles(id);
            else continue;
            const auto mesh_world = api::world_matrix(world, id);
            if (!tris || !mesh_world) continue;
            const auto base = static_cast<std::uint32_t>(out.positions.size());
            for (const auto& p : tris->positions)
                out.positions.push_back(math::rotate(inv_rot, mesh_world->transform_point(p) - root_pos));
            for (const auto i : tris->indices) out.indices.push_back(base + i);
        }
        if (out.indices.empty()) return std::nullopt;
        return out;
    }

    JPH::RefConst<JPH::Shape> make_shape(runtime::World& world, const scene::Entity& e, const Json& collider, math::Vec3 scale,
                                         bool may_be_mesh, std::string& error) {
        const std::string shape = collider.value("shape", std::string("box"));
        const float s_max = std::max({scale.x, scale.y, scale.z});
        JPH::ShapeSettings::ShapeResult result;
        if (shape == "box") {
            const math::Vec3 size = vec3(collider, "size", {1, 1, 1});
            const JPH::Vec3 half(std::max(0.005f, size.x * scale.x * 0.5f), std::max(0.005f, size.y * scale.y * 0.5f),
                                 std::max(0.005f, size.z * scale.z * 0.5f));
            const float convex = std::min(JPH::cDefaultConvexRadius, 0.5f * half.ReduceMin());
            result = JPH::BoxShapeSettings(half, convex).Create();
        } else if (shape == "sphere") {
            result = JPH::SphereShapeSettings(std::max(0.005f, number(collider, "radius", 0.5f) * s_max)).Create();
        } else if (shape == "rectangle") {
            // 2D shapes (ADR-040): slabs in the XY plane, `depth` thick (1 m), so 2D bodies in one plane always meet.
            math::Vec3 size{1, 1, 0};  // [w, h] (a third number is ignored: the slab's thickness is `depth`)
            if (const auto sz = collider.find("size"); sz != collider.end() && sz->is_array() && sz->size() >= 2 && (*sz)[0].is_number() && (*sz)[1].is_number())
                size = {(*sz)[0].get<float>(), (*sz)[1].get<float>(), 0.0f};
            const float depth = number(collider, "depth", 1.0f);
            const JPH::Vec3 half(std::max(0.005f, size.x * scale.x * 0.5f), std::max(0.005f, size.y * scale.y * 0.5f),
                                 std::max(0.005f, depth * scale.z * 0.5f));
            const float convex = std::min(JPH::cDefaultConvexRadius, 0.5f * half.ReduceMin());
            result = JPH::BoxShapeSettings(half, convex).Create();
        } else if (shape == "circle") {
            // A sphere touches a plane exactly as a disc does, and rolls like one when the body is locked to the plane.
            result = JPH::SphereShapeSettings(std::max(0.005f, number(collider, "radius", 0.5f) * std::max(scale.x, scale.y))).Create();
        } else if (shape == "polygon") {
            // A convex polygon from `points` [[x, y], ...] (local, before scale), extruded `depth` along Z.
            const auto pts_json = collider.find("points");
            JPH::Array<JPH::Vec3> pts;
            const float half_depth = std::max(0.005f, number(collider, "depth", 1.0f) * scale.z * 0.5f);
            if (pts_json != collider.end() && pts_json->is_array()) {
                for (const auto& q : *pts_json) {
                    if (!q.is_array() || q.size() < 2 || !q[0].is_number() || !q[1].is_number()) continue;
                    const float x = q[0].get<float>() * scale.x, y = q[1].get<float>() * scale.y;
                    pts.push_back(JPH::Vec3(x, y, -half_depth));
                    pts.push_back(JPH::Vec3(x, y, half_depth));
                }
            }
            if (pts.size() < 6) {
                error = "a polygon needs at least three points: \"points\": [[x, y], ...]";
                return nullptr;
            }
            result = JPH::ConvexHullShapeSettings(pts).Create();
        } else if (shape == "capsule") {
            const float radius = std::max(0.005f, number(collider, "radius", 0.3f) * std::max(scale.x, scale.z));
            const float height = number(collider, "height", 1.8f) * scale.y;
            result = JPH::CapsuleShapeSettings(std::max(0.001f, height * 0.5f - radius), radius).Create();
        } else if (shape == "convex") {
            // A convex hull of the collision node's (or the entity's meshes') points: rocks (Noor Interchange
            // `-convcolonly` pieces). Unlike "mesh", a hull can move (dynamic bodies).
            auto tris = model_triangles(world, e, collider);
            if (!tris || tris->positions.size() < 4) {
                error = "no mesh points found for the convex hull";
                return nullptr;
            }
            JPH::Array<JPH::Vec3> pts;
            pts.reserve(tris->positions.size());
            for (const auto& p : tris->positions) pts.push_back(JPH::Vec3(p.x, p.y, p.z));
            result = JPH::ConvexHullShapeSettings(pts).Create();
        } else if (shape == "mesh") {
            if (!may_be_mesh) {
                error = "mesh colliders can only be static (use box, sphere or capsule for moving bodies)";
                return nullptr;
            }
            auto tris = model_triangles(world, e, collider);
            if (!tris) {
                error = "no mesh triangles found on this entity or below it";
                return nullptr;
            }
            JPH::VertexList vertices;
            vertices.reserve(tris->positions.size());
            for (const auto& p : tris->positions) vertices.push_back(JPH::Float3(p.x, p.y, p.z));
            JPH::IndexedTriangleList triangles;
            triangles.reserve(tris->indices.size() / 3);
            for (std::size_t i = 0; i + 2 < tris->indices.size(); i += 3)
                triangles.push_back(JPH::IndexedTriangle(tris->indices[i], tris->indices[i + 1], tris->indices[i + 2], 0));
            result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
        } else {
            error = std::format("unknown collider shape \"{}\" (box, sphere, capsule, convex, mesh; 2D: rectangle, circle, polygon)", shape);
            return nullptr;
        }
        if (result.HasError()) {
            error = result.GetError().c_str();
            return nullptr;
        }
        JPH::RefConst<JPH::Shape> made = result.Get();
        const math::Vec3 offset = vec3(collider, "offset", {0, 0, 0});
        if (offset.x != 0 || offset.y != 0 || offset.z != 0) {
            const math::Vec3 o{offset.x * scale.x, offset.y * scale.y, offset.z * scale.z};
            auto shifted = JPH::RotatedTranslatedShapeSettings(to_j(o), JPH::Quat::sIdentity(), made).Create();
            if (shifted.HasError()) {
                error = shifted.GetError().c_str();
                return nullptr;
            }
            made = shifted.Get();
        }
        return made;
    }
};

PhysicsSystem::PhysicsSystem(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::entity_started(runtime::World& world, scene::Entity& entity) {
    if (const auto ch = entity.components.find("character"); ch != entity.components.end() && ch->is_object()) {
        auto& d = *impl_;
        const float radius = std::max(0.05f, number(*ch, "radius", 0.3f));
        const float height = std::max(radius * 2.0f + 0.01f, number(*ch, "height", 1.8f));
        const float half_cylinder = height * 0.5f - radius;
        // Feet at the entity's origin: the capsule sits on top of it.
        auto capsule = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0, height * 0.5f, 0), JPH::Quat::sIdentity(),
                                                           new JPH::CapsuleShape(half_cylinder, radius)).Create();
        if (capsule.HasError()) {
            NOOR_LOG_ERROR("Physics", "'{}': character not created: {}", entity.name, capsule.GetError().c_str());
            return;
        }
        JPH::CharacterVirtualSettings s;
        s.mShape = capsule.Get();
        s.mInnerBodyShape = capsule.Get();
        s.mInnerBodyLayer = layers::kMoving;
        s.mMaxSlopeAngle = JPH::DegreesToRadians(number(*ch, "max_slope", 50.0f));
        s.mMass = std::max(1.0f, number(*ch, "mass", 70.0f));
        s.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);  // contacts below the lower sphere's centre carry it
        const auto pos = runtime::api::world_position(world, entity.id);
        d.entity_of.push_back(entity.id);
        Impl::Character c;
        c.controller = new JPH::CharacterVirtual(&s, to_j(pos.value_or(math::Vec3{})), JPH::Quat::sIdentity(), d.entity_of.size(), &d.system);
        c.step_height = std::max(0.0f, number(*ch, "step_height", 0.4f));
        c.height = height;
        c.swim_depth = std::clamp(number(*ch, "swim_depth", 0.65f * height), 0.1f, height);
        c.wade_speed = std::clamp(number(*ch, "wade_speed", 0.5f), 0.0f, 1.0f);
        c.swim_speed = std::clamp(number(*ch, "swim_speed", 0.6f), 0.0f, 2.0f);
        c.plane = ch->value("plane", std::string()) == "xy";
        c.plane_z = pos ? pos->z : 0.0f;
        c.acceleration = std::max(0.0f, number(*ch, "acceleration", 0.0f));
        c.deceleration = std::max(0.0f, number(*ch, "deceleration", c.acceleration));
        c.air_acceleration = std::max(0.0f, number(*ch, "air_acceleration", c.acceleration));
        c.gravity_scale = std::max(0.0f, number(*ch, "gravity_scale", 1.0f));
        c.coyote_time = std::clamp(number(*ch, "coyote_time", 0.0f), 0.0f, 1.0f);
        c.jump_buffer = std::clamp(number(*ch, "jump_buffer", 0.0f), 0.0f, 1.0f);
        c.jump_cut = std::clamp(number(*ch, "jump_cut", 0.5f), 0.0f, 1.0f);
        c.controller->SetListener(&d.character_listener);
        d.characters[entity.id] = std::move(c);
    }
    // Terrain: one static height field per tile, placed at the entity (its rotation and scale are ignored).
    if (const auto t = entity.components.find("terrain"); t != entity.components.end() && t->is_object() && t->value("collision", true) &&
                                                          impl_->options.terrain_tiles) {
        auto& d = *impl_;
        const auto pos = runtime::api::world_position(world, entity.id).value_or(math::Vec3{});
        for (const auto& tile : impl_->options.terrain_tiles(*t)) {
            if (tile.samples < 2 || tile.heights.size() != static_cast<std::size_t>(tile.samples) * tile.samples) continue;
            JPH::HeightFieldShapeSettings hs(tile.heights.data(), to_j(tile.corner), JPH::Vec3(tile.cell_size, 1.0f, tile.cell_size), tile.samples);
            auto result = hs.Create();
            if (result.HasError()) {
                NOOR_LOG_ERROR("Physics", "'{}': terrain tile not created: {}", entity.name, result.GetError().c_str());
                continue;
            }
            JPH::BodyCreationSettings settings(result.Get(), to_j(pos), JPH::Quat::sIdentity(), JPH::EMotionType::Static, layers::kStatic);
            settings.mFriction = 0.8f;
            d.entity_of.push_back(entity.id);
            settings.mUserData = d.entity_of.size();
            const JPH::BodyID id = d.system.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
            if (id.IsInvalid()) {
                NOOR_LOG_ERROR("Physics", "'{}': too many physics bodies", entity.name);
                break;
            }
            d.terrain_bodies[entity.id].push_back(id);
        }
    }
    // Scattered instances (Milestone 7, part 8): static trunks (capsules) and rock pieces (convex hulls), all
    // belonging to the scatter entity.
    if (const auto sc = entity.components.find("scatter"); sc != entity.components.end() && sc->is_object() && impl_->options.scatter_shapes) {
        auto& d = *impl_;
        auto& bi = d.system.GetBodyInterface();
        const auto shapes = impl_->options.scatter_shapes(entity, world);
        if (!shapes.empty()) d.entity_of.push_back(entity.id);
        for (const auto& st : shapes) {
            JPH::ShapeSettings::ShapeResult shape;
            math::Vec3 at = st.centre;
            if (st.kind == StaticShape::Kind::Convex) {
                if (st.points.size() < 4) continue;
                at = {0, 0, 0};
                for (const auto& q : st.points) at = at + q;
                at = at * (1.0f / static_cast<float>(st.points.size()));
                JPH::Array<JPH::Vec3> pts;
                pts.reserve(st.points.size());
                for (const auto& q : st.points) pts.push_back(to_j(q - at));
                shape = JPH::ConvexHullShapeSettings(pts).Create();
            } else {
                const float r = std::max(0.01f, st.radius);
                shape = JPH::CapsuleShapeSettings(std::max(0.001f, st.height * 0.5f - r), r).Create();
            }
            if (shape.HasError()) continue;
            JPH::BodyCreationSettings settings(shape.Get(), to_j(at), JPH::Quat::sIdentity(), JPH::EMotionType::Static, layers::kStatic);
            settings.mFriction = 0.6f;
            settings.mUserData = d.entity_of.size();
            const JPH::BodyID id = bi.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
            if (id.IsInvalid()) {
                NOOR_LOG_ERROR("Physics", "'{}': too many physics bodies", entity.name);
                break;
            }
            d.terrain_bodies[entity.id].push_back(id);
        }
    }
    const auto c = entity.components.find("collider");
    if (c == entity.components.end() || !c->is_object()) return;
    auto& d = *impl_;
    const Json rb = entity.components.contains("rigidbody") && entity.components["rigidbody"].is_object()
                        ? entity.components["rigidbody"] : Json::object();
    const std::string type_name = rb.value("type", std::string(entity.components.contains("rigidbody") ? "dynamic" : "static"));
    const Impl::Type type = type_name == "dynamic" ? Impl::Type::Dynamic : type_name == "kinematic" ? Impl::Type::Kinematic : Impl::Type::Static;

    const auto world_m = runtime::api::world_matrix(world, entity.id);
    const auto world_r = runtime::api::world_rotation(world, entity.id);
    if (!world_m || !world_r) return;
    std::string error;
    const auto shape = d.make_shape(world, entity, *c, scale_of(*world_m), type == Impl::Type::Static, error);
    if (!shape) {
        NOOR_LOG_ERROR("Physics", "'{}': collider not created: {}", entity.name, error);
        return;
    }
    const JPH::EMotionType motion = type == Impl::Type::Dynamic ? JPH::EMotionType::Dynamic
                                    : type == Impl::Type::Kinematic ? JPH::EMotionType::Kinematic
                                                                    : JPH::EMotionType::Static;
    JPH::BodyCreationSettings settings(shape, to_j(world_m->transform_point({0, 0, 0})), to_j(*world_r), motion,
                                       type == Impl::Type::Static ? layers::kStatic : layers::kMoving);
    settings.mFriction = number(rb, "friction", 0.5f);
    settings.mRestitution = number(rb, "restitution", 0.0f);
    settings.mLinearDamping = number(rb, "linear_damping", 0.05f);
    settings.mAngularDamping = number(rb, "angular_damping", 0.05f);
    settings.mGravityFactor = number(rb, "gravity_scale", 1.0f);
    settings.mIsSensor = c->value("trigger", false);
    if (settings.mIsSensor && type != Impl::Type::Dynamic) settings.mCollideKinematicVsNonDynamic = true;
    if (type == Impl::Type::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(0.001f, number(rb, "mass", 1.0f));
        settings.mAllowedDOFs = allowed_dofs(rb);  // 2D (ADR-040): locked to a plane, or any axes locked
        if (c->value("shape", std::string()) == "circle") {
            // A circle is a sphere to Jolt, but it should turn like a disc: I = m r^2 / 2 about Z (a sphere's 2/5 would
            // roll it 7% faster down a slope than a disc rolls). X and Y get a disc's m r^2 / 4.
            const math::Vec3 s = scale_of(*world_m);
            const float r = std::max(0.005f, number(*c, "radius", 0.5f) * std::max(s.x, s.y));
            const float m = settings.mMassPropertiesOverride.mMass;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(JPH::Vec3(m * r * r * 0.25f, m * r * r * 0.25f, m * r * r * 0.5f));
        }
    }
    d.entity_of.push_back(entity.id);
    settings.mUserData = d.entity_of.size();
    auto& bi = d.system.GetBodyInterface();
    const JPH::BodyID id = bi.CreateAndAddBody(settings, type == Impl::Type::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        NOOR_LOG_ERROR("Physics", "'{}': too many physics bodies", entity.name);
        return;
    }
    Impl::Body body{id, type, settings.mIsSensor};
    if (type == Impl::Type::Static && c->value("one_way", false))  // 2D part 2: a platform characters jump up through
        d.one_way_tops[id.GetIndexAndSequenceNumber()] = bi.GetTransformedShape(id).GetWorldSpaceBounds().mMax.GetY();
    if (type == Impl::Type::Dynamic && !settings.mIsSensor) {
        body.buoyancy = std::max(0.0f, number(rb, "buoyancy", 1.0f));
        body.water_drag = std::max(0.0f, number(rb, "water_drag", 1.0f));
        d.make_cells(body, *shape);
    }
    d.bodies[entity.id] = std::move(body);
}

void PhysicsSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) {
    auto& d = *impl_;
    d.characters.erase(entity.id);
    d.poses.erase(entity.id);
    if (const auto t = d.terrain_bodies.find(entity.id); t != d.terrain_bodies.end()) {
        auto& tbi = d.system.GetBodyInterface();
        for (const auto& id : t->second) {
            tbi.RemoveBody(id);
            tbi.DestroyBody(id);
        }
        d.terrain_bodies.erase(t);
    }
    const auto it = d.bodies.find(entity.id);
    if (it == d.bodies.end()) return;
    auto& bi = d.system.GetBodyInterface();
    bi.RemoveBody(it->second.id);
    bi.DestroyBody(it->second.id);
    d.bodies.erase(it);
}

void PhysicsSystem::fixed_update(runtime::World& world, double dt) {
    auto& d = *impl_;
    auto& bi = d.system.GetBodyInterface();
    for (auto& [entity, pose] : d.poses) {  // a new step: what was current is now the previous pose
        pose.prev = pose.curr;
        pose.prev_rot = pose.curr_rot;
    }
    // Kinematic bodies follow their entities (scripts move them).
    for (const auto& [entity, body] : d.bodies) {
        if (body.type != Impl::Type::Kinematic) continue;
        const auto m = runtime::api::world_matrix(world, entity);
        const auto r = runtime::api::world_rotation(world, entity);
        if (m && r) bi.MoveKinematic(body.id, to_j(m->transform_point({0, 0, 0})), to_j(*r), static_cast<float>(dt));
    }
    // Characters move first (Jolt's recommended order), then the rigid bodies.
    const JPH::Vec3 gravity = d.system.GetGravity();
    const float step = static_cast<float>(dt);
    for (auto& [entity, c] : d.characters) {
        auto& ch = *c.controller;
        ch.UpdateGroundVelocity();
        const JPH::Vec3 ground = ch.GetGroundVelocity();
        const JPH::Vec3 vertical(0, ch.GetLinearVelocity().GetY(), 0);
        // The water where it stands (8D part 5): how deep at its feet and over its head, and so its state.
        const math::Vec3 feet = from_j(ch.GetPosition());
        CharacterWater water;
        math::Vec3 current{0, 0, 0};
        if (const auto s = world.water_at(feet.x, feet.z)) {
            water.feet_depth = s->height - feet.y;
            water.head_depth = s->height - (feet.y + c.height);
            current = s->flow;
        } else {
            water.feet_depth = water.head_depth = -1e3f;
        }
        const bool was_swimming = c.water.state == WaterState::Swimming || c.water.state == WaterState::Under;
        if (water.head_depth > 0.0f) water.state = WaterState::Under;
        else if (water.feet_depth >= c.swim_depth || (was_swimming && water.feet_depth >= c.swim_depth - 0.1f)) water.state = WaterState::Swimming;
        else if (water.feet_depth > 0.02f) water.state = WaterState::Wading;
        else water.state = WaterState::Dry;
        if (water.state != c.water.state) world.post({runtime::Event::Kind::Water, entity, entity, water_state_name(water.state)});
        c.water = water;
        const bool swimming = water.state == WaterState::Swimming || water.state == WaterState::Under;
        // Wading slows it the deeper it goes; swimming moves at swim_speed.
        float speed = 1.0f;
        if (water.state == WaterState::Wading) speed = 1.0f + (c.wade_speed - 1.0f) * std::clamp(water.feet_depth / c.swim_depth, 0.0f, 1.0f);
        else if (swimming) speed = c.swim_speed;
        // Jumping (2D part 2): a jump asked for waits up to jump_buffer for ground; ground counts up to coyote_time after
        // leaving it (once). With both 0 (the default) a jump happens only from the ground, at the next step, as before.
        const bool on_ground = ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && vertical.GetY() - ground.GetY() < 0.1f;
        if (on_ground) {
            c.since_ground = 0.0f;
            c.jumped = false;
        } else {
            c.since_ground += step;
        }
        if (c.jump > 0) {
            c.buffered_jump = std::max(c.buffered_jump, c.jump);
            c.buffer_left = c.jump_buffer;
        }
        c.jump = 0.0f;
        c.drop_left = std::max(0.0f, c.drop_left - step);
        const bool may_jump = on_ground || (!c.jumped && c.since_ground <= c.coyote_time + 1e-4f);
        bool jumping = false;
        JPH::Vec3 v;
        if (swimming) {
            // Held with its chest at the surface (feet swim_depth under it): pulled there, gently, no gravity.
            const float target = feet.y + (water.feet_depth - c.swim_depth);
            v = JPH::Vec3(0, std::clamp((target - feet.y) * 3.0f, -2.0f, 2.0f), 0);
        } else if (c.buffered_jump > 0 && may_jump) {
            // Jump: from the ground (moving with it), or just off a ledge (coyote time).
            v = (on_ground ? ground : JPH::Vec3::sZero()) + JPH::Vec3(0, c.buffered_jump, 0);
            jumping = true;
        } else if (on_ground) {
            v = ground;  // standing: move with what we stand on
        } else {
            v = vertical;  // in the air: keep falling (or rising)
            // Let go of jump while rising: keep jump_cut of the upward speed (once per jump).
            if (c.release && c.jumped && !c.cut && v.GetY() > 0.0f) {
                v.SetY(v.GetY() * c.jump_cut);
                c.cut = true;
            }
        }
        if (jumping) {
            c.buffered_jump = 0.0f;
            c.jumped = true;
            c.cut = false;
            c.since_ground = c.coyote_time + 1.0f;  // no second coyote jump
        } else if (c.buffered_jump > 0) {
            c.buffer_left -= step;
            if (c.buffer_left < 0.0f) c.buffered_jump = 0.0f;  // waited long enough (or there's no buffer)
        }
        c.release = false;
        // Horizontal: straight to the asked speed, or towards it at the character's acceleration.
        const math::Vec3 want{c.wanted.x * speed, 0.0f, c.plane ? 0.0f : c.wanted.z * speed};
        const float rate = !on_ground ? c.air_acceleration : (math::length(want) > 1e-4f ? c.acceleration : c.deceleration);
        if (rate <= 0.0f || swimming) {
            c.horizontal = want;
        } else {
            const math::Vec3 diff = want - c.horizontal;
            const float dl = math::length(diff), most = rate * step;
            c.horizontal = dl <= most ? want : c.horizontal + diff * (most / dl);
        }
        const JPH::Vec3 fall = swimming ? JPH::Vec3::sZero() : gravity * (step * c.gravity_scale);
        v += fall + JPH::Vec3(c.horizontal.x, 0, c.horizontal.z);
        // A river's current (8D part 6): a swimmer drifts with it, a wader is pushed by part of it (more the deeper).
        const float drift = swimming ? 1.0f : water.state == WaterState::Wading ? 0.5f * std::clamp(water.feet_depth / c.swim_depth, 0.0f, 1.0f) : 0.0f;
        v += JPH::Vec3(current.x * drift, 0, current.z * drift);
        ch.SetLinearVelocity(v);
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp = JPH::Vec3(0, c.step_height, 0);
        const JPH::IgnoreSingleBodyFilter self(ch.GetInnerBodyID());
        ch.ExtendedUpdate(step, gravity * c.gravity_scale, settings, d.system.GetDefaultBroadPhaseLayerFilter(layers::kMoving),
                          d.system.GetDefaultLayerFilter(layers::kMoving), self, {}, *d.temp);
        if (c.plane) {  // 2D: never a hair out of its plane
            JPH::RVec3 at = ch.GetPosition();
            if (at.GetZ() != c.plane_z) {
                at.SetZ(c.plane_z);
                ch.SetPosition(at);
            }
            JPH::Vec3 lv = ch.GetLinearVelocity();
            lv.SetZ(0.0f);
            ch.SetLinearVelocity(lv);
        }
        c.velocity = from_j(ch.GetLinearVelocity());
        if (auto* e = world.find(entity)) {
            const auto r = runtime::api::world_rotation(world, entity).value_or(math::Quat{});
            runtime::api::set_world_pose(world, entity, from_j(ch.GetPosition()), r);
            d.moved(entity, from_j(ch.GetPosition()), r, true);
            (void)e;
        }
    }
    d.float_bodies(world);
    d.system.Update(step, 1, d.temp.get(), d.jobs.get());
    // Dynamic bodies move their entities.
    for (const auto& [entity, body] : d.bodies) {
        if (body.type != Impl::Type::Dynamic || !bi.IsActive(body.id)) continue;
        JPH::RVec3 p;
        JPH::Quat q;
        bi.GetPositionAndRotation(body.id, p, q);
        runtime::api::set_world_pose(world, entity, from_j(p), from_j(q));
        d.moved(entity, from_j(p), from_j(q), false);
    }
    // Contact changes become events for both entities.
    std::vector<Impl::Listener::Change> changes;
    {
        std::scoped_lock lock(d.listener.mutex);
        changes.swap(d.listener.changes);
    }
    using K = runtime::Event::Kind;
    auto post = [&](const Guid& a, const Guid& b, bool entered) {
        const bool sensor = (d.bodies.contains(a) && d.bodies[a].sensor) || (d.bodies.contains(b) && d.bodies[b].sensor);
        const K kind = sensor ? (entered ? K::TriggerEnter : K::TriggerExit) : (entered ? K::CollisionEnter : K::CollisionExit);
        world.post({kind, a, b});
        world.post({kind, b, a});
    };
    // Asleep: every moving body of the pair is inactive (static bodies never are active).
    auto asleep = [&](const Guid& a, const Guid& b) {
        bool any_moving = false;
        for (const Guid& g : {a, b}) {
            const auto it = d.bodies.find(g);
            if (it == d.bodies.end()) return false;  // gone: a real exit
            if (it->second.type == Impl::Type::Static) continue;
            any_moving = true;
            if (bi.IsActive(it->second.id)) return false;
        }
        return any_moving;
    };
    for (const auto& c : changes) {
        const auto a = d.entity_for(c.a);
        const auto b = d.entity_for(c.b);
        if (!a || !b) continue;
        const auto key = std::minmax(*a, *b);
        if (c.entered) {
            if (d.sleeping_pairs.erase(key) > 0) continue;  // woke up still touching: nothing new
            post(*a, *b, true);
        } else if (asleep(*a, *b)) {
            d.sleeping_pairs[key] = true;
        } else {
            post(*a, *b, false);
        }
    }
    // Parked pairs whose bodies woke up without the contact coming back have separated.
    for (auto it = d.sleeping_pairs.begin(); it != d.sleeping_pairs.end();) {
        const auto& [a, b] = it->first;
        if (asleep(a, b)) {
            ++it;
            continue;
        }
        if (d.bodies.contains(a) && d.bodies.contains(b)) post(a, b, false);
        it = d.sleeping_pairs.erase(it);
    }
}

std::optional<RayHit> PhysicsSystem::raycast(math::Vec3 origin, math::Vec3 direction, float max_distance, std::optional<Guid> ignore) const {
    auto& d = *impl_;
    if (math::length(direction) < 1e-9f || max_distance <= 0) return std::nullopt;
    const math::Vec3 dir = math::normalize(direction);
    const JPH::RRayCast ray{to_j(origin), to_j(dir * max_distance)};
    JPH::RayCastResult hit;
    JPH::BodyID ignored;
    if (ignore) {
        if (const auto it = d.bodies.find(*ignore); it != d.bodies.end()) ignored = it->second.id;
        // A character's body is its controller's inner body (not in `bodies`): a shot from the
        // player's eyes hit the player at distance 0 until this was here.
        else if (const auto c = d.characters.find(*ignore); c != d.characters.end()) ignored = c->second.controller->GetInnerBodyID();
    }
    const JPH::IgnoreSingleBodyFilter filter(ignored);
    if (!d.system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return std::nullopt;
    RayHit out;
    out.distance = hit.mFraction * max_distance;
    out.point = origin + dir * out.distance;
    const JPH::BodyLockRead lock(d.system.GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) out.normal = from_j(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, to_j(out.point)));
    const auto entity = d.entity_for(hit.mBodyID);
    if (!entity) return std::nullopt;
    out.entity = *entity;
    return out;
}

bool PhysicsSystem::has_body(const Guid& entity) const { return impl_->bodies.contains(entity); }

std::optional<math::Vec3> PhysicsSystem::velocity(const Guid& entity) const {
    const auto it = impl_->bodies.find(entity);
    if (it == impl_->bodies.end()) return std::nullopt;
    return from_j(impl_->system.GetBodyInterface().GetLinearVelocity(it->second.id));
}

void PhysicsSystem::set_velocity(const Guid& entity, math::Vec3 v) {
    const auto it = impl_->bodies.find(entity);
    if (it == impl_->bodies.end() || it->second.type == Impl::Type::Static) return;
    impl_->system.GetBodyInterface().SetLinearVelocity(it->second.id, to_j(v));
}

void PhysicsSystem::add_impulse(const Guid& entity, math::Vec3 impulse) {
    const auto it = impl_->bodies.find(entity);
    if (it == impl_->bodies.end() || it->second.type != Impl::Type::Dynamic) return;
    impl_->system.GetBodyInterface().AddImpulse(it->second.id, to_j(impulse));
}

void PhysicsSystem::add_force(const Guid& entity, math::Vec3 force) {
    const auto it = impl_->bodies.find(entity);
    if (it == impl_->bodies.end() || it->second.type != Impl::Type::Dynamic) return;
    auto& bi = impl_->system.GetBodyInterface();
    bi.AddForce(it->second.id, to_j(force));
    bi.ActivateBody(it->second.id);
}

std::size_t PhysicsSystem::body_count() const { return impl_->bodies.size(); }

bool PhysicsSystem::has_character(const Guid& entity) const { return impl_->characters.contains(entity); }

void PhysicsSystem::character_move(const Guid& entity, math::Vec3 velocity) {
    if (const auto it = impl_->characters.find(entity); it != impl_->characters.end()) it->second.wanted = {velocity.x, 0, velocity.z};
}

void PhysicsSystem::character_jump(const Guid& entity, float speed) {
    if (const auto it = impl_->characters.find(entity); it != impl_->characters.end()) it->second.jump = std::max(it->second.jump, speed);
}

std::optional<PhysicsSystem::CharacterWater> PhysicsSystem::character_water(const Guid& entity) const {
    const auto it = impl_->characters.find(entity);
    if (it == impl_->characters.end()) return std::nullopt;
    return it->second.water;
}

const char* PhysicsSystem::water_state_name(WaterState s) noexcept {
    switch (s) {
    case WaterState::Dry: return "dry";
    case WaterState::Wading: return "wading";
    case WaterState::Swimming: return "swimming";
    case WaterState::Under: return "under";
    }
    return "dry";
}

void PhysicsSystem::character_jump_release(const Guid& entity) {
    if (const auto it = impl_->characters.find(entity); it != impl_->characters.end()) it->second.release = true;
}

void PhysicsSystem::character_drop(const Guid& entity) {
    if (const auto it = impl_->characters.find(entity); it != impl_->characters.end()) it->second.drop_left = 0.25f;
}

std::optional<math::Vec3> PhysicsSystem::character_velocity(const Guid& entity) const {
    const auto it = impl_->characters.find(entity);
    if (it == impl_->characters.end()) return std::nullopt;
    return it->second.velocity;
}

// Built-in platformer controls (2D part 2): reads the input each frame and steers the character, as a script would.
void PhysicsSystem::update(runtime::World& world, double) {
    auto& d = *impl_;
    for (auto& [entity, c] : d.characters) {
        const Json* pc = api::component(world, entity, "platformer");
        if (!pc || !pc->is_object()) continue;
        const float speed = std::max(0.0f, number(*pc, "speed", 6.0f));
        const float jump_height = std::max(0.0f, number(*pc, "jump_height", 1.5f));
        const float x = api::input_axis(world, pc->value("axis", std::string("move_x")));
        const std::string jump = pc->value("jump", std::string("jump"));
        if (std::fabs(x) > 0.01f) c.wanted = {x * speed, 0.0f, 0.0f};  // keys walk it; without them a script may
        if (api::input_pressed(world, jump)) {
            if (api::input_axis(world, pc->value("down", std::string("move_y"))) < -0.5f) {
                c.drop_left = 0.25f;  // down + jump: through a one-way platform
            } else {
                // The speed that rises exactly jump_height in fixed steps of dt. The textbook v = sqrt(2 g h) peaks
                // short by about v dt / 2 (each step moves with the speed at its end), 4% at 60 Hz for a 1.5 m jump;
                // the stepped apex is v^2 / 2g - v dt / 2 = h, so v = g dt / 2 + sqrt((g dt / 2)^2 + 2 g h).
                const float g = std::max(0.0f, -d.system.GetGravity().GetY() * c.gravity_scale);
                const float half = g * static_cast<float>(world.fixed_dt) * 0.5f;
                c.jump = std::max(c.jump, half + std::sqrt(half * half + 2.0f * g * jump_height));
            }
        }
        if (api::input_released(world, jump)) c.release = true;
        // Face the way it moves (its sprite's flip_x), and play the sprite animation for what it's doing: from how it
        // actually moves, so a script steering it (or something pushing it) looks right too.
        const float vx = std::fabs(x) > 0.01f ? x : c.velocity.x;
        if (vx < -0.1f) c.facing_left = true;
        else if (vx > 0.1f) c.facing_left = false;
        if (const Json* sprite = api::component(world, entity, "sprite"); sprite && sprite->is_object() && sprite->value("flip_x", false) != c.facing_left) {
            Json s = *sprite;
            s["flip_x"] = c.facing_left;
            api::set_component(world, entity, "sprite", std::move(s));
        }
        if (const auto anims = pc->find("animations"); anims != pc->end() && anims->is_object()) {
            const bool grounded = c.controller->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            const char* state = !grounded ? (c.velocity.y > 0.0f ? "jump" : "fall") : (std::fabs(c.velocity.x) > 0.3f ? "run" : "idle");
            const std::string name = anims->value(state, std::string());
            if (!name.empty() && name != c.animation && api::play_animation(world, entity, name)) c.animation = name;
        }
    }
}

bool PhysicsSystem::is_grounded(const Guid& entity) const {
    const auto it = impl_->characters.find(entity);
    return it != impl_->characters.end() && it->second.controller->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
}

void PhysicsSystem::late_update(runtime::World& world, double) {
    auto& d = *impl_;
    for (auto& [entity, c] : d.characters) c.wanted = {0, 0, 0};  // scripts steer every frame
    // Where to draw what physics moves: between its last two steps (it lags one step behind, the
    // usual price of smooth motion). Without this, motion stutters whenever a frame gets no step or
    // two (frame times wobble around 1/60 s even on a 60 Hz screen).
    const float alpha = static_cast<float>(std::clamp(world.fixed_alpha(), 0.0, 1.0));
    for (const auto& [entity, pose] : d.poses) {
        const auto m = runtime::api::world_matrix(world, entity);
        if (!m) continue;
        const math::Vec3 scale{math::length(math::transform_vector(*m, {1, 0, 0})), math::length(math::transform_vector(*m, {0, 1, 0})),
                               math::length(math::transform_vector(*m, {0, 0, 1}))};
        const math::Quat rot = pose.character ? runtime::api::world_rotation(world, entity).value_or(pose.curr_rot)
                                              : math::nlerp(pose.prev_rot, pose.curr_rot, alpha);
        world.set_render_pose(entity, math::compose(pose.prev + (pose.curr - pose.prev) * alpha, rot, scale));
    }
}

} // namespace noor::physics
