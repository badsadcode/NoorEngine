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
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
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
    struct Body {
        JPH::BodyID id;
        Type type;
        bool sensor;
    };
    std::unordered_map<Guid, Body> bodies;
    struct Character {
        JPH::Ref<JPH::CharacterVirtual> controller;
        float step_height = 0.4f;
        math::Vec3 wanted{0, 0, 0};  // horizontal velocity this frame
        float jump = 0.0f;           // pending jump speed
    };
    std::unordered_map<Guid, Character> characters;
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
    }

    ~Impl() {
        characters.clear();  // removes their inner bodies
        auto& bi = system.GetBodyInterface();
        for (auto& [entity, body] : bodies) {
            bi.RemoveBody(body.id);
            bi.DestroyBody(body.id);
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
            if (!e || !e->has("mesh")) continue;
            auto tris = options.mesh_triangles(e->components["mesh"]);
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
        } else if (shape == "capsule") {
            const float radius = std::max(0.005f, number(collider, "radius", 0.3f) * std::max(scale.x, scale.z));
            const float height = number(collider, "height", 1.8f) * scale.y;
            result = JPH::CapsuleShapeSettings(std::max(0.001f, height * 0.5f - radius), radius).Create();
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
            error = std::format("unknown collider shape \"{}\" (box, sphere, capsule, mesh)", shape);
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
        d.characters[entity.id] = std::move(c);
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
    settings.mGravityFactor = number(rb, "gravity_scale", 1.0f);
    settings.mIsSensor = c->value("trigger", false);
    if (settings.mIsSensor && type != Impl::Type::Dynamic) settings.mCollideKinematicVsNonDynamic = true;
    if (type == Impl::Type::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(0.001f, number(rb, "mass", 1.0f));
    }
    d.entity_of.push_back(entity.id);
    settings.mUserData = d.entity_of.size();
    auto& bi = d.system.GetBodyInterface();
    const JPH::BodyID id = bi.CreateAndAddBody(settings, type == Impl::Type::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        NOOR_LOG_ERROR("Physics", "'{}': too many physics bodies", entity.name);
        return;
    }
    d.bodies[entity.id] = {id, type, settings.mIsSensor};
}

void PhysicsSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) {
    auto& d = *impl_;
    d.characters.erase(entity.id);
    d.poses.erase(entity.id);
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
        JPH::Vec3 v;
        if (ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && vertical.GetY() - ground.GetY() < 0.1f) {
            v = ground;  // standing: move with what we stand on
            if (c.jump > 0) v += JPH::Vec3(0, c.jump, 0);
        } else {
            v = vertical;  // in the air: keep falling (or rising)
        }
        c.jump = 0.0f;
        v += gravity * step + JPH::Vec3(c.wanted.x, 0, c.wanted.z);
        ch.SetLinearVelocity(v);
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp = JPH::Vec3(0, c.step_height, 0);
        const JPH::IgnoreSingleBodyFilter self(ch.GetInnerBodyID());
        ch.ExtendedUpdate(step, gravity, settings, d.system.GetDefaultBroadPhaseLayerFilter(layers::kMoving),
                          d.system.GetDefaultLayerFilter(layers::kMoving), self, {}, *d.temp);
        if (auto* e = world.find(entity)) {
            const auto r = runtime::api::world_rotation(world, entity).value_or(math::Quat{});
            runtime::api::set_world_pose(world, entity, from_j(ch.GetPosition()), r);
            d.moved(entity, from_j(ch.GetPosition()), r, true);
            (void)e;
        }
    }
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
