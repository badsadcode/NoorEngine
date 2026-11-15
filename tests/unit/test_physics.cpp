// Physics (Jolt, ADR-010) as a runtime system: falling, resting, bouncing, triggers, queries,
// kinematic bodies, mesh colliders, parented bodies and Lua collision hooks.
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>

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

TEST_CASE("physics: raycasts report entity, point, normal and distance; ignore skips one entity") {
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

TEST_CASE("physics: a mesh collider from the host's triangles; dynamic mesh colliders are refused") {
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
