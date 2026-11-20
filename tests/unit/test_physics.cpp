// Physics (Jolt, ADR-010) as a runtime system: falling, resting, bouncing, triggers, queries,
// kinematic bodies, mesh colliders, parented bodies and Lua collision hooks.
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>

using namespace noor;
using asset::Json;
using physics::PhysicsSystem;

namespace {

scene::Entity body(std::string name, math::Vec3 pos, Json collider, Json rigidbody = nullptr) {
    scene::Entity e;
    e.id = Guid::generate();
    e.name = std::move(name);
    e.transform.position = pos;
    e.components["collider"] = std::move(collider);
    if (!rigidbody.is_null()) e.components["rigidbody"] = std::move(rigidbody);
    return e;
}

scene::Entity floor_entity() { return body("Floor", {0, -0.5f, 0}, {{"shape", "box"}, {"size", {40, 1, 40}}}); }

// Events live for one frame (cleared after late_update), so record them from inside the frame.
struct EventRecorder final : runtime::System {
    std::vector<runtime::Event> events;
    std::string_view name() const override { return "recorder"; }
    void late_update(runtime::World& w, double) override { events.insert(events.end(), w.events().begin(), w.events().end()); }
    std::vector<runtime::Event> for_entity(const Guid& id) const {
        std::vector<runtime::Event> out;
        for (const auto& e : events)
            if (e.entity == id) out.push_back(e);
        return out;
    }
};

struct Sim {
    runtime::World world;
    PhysicsSystem* physics;
    EventRecorder* recorder;
    explicit Sim(scene::Scene s, PhysicsSystem::Options options = {}) : world(std::move(s)) {
        auto p = std::make_unique<PhysicsSystem>(std::move(options));
        physics = p.get();
        world.add_system(std::move(p));
        auto r = std::make_unique<EventRecorder>();
        recorder = r.get();
        world.add_system(std::move(r));
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) world.frame(1.0 / 60.0, {});
    }
    math::Vec3 pos(const scene::Entity& e) { return world.find(e.id)->transform.position; }
};

int count(const std::vector<runtime::Event>& all, runtime::Event::Kind kind) {
    return static_cast<int>(std::count_if(all.begin(), all.end(), [&](const auto& e) { return e.kind == kind; }));
}

} // namespace

TEST_CASE("physics: a dynamic crate falls onto a static floor and rests on it") {
    scene::Scene s;
    s.entities = {floor_entity(), body("Crate", {0, 5, 0}, {{"shape", "box"}, {"size", {1, 1, 1}}}, {{"type", "dynamic"}, {"mass", 10}})};
    const auto crate = s.entities[1];
    Sim sim(s);
    sim.world.frame(1.0 / 60.0, {});
    CHECK(sim.physics->body_count() == 2);
    sim.run(0.5);
    CHECK(sim.pos(crate).y < 4.5f);  // falling
    sim.run(3.0);
    CHECK(sim.pos(crate).y == doctest::Approx(0.5f).epsilon(0.02));  // resting on the floor (top at y = 0)
    const auto v = sim.physics->velocity(crate.id);
    REQUIRE(v);
    CHECK(math::length(*v) < 0.05f);
}

TEST_CASE("physics: one collision-enter event per contact, none when the crate goes to sleep") {
    scene::Scene s;
    s.entities = {floor_entity(), body("Crate", {0, 2, 0}, {{"shape", "box"}}, {{"type", "dynamic"}})};
    const auto floor = s.entities[0];
    const auto crate = s.entities[1];
    Sim sim(s);
    sim.run(4.0);  // lands, settles, sleeps
    const auto seen = sim.recorder->for_entity(crate.id);
    CHECK(count(seen, runtime::Event::Kind::CollisionEnter) == 1);
    CHECK(count(seen, runtime::Event::Kind::CollisionExit) == 0);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen[0].other == floor.id);

    // Woken by a kick: it leaves the floor once and lands once.
    sim.physics->add_impulse(crate.id, {0, 6, 0});
    sim.run(2.0);
    const auto after = sim.recorder->for_entity(crate.id);
    CHECK(count(after, runtime::Event::Kind::CollisionExit) == 1);
    CHECK(count(after, runtime::Event::Kind::CollisionEnter) == 2);
}

TEST_CASE("physics: raycasts report entity, point, normal and distance, ignore skips one entity") {
    scene::Scene s;
    s.entities = {floor_entity(), body("Crate", {0, 0.5f, 0}, {{"shape", "box"}}, {{"type", "dynamic"}})};
    const auto floor = s.entities[0];
    const auto crate = s.entities[1];
    Sim sim(s);
    sim.run(0.2);
    const auto hit = sim.physics->raycast({0, 10, 0}, {0, -1, 0}, 100);
    REQUIRE(hit);
    CHECK(hit->entity == crate.id);
    CHECK(hit->point.y == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(hit->distance == doctest::Approx(9.0f).epsilon(0.02));
    CHECK(hit->normal.y == doctest::Approx(1.0f).epsilon(0.01));
    const auto past = sim.physics->raycast({0, 10, 0}, {0, -1, 0}, 100, crate.id);
    REQUIRE(past);
    CHECK(past->entity == floor.id);
    CHECK_FALSE(sim.physics->raycast({0, 10, 0}, {0, 1, 0}, 100));  // nothing above
}

TEST_CASE("physics: restitution bounces, impulses launch, triggers detect without stopping") {
    scene::Scene s;
    s.entities = {floor_entity(),
                  body("Ball", {0, 4, 0}, {{"shape", "sphere"}, {"radius", 0.25}}, {{"type", "dynamic"}, {"restitution", 0.8}}),
                  body("Gate", {5, 2, 0}, {{"shape", "box"}, {"size", {2, 0.5, 2}}, {"trigger", true}}),
                  body("Pebble", {5, 6, 0}, {{"shape", "sphere"}, {"radius", 0.1}}, {{"type", "dynamic"}})};
    const auto ball = s.entities[1];
    const auto gate = s.entities[2];
    const auto pebble = s.entities[3];
    Sim sim(s);
    float highest_after_bounce = 0;
    bool bounced = false;
    for (int i = 0; i < 60 * 3; ++i) {
        sim.world.frame(1.0 / 60.0, {});
        const float y = sim.pos(ball).y;
        if (y < 0.4f) bounced = true;
        if (bounced) highest_after_bounce = std::max(highest_after_bounce, y);
    }
    const auto gate_events = sim.recorder->for_entity(gate.id);
    CHECK(highest_after_bounce > 1.5f);  // bounced back up a good way
    CHECK(count(gate_events, runtime::Event::Kind::TriggerEnter) == 1);
    CHECK(count(gate_events, runtime::Event::Kind::TriggerExit) == 1);
    CHECK(sim.pos(pebble).y < 0.5f);  // fell through the trigger to the floor

    sim.run(3.0);  // let the ball settle, then kick it up
    sim.physics->add_impulse(ball.id, {0, 5, 0});  // 1 kg: 5 m/s up
    sim.run(0.2);
    CHECK(sim.pos(ball).y > 0.6f);
}

TEST_CASE("physics: kinematic bodies follow their entity and push dynamic bodies") {
    scene::Scene s;
    s.entities = {floor_entity(),
                  body("Pusher", {-3, 0.5f, 0}, {{"shape", "box"}, {"size", {0.5, 1, 2}}}, {{"type", "kinematic"}}),
                  body("Crate", {0, 0.5f, 0}, {{"shape", "box"}}, {{"type", "dynamic"}, {"friction", 0.2}})};
    const auto pusher = s.entities[1];
    const auto crate = s.entities[2];
    Sim sim(s);
    for (int i = 0; i < 120; ++i) {  // the pusher slides +X at 3 m/s for 2 s
        runtime::api::translate(sim.world, pusher.id, {3.0f / 60.0f, 0, 0}, false);
        sim.world.frame(1.0 / 60.0, {});
    }
    CHECK(sim.pos(pusher).x == doctest::Approx(3.0f).epsilon(0.01));
    CHECK(sim.pos(crate).x > 2.5f);  // shoved along
}

TEST_CASE("physics: a mesh collider from the host's triangles, dynamic mesh colliders are refused") {
    scene::Scene s;
    auto ground = body("Ground", {0, 0, 0}, {{"shape", "mesh"}});
    ground.components["mesh"] = {{"asset", "test"}, {"mesh_index", 0}};
    s.entities = {ground, body("Ball", {1, 3, 1}, {{"shape", "sphere"}, {"radius", 0.5}}, {{"type", "dynamic"}}),
                  body("Bad", {9, 9, 9}, {{"shape", "mesh"}}, {{"type", "dynamic"}})};
    const auto ball = s.entities[1];
    PhysicsSystem::Options options;
    options.mesh_triangles = [](const Json&) {
        physics::Triangles t;  // a 20 x 20 m quad at y = 0, facing up
        t.positions = {{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}};
        t.indices = {0, 2, 1, 0, 3, 2};
        return std::optional(t);
    };
    Sim sim(s, options);
    sim.run(3.0);
    CHECK(sim.physics->body_count() == 2);  // "Bad" was refused (logged), the rest work
    CHECK(sim.pos(ball).y == doctest::Approx(0.5f).epsilon(0.03));
}

TEST_CASE("physics: a dynamic child of a moved parent keeps correct world placement") {
    scene::Scene s;
    scene::Entity parent;
    parent.id = Guid::generate();
    parent.name = "Parent";
    parent.transform.position = {10, 0, 0};
    auto child = body("Child", {0, 3, 0}, {{"shape", "box"}}, {{"type", "dynamic"}});
    child.parent = parent.id;
    s.entities = {floor_entity(), parent, child};
    Sim sim(s);
    sim.run(3.0);
    const auto world = runtime::api::world_position(sim.world, child.id);
    REQUIRE(world);
    CHECK(world->x == doctest::Approx(10.0f).epsilon(0.01));
    CHECK(world->y == doctest::Approx(0.5f).epsilon(0.03));
    CHECK(sim.pos(child).x == doctest::Approx(0.0f).epsilon(0.01));  // local stays relative to the parent
}

TEST_CASE("physics + lua: collision hooks fire and scripts can raycast and push") {
    scene::Scene s;
    auto ball = body("Ball", {0, 3, 0}, {{"shape", "sphere"}, {"radius", 0.5}}, {{"type", "dynamic"}});
    ball.components["script"] = {{"file", "Scripts/ball.luau"}};
    s.entities = {floor_entity(), ball};
    runtime::World world(s);
    world.add_system(std::make_unique<PhysicsSystem>(PhysicsSystem::Options{}));
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
    auto* lua_ptr = lua.get();
    lua->add_source("Scripts/ball.luau", R"(
        local Ball = {}
        function Ball:on_start()
            local hit = noor.raycast(vector.create(0, 10, 0), vector.create(0, -1, 0), 50, self.entity)
            self.entity:set("probe", { what = hit.entity:name(), y = hit.point.y })
        end
        function Ball:on_collision_enter(other)
            self.entity:set("landed", { on = other:name(), at = noor.time() })
            self.entity:add_impulse(vector.create(0, 3, 0))
        end
        return Ball
    )");
    world.add_system(std::move(lua));
    for (int i = 0; i < 60 * 2; ++i) world.frame(1.0 / 60.0, {});
    CHECK(lua_ptr->errors().empty());
    const auto* probe = runtime::api::component(world, ball.id, "probe");
    REQUIRE(probe);
    CHECK((*probe)["what"] == "Floor");
    CHECK((*probe)["y"].get<double>() == doctest::Approx(0.0).epsilon(0.02));
    const auto* landed = runtime::api::component(world, ball.id, "landed");
    REQUIRE(landed);
    CHECK((*landed)["on"] == "Floor");
}

// ---- 2D physics (9-2D part 1, ADR-040): Jolt with bodies locked to the XY plane, 2D shapes as slabs -----------------

namespace {

// A tumbling pile of 2D shapes dropped onto a tilted floor and against a wall: rectangles, circles and triangles, some
// at z = 0.3 (bodies keep whatever depth they start at). `plane` false: the same pile, free in 3D (the control).
scene::Scene pile_2d(bool plane) {
    scene::Scene s;
    auto ground = body("Ground", {0, 0, 0}, {{"shape", "rectangle"}, {"size", {30, 1}}});
    ground.transform.rotation = math::Quat::from_axis_angle({0, 0, 1}, math::radians(-12.0f));
    auto wall = body("Wall", {9, 4, 0}, {{"shape", "rectangle"}, {"size", {1, 10}}});
    s.entities = {ground, wall};
    Json rb{{"type", "dynamic"}};
    if (plane) rb["plane"] = "xy";
    for (int i = 0; i < 18; ++i) {
        const float x = -6.0f + static_cast<float>(i % 6) * 1.3f, y = 3.0f + static_cast<float>(i / 6) * 1.6f;
        const float z = (i % 4 == 0) ? 0.3f : 0.0f;
        Json c;
        if (i % 3 == 0) c = {{"shape", "rectangle"}, {"size", {0.9, 0.6}}};
        else if (i % 3 == 1) c = {{"shape", "circle"}, {"radius", 0.4}};
        else c = {{"shape", "polygon"}, {"points", {{-0.5, -0.3}, {0.5, -0.3}, {0.1, 0.5}}}};
        auto e = body("Piece " + std::to_string(i), {x, y, z}, c, rb);
        e.transform.rotation = math::Quat::from_axis_angle({0, 0, 1}, 0.3f * static_cast<float>(i));
        s.entities.push_back(e);
    }
    return s;
}

} // namespace

TEST_CASE("physics 2D: bodies locked to the plane stay in it to the bit and turn only about Z") {
    Sim locked(pile_2d(true));
    locked.run(8.0);
    int moved_in_plane = 0;
    for (const auto& e : locked.world.scene().entities) {
        if (!e.name.starts_with("Piece")) continue;
        const int i = std::stoi(e.name.substr(6));
        CHECK(e.transform.position.z == ((i % 4 == 0) ? 0.3f : 0.0f));  // exactly: not a micrometre out of its plane
        CHECK(std::fabs(e.transform.rotation.x) < 1e-6f);
        CHECK(std::fabs(e.transform.rotation.y) < 1e-6f);
        if (e.transform.position.y < 3.0f + static_cast<float>(i / 6) * 1.6f - 0.5f) ++moved_in_plane;  // fell from its start
    }
    CHECK(moved_in_plane == 18);  // everything fell and tumbled

    // The control: the same pile free in 3D leaves the plane (so the lock is what keeps it there).
    Sim free(pile_2d(false));
    free.run(8.0);
    float most = 0;
    for (const auto& e : free.world.scene().entities)
        if (e.name.starts_with("Piece")) most = std::max(most, std::fabs(e.transform.position.z - ((std::stoi(e.name.substr(6)) % 4 == 0) ? 0.3f : 0.0f)));
    CHECK(most > 0.01f);
}

TEST_CASE("physics 2D: the same pile twice lands the same, to the bit") {
    Sim a(pile_2d(true)), b(pile_2d(true));
    a.run(6.0);
    b.run(6.0);
    const auto& ea = a.world.scene().entities;
    const auto& eb = b.world.scene().entities;
    REQUIRE(ea.size() == eb.size());
    for (std::size_t i = 0; i < ea.size(); ++i) {
        CHECK(ea[i].transform.position.x == eb[i].transform.position.x);
        CHECK(ea[i].transform.position.y == eb[i].transform.position.y);
        CHECK(ea[i].transform.rotation.z == eb[i].transform.rotation.z);
    }
}

TEST_CASE("physics 2D: a circle rolls down a slope like a disc: a = 2/3 g sin(angle)") {
    const float angle = math::radians(20.0f);
    scene::Scene s;
    auto slope = body("Slope", {0, 0, 0}, {{"shape", "rectangle"}, {"size", {60, 1}}});
    slope.transform.rotation = math::Quat::from_axis_angle({0, 0, 1}, -angle);
    // On the slope's top face (0.5 up from its middle line), 0.5 m radius, near the high end.
    const math::Vec3 down{std::cos(angle), -std::sin(angle), 0}, up_normal{std::sin(angle), std::cos(angle), 0};
    const math::Vec3 start = down * -20.0f + up_normal * (0.5f + 0.5f + 0.001f);
    auto wheel = body("Wheel", start, {{"shape", "circle"}, {"radius", 0.5}},
                      {{"type", "dynamic"}, {"plane", "xy"}, {"linear_damping", 0}, {"angular_damping", 0}, {"friction", 0.8}});
    s.entities = {slope, wheel};
    Sim sim(s);
    std::vector<float> along;  // distance down the slope, frame by frame
    for (int f = 0; f < 150; ++f) {
        sim.world.frame(1.0 / 60.0, {});
        along.push_back(math::dot(sim.pos(wheel) - start, down));
    }
    // From 0.5 s to 2.5 s: speed from the distance covered over a frame, at both ends.
    const auto speed = [&](int f) { return (along[static_cast<std::size_t>(f)] - along[static_cast<std::size_t>(f - 1)]) * 60.0f; };
    const float a = (speed(149) - speed(30)) / (119.0f / 60.0f);
    const float disc = 2.0f / 3.0f * 9.81f * std::sin(angle), sphere = 5.0f / 7.0f * 9.81f * std::sin(angle);
    MESSAGE("rolling: " << a << " m/s2 (disc " << disc << ", a ball would be " << sphere << ")");
    CHECK(a == doctest::Approx(disc).epsilon(0.015));
    // Rolling, not sliding: it turned about Z, by the distance over its radius.
    CHECK(std::fabs(sim.world.find(wheel.id)->transform.rotation.z) > 0.1f);
}

TEST_CASE("physics 2D: a polygon lands and rests on a side, locks work in 3D too, bad polygons are refused") {
    scene::Scene s;
    s.entities.push_back(body("Ground", {0, -0.5f, 0}, {{"shape", "rectangle"}, {"size", {20, 1}}}));
    // A right triangle dropped point-down falls over onto one of its sides.
    auto tri = body("Triangle", {0, 3, 0}, {{"shape", "polygon"}, {"points", {{0, 0}, {1.2, 0}, {0, 0.9}}}}, {{"type", "dynamic"}, {"plane", "xy"}});
    tri.transform.rotation = math::Quat::from_axis_angle({0, 0, 1}, math::radians(200.0f));
    // A tall 3D box dropped half onto a step, with its X and Z turning locked: unlocked it would topple off the step's
    // edge; locked it can only turn about Y, so it stands on the edge, upright.
    s.entities.push_back(body("Step", {5.3f, 0.25f, 0}, {{"shape", "box"}, {"size", {0.4, 0.5, 0.4}}}));
    auto post = body("Post", {5, 2.5f, 0}, {{"shape", "box"}, {"size", {0.4, 2, 0.4}}}, {{"type", "dynamic"}, {"lock_rotation", {"x", "z"}}});
    auto bad = body("Bad", {-5, 2, 0}, {{"shape", "polygon"}, {"points", {{0, 0}, {1, 0}}}}, {{"type", "dynamic"}});
    s.entities.push_back(tri);
    s.entities.push_back(post);
    s.entities.push_back(bad);
    Sim sim(s);
    sim.run(6.0);
    // Resting on a side: its origin is the right-angle corner, so it's on the ground (a leg down) or the hypotenuse's
    // height above it (1.2 x 0.9 / 1.5 = 0.72 m, the hypotenuse down), give or take Jolt's convex radius.
    const float cy = sim.pos(tri).y;
    MESSAGE("triangle rests with its origin at y " << cy);
    CHECK((std::fabs(cy) < 0.03f || std::fabs(cy - 0.72f) < 0.03f));
    const auto r = sim.world.find(tri.id)->transform.rotation;
    CHECK(std::fabs(r.x) < 1e-6f);
    CHECK(std::fabs(r.y) < 1e-6f);
    // Landed and still: no change over a further second.
    const math::Vec3 before = sim.pos(tri);
    sim.run(1.0);
    CHECK(math::length(sim.pos(tri) - before) < 1e-3f);
    // The post stands on the step's edge: the step's 0.5 m plus half its height, and no tilt.
    CHECK(sim.pos(post).y == doctest::Approx(1.5f).epsilon(0.02));
    const auto pr = sim.world.find(post.id)->transform.rotation;
    CHECK(std::fabs(pr.x) < 1e-6f);
    CHECK(std::fabs(pr.z) < 1e-6f);
    // Two points aren't a polygon: no body, so it hangs where it was put.
    CHECK(sim.pos(bad).y == 2.0f);
}
