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

// ---- 2D characters (2D foundations part 2, ADR-040): a platformer's controller, driven by keys like a player ------

namespace {

using runtime::Key;

// A platformer character: 0.3 m wide, 1.1 m tall, gravity x3 (platformers fall fast), the built-in controls.
scene::Entity hero(math::Vec3 at, Json extra_character = Json::object(), Json platformer = Json::object()) {
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Hero";
    e.transform.position = at;
    Json ch{{"radius", 0.3}, {"height", 1.1}, {"plane", "xy"}, {"gravity_scale", 3}, {"coyote_time", 0.1}, {"jump_buffer", 0.1}};
    for (auto& [k, v] : extra_character.items()) ch[k] = v;
    e.components["character"] = ch;
    Json pc{{"speed", 6}, {"jump_height", 1.5}};
    for (auto& [k, v] : platformer.items()) pc[k] = v;
    e.components["platformer"] = pc;
    return e;
}

scene::Entity block(std::string name, math::Vec3 centre, float w, float h, bool one_way = false) {
    Json c{{"shape", "rectangle"}, {"size", {w, h}}};
    if (one_way) c["one_way"] = true;
    return body(std::move(name), centre, c);
}

// Steps the world a frame at a time with these keys held.
struct Pad {
    Sim& sim;
    runtime::InputState keys;
    void hold(Key k, bool on = true) { keys.set(k, on); }
    void frames(int n) {
        for (int i = 0; i < n; ++i) sim.world.frame(1.0 / 60.0, keys);
    }
};

} // namespace

TEST_CASE("platformer: walks along X only, up a slope, and never leaves its plane") {
    scene::Scene s;
    s.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
    auto slope = block("Slope", {8, 0, 0}, 8, 1);
    slope.transform.rotation = math::Quat::from_axis_angle({0, 0, 1}, math::radians(20.0f));
    s.entities.push_back(slope);
    const auto h = hero({0, 0.01f, 0.2f});
    s.entities.push_back(h);
    Sim sim(s);
    Pad pad{sim};
    pad.frames(10);  // settle
    pad.hold(Key::D);
    pad.hold(Key::W);  // W is move_y, which a platformer ignores: no walking "into" the screen
    float highest = 0.0f;
    for (int i = 0; i < 150; ++i) {
        pad.frames(1);
        highest = std::max(highest, sim.pos(h).y);
        CHECK(sim.pos(h).z == 0.2f);  // exactly in its plane, every step
    }
    const math::Vec3 p = sim.pos(h);
    MESSAGE("after 2.5 s walking right: " << p.x << ", " << p.y << ", " << p.z << " (highest " << highest << ")");
    CHECK(p.x > 12.0f);      // walked right, over the slope and off its far end
    CHECK(highest > 1.5f);   // up the slope (its top is about 1.8 m up)
}

TEST_CASE("platformer: the jump peaks at jump_height, a tap jumps lower, and speed builds at the acceleration") {
    scene::Scene s;
    s.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
    const auto h = hero({0, 0.01f, 0}, {{"acceleration", 30}, {"deceleration", 40}});
    s.entities.push_back(h);
    Sim sim(s);
    Pad pad{sim};
    pad.frames(20);
    const float ground = sim.pos(h).y;
    // Held: up to jump_height (1.5 m), to the millimetre (the jump speed is solved for 60 Hz steps).
    pad.hold(Key::Space);
    float top = ground;
    for (int i = 0; i < 60; ++i) {
        pad.frames(1);
        top = std::max(top, sim.pos(h).y);
    }
    pad.hold(Key::Space, false);
    pad.frames(60);
    MESSAGE("held jump: " << top - ground << " m");
    CHECK(top - ground == doctest::Approx(1.5f).epsilon(0.01));
    // Tapped (let go after 4 frames): jump_cut halves the rise speed, so a much lower jump.
    pad.hold(Key::Space);
    pad.frames(4);
    pad.hold(Key::Space, false);
    float tap = ground;
    for (int i = 0; i < 60; ++i) {
        pad.frames(1);
        tap = std::max(tap, sim.pos(h).y);
    }
    MESSAGE("tapped jump: " << tap - ground << " m");
    CHECK(tap - ground < 0.6f * 1.5f);  // a tap: a little over half the full jump
    CHECK(tap - ground > 0.2f);
    pad.frames(60);
    // Acceleration 30 m/s2: from standing to 6 m/s in 0.2 s (12 steps), not at once.
    const float x0 = sim.pos(h).x;
    pad.hold(Key::D);
    pad.frames(1);
    pad.frames(1);
    const float early = (sim.pos(h).x - x0);
    pad.frames(20);
    const float before = sim.pos(h).x;
    pad.frames(1);
    const float full = (sim.pos(h).x - before) * 60.0f;
    MESSAGE("moved " << early << " m in two steps; then " << full << " m/s");
    CHECK(early < 2.0f * 6.0f / 60.0f * 0.5f);  // well under two steps at full speed
    CHECK(full == doctest::Approx(6.0f).epsilon(0.02));
}

TEST_CASE("platformer: coyote time and jump buffering forgive a late or early press") {
    // A ledge: floor to x = 2, a pit, floor again from x = 12 (far: the run off the ledge falls into the pit).
    const auto course = [] {
        scene::Scene s;
        s.entities.push_back(block("Left", {-8, -0.5f, 0}, 20, 1));
        s.entities.push_back(block("Pit floor", {7, -6.5f, 0}, 10, 1));
        return s;
    };
    // Runs right off the ledge and presses jump `late` frames after the feet leave it: does it jump (rise)?
    const auto jumps_after = [&](int late) {
        auto s = course();
        const auto h = hero({0.0f, 0.01f, 0});
        s.entities.push_back(h);
        Sim sim(s);
        Pad pad{sim};
        pad.frames(10);
        pad.hold(Key::D);
        int left_at = -1;
        for (int i = 0; i < 120 && left_at < 0; ++i) {
            pad.frames(1);
            if (!sim.physics->is_grounded(h.id)) left_at = i;
        }
        REQUIRE(left_at >= 0);
        pad.frames(late);
        const float y0 = sim.pos(h).y;
        pad.hold(Key::Space);
        pad.frames(8);
        return sim.pos(h).y > y0 + 0.1f;
    };
    CHECK(jumps_after(3));    // 0.05 s late: within coyote_time (0.1 s), it still jumps
    CHECK_FALSE(jumps_after(12));  // 0.2 s late: too late, it falls

    // Buffering: jump pressed (and held) a few frames before landing from a fall happens on landing.
    const auto jumps_on_landing = [&](int early_frames) {
        scene::Scene s;
        s.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
        const auto h = hero({0.0f, 3.0f, 0});
        s.entities.push_back(h);
        Sim sim(s);
        Pad pad{sim};
        // Fall until about `early_frames` frames from the floor (falling at v, a frame covers v / 60).
        for (int i = 0; i < 200; ++i) {
            pad.frames(1);
            const auto v = sim.physics->character_velocity(h.id).value_or(math::Vec3{});
            if (sim.pos(h).y + v.y / 60.0f * static_cast<float>(early_frames) <= 0.0f) break;
        }
        pad.hold(Key::Space);  // pressed now, while still in the air
        float lowest = sim.pos(h).y, highest_after = -1e9f;
        bool landed = false;
        for (int i = 0; i < 40; ++i) {
            pad.frames(1);
            const float y = sim.pos(h).y;
            if (!landed && sim.physics->is_grounded(h.id)) landed = true;
            lowest = std::min(lowest, y);
            if (landed) highest_after = std::max(highest_after, y);
        }
        return highest_after > lowest + 0.5f;
    };
    CHECK(jumps_on_landing(3));    // 0.05 s early: buffered, it jumps when it lands
    CHECK_FALSE(jumps_on_landing(15));  // 0.25 s early: forgotten (and the key is held, not pressed again)
}

TEST_CASE("platformer: up through a one-way platform from below, stands on it, drops through with down + jump") {
    scene::Scene s;
    s.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
    s.entities.push_back(block("Ledge", {0, 1.0f, 0}, 4, 0.2f, true));  // top at 1.1 m, under a 1.5 m jump
    const auto h = hero({0.0f, 0.01f, 0});
    s.entities.push_back(h);
    Sim sim(s);
    Pad pad{sim};
    pad.frames(10);
    pad.hold(Key::Space);
    pad.frames(50);  // up through it, and back down onto it
    pad.hold(Key::Space, false);
    pad.frames(30);
    MESSAGE("after jumping up through the ledge: y " << sim.pos(h).y);
    CHECK(sim.pos(h).y == doctest::Approx(1.1f).epsilon(0.05));  // standing on its top
    CHECK(sim.physics->is_grounded(h.id));
    // Down + jump: through it, back to the floor.
    pad.hold(Key::S);
    pad.hold(Key::Space);
    pad.frames(2);
    pad.hold(Key::S, false);
    pad.hold(Key::Space, false);
    pad.frames(60);
    MESSAGE("after dropping: y " << sim.pos(h).y);
    CHECK(sim.pos(h).y < 0.1f);
    // A solid ledge (not one-way) stops the same jump: its head hits the underside.
    scene::Scene s2;
    s2.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
    s2.entities.push_back(block("Ceiling", {0, 1.6f, 0}, 4, 0.2f));
    const auto h2 = hero({0.0f, 0.01f, 0});
    s2.entities.push_back(h2);
    Sim sim2(s2);
    Pad pad2{sim2};
    pad2.frames(10);
    pad2.hold(Key::Space);
    pad2.frames(50);
    CHECK(sim2.pos(h2).y < 0.1f);  // bumped its head and fell back to the floor
}

TEST_CASE("platformer: a 3D character without the new settings moves exactly as before") {
    // No plane, no acceleration, no coyote time, no buffer: a jump asked for in the air is dropped (as in Milestone 4),
    // and speed is reached at once.
    scene::Scene s;
    s.entities.push_back(floor_entity());
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Walker";
    e.transform.position = {0, 0.01f, 0};
    e.components["character"] = {{"radius", 0.3}, {"height", 1.8}};
    s.entities.push_back(e);
    Sim sim(s);
    sim.run(0.3);
    const math::Vec3 p0 = sim.pos(e);
    sim.physics->character_move(e.id, {2, 0, 3});
    sim.world.frame(1.0 / 60.0, {});
    const math::Vec3 step = (sim.pos(e) - p0) * 60.0f;
    CHECK(step.x == doctest::Approx(2.0f).epsilon(0.02));
    CHECK(step.z == doctest::Approx(3.0f).epsilon(0.02));
}

// ---- Your own controllers (2D part 2b): script-owned velocity, contacts, shape queries ---------------------------

TEST_CASE("own controllers: with motion script, the script owns the velocity (no gravity added) and it slides") {
    scene::Scene s;
    s.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
    s.entities.push_back(block("Roof", {0, 4.0f, 0}, 40, 1));  // underside at 3.5 m
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Mover";
    e.transform.position = {0, 1.0f, 0};
    e.components["character"] = {{"radius", 0.3}, {"height", 1.0}, {"plane", "xy"}, {"motion", "script"}};
    s.entities.push_back(e);
    Sim sim(s);
    // No velocity set: it hangs in the air (the engine adds no gravity to a script-owned character).
    sim.run(1.0);
    CHECK(sim.pos(e).y == doctest::Approx(1.0f).epsilon(0.001));
    CHECK_FALSE(sim.physics->character_contacts(e.id)->floor);
    // Straight up at 5 m/s: it stops under the roof, touching it, and its velocity's upward part is gone (slid off).
    sim.physics->set_velocity(e.id, {0, 5, 0});
    sim.run(1.0);
    const auto c = sim.physics->character_contacts(e.id);
    MESSAGE("under the roof: feet at " << sim.pos(e).y << ", velocity y " << sim.physics->velocity(e.id)->y);
    CHECK(c->ceiling);
    CHECK(sim.pos(e).y == doctest::Approx(2.5f).epsilon(0.02));  // head (1 m tall) at the roof's underside
    CHECK(std::fabs(sim.physics->velocity(e.id)->y) < 0.01f);
    // Down at 5 m/s: it lands on the floor and stops; its contacts say floor, with the floor's normal.
    sim.physics->set_velocity(e.id, {0, -5, 0});
    sim.run(1.0);
    const auto f = sim.physics->character_contacts(e.id);
    CHECK(f->floor);
    CHECK(f->floor_normal.y == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(sim.pos(e).y == doctest::Approx(0.0f).epsilon(0.02));
    CHECK(std::fabs(sim.physics->velocity(e.id)->y) < 0.01f);
}

TEST_CASE("own controllers: a walking character takes a script's velocity for one step (a double jump)") {
    scene::Scene s;
    s.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
    const auto h = hero({0.0f, 0.01f, 0});
    s.entities.push_back(h);
    Sim sim(s);
    Pad pad{sim};
    pad.frames(10);
    pad.hold(Key::Space);
    float first = 0, second = 0;
    for (int i = 0; i < 26; ++i) {  // up to about the top of the first jump
        pad.frames(1);
        first = std::max(first, sim.pos(h).y);
    }
    sim.physics->set_velocity(h.id, {0, 9, 0});  // a second jump in mid-air: no API for it but the velocity
    for (int i = 0; i < 60; ++i) {
        pad.frames(1);
        second = std::max(second, sim.pos(h).y);
    }
    MESSAGE("first jump " << first << " m, after the double jump " << second << " m");
    CHECK(second > first + 1.0f);
}

TEST_CASE("own controllers: shape casts find the first thing a shape would hit, overlaps list what's there") {
    scene::Scene s;
    const auto box = body("Box", {5, 0.5f, 0}, {{"shape", "box"}, {"size", {1, 1, 1}}});
    const auto other = body("Other", {-5, 0.5f, 0}, {{"shape", "box"}, {"size", {1, 1, 1}}});
    s.entities = {box, other};
    Sim sim(s);
    sim.run(0.1);
    // A 0.5 m circle swept right from the origin meets the box's face at x 4.5 when its centre is at x 4.0.
    const auto hit = sim.physics->shape_cast({{"shape", "circle"}, {"radius", 0.5}}, {0, 0.5f, 0}, {10, 0.5f, 0});
    REQUIRE(hit);
    CHECK(hit->entity == box.id);
    CHECK(hit->distance == doctest::Approx(4.0f).epsilon(0.02));
    CHECK(hit->normal.x == doctest::Approx(-1.0f).epsilon(0.02));
    // Ignoring the box: nothing else that way.
    CHECK_FALSE(sim.physics->shape_cast({{"shape", "circle"}, {"radius", 0.5}}, {0, 0.5f, 0}, {10, 0.5f, 0}, box.id));
    // Overlaps: a rectangle over each box finds just that one; one spanning both finds both, sorted; empty air: none.
    CHECK(sim.physics->overlap({{"shape", "rectangle"}, {"size", {2, 2}}}, {5, 0.5f, 0}) == std::vector<Guid>{box.id});
    CHECK(sim.physics->overlap({{"shape", "box"}, {"size", {12, 1, 1}}}, {0, 0.5f, 0}).size() == 2);
    CHECK(sim.physics->overlap({{"shape", "sphere"}, {"radius", 1}}, {0, 5, 0}).empty());
    // Not a shape: nothing, no crash.
    CHECK_FALSE(sim.physics->shape_cast({{"shape", "teapot"}}, {0, 0, 0}, {1, 0, 0}));
}

TEST_CASE("own controllers + lua: a Godot-style platformer written in a script lands, jumps, and stops at a wall") {
    scene::Scene s;
    s.entities.push_back(block("Floor", {0, -0.5f, 0}, 40, 1));
    s.entities.push_back(block("Wall", {6, 2, 0}, 1, 4));  // its face at x 5.5
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Player";
    e.transform.position = {0, 2.0f, 0};
    e.components["character"] = {{"radius", 0.3}, {"height", 1.0}, {"plane", "xy"}, {"motion", "script"}};
    e.components["script"] = {{"file", "Scripts/player.luau"}};
    s.entities.push_back(e);
    runtime::World world(s);
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
    auto* lua_ptr = lua.get();
    // What a GDScript CharacterBody2D would do in _physics_process: gravity, run, jump once landed, move_and_slide.
    lua->add_source("Scripts/player.luau", R"(
        local P = {}
        local GRAVITY, SPEED, JUMP = 29.43, 4, 10
        function P:on_start() self.log = { landed = false, wall = false, top = 0, jumped = false } end
        function P:on_fixed_update(dt)
            local e = self.entity
            local v = e:velocity()  -- what the last step left after sliding (Godot: velocity after move_and_slide)
            self.log.vx = v.x
            v = vector.create(SPEED, v.y - GRAVITY * dt, 0)
            if e:is_on_floor() then
                self.log.landed = true
                if not self.log.jumped and noor.time() > 0.6 then
                    v = vector.create(v.x, JUMP, 0)
                    self.log.jumped = true
                end
            end
            if e:is_on_wall() then self.log.wall = true end
            e:set_velocity(v)
            if self.log.jumped then self.log.top = math.max(self.log.top, e:position().y) end
            self.log.x = e:position().x
            local hit = noor.shape_cast({ shape = "circle", radius = 0.3 }, e:position() + vector.create(0, 0.5, 0),
                                        e:position() + vector.create(20, 0.5, 0), e)
            self.log.ahead = hit and hit.entity:name() or "nothing"
            e:set("log", self.log)
        end
        return P
    )");
    world.add_system(std::move(lua));
    world.add_system(std::make_unique<PhysicsSystem>(PhysicsSystem::Options{}));
    for (int i = 0; i < 60 * 3; ++i) world.frame(1.0 / 60.0, {});
    CHECK(lua_ptr->errors().empty());
    const auto* log = runtime::api::component(world, e.id, "log");
    REQUIRE(log);
    MESSAGE("lua player: " << log->dump());
    CHECK((*log)["landed"].get<bool>());
    CHECK((*log)["wall"].get<bool>());
    CHECK((*log)["ahead"].get<std::string>() == "Wall");
    // It jumped: 10 m/s against 29.43 m/s2 rises about 1.7 m.
    CHECK((*log)["top"].get<float>() > 1.5f);
    // Stopped at the wall: its body's edge (0.3 m) against the face at 5.5, its run speed slid away.
    CHECK((*log)["x"].get<float>() == doctest::Approx(5.2f).epsilon(0.02));
    CHECK(std::fabs((*log)["vx"].get<float>()) < 0.05f);
}
