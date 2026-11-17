// Character controller (Jolt CharacterVirtual) and the game-facing bits around it.
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace noor;
using asset::Json;
using physics::PhysicsSystem;

namespace {

scene::Entity box(std::string name, math::Vec3 pos, math::Vec3 size) {
    scene::Entity e;
    e.id = Guid::generate();
    e.name = std::move(name);
    e.transform.position = pos;
    e.components["collider"] = {{"shape", "box"}, {"size", {size.x, size.y, size.z}}};
    return e;
}

scene::Entity player(math::Vec3 feet) {
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Player";
    e.transform.position = feet;
    e.components["character"] = {{"radius", 0.3}, {"height", 1.8}, {"step_height", 0.4}};
    return e;
}

struct Game {
    Guid hero;  // before `world`: make() sets it while `world` is being built
    runtime::World world;
    PhysicsSystem* physics;
    explicit Game(std::vector<scene::Entity> extra, math::Vec3 start = {0, 0.05f, 0}) : world(make(std::move(extra), start)) {
        auto p = std::make_unique<PhysicsSystem>(PhysicsSystem::Options{});
        physics = p.get();
        world.add_system(std::move(p));
    }
    scene::Scene make(std::vector<scene::Entity> extra, math::Vec3 start) {
        scene::Scene s;
        s.entities.push_back(box("Floor", {0, -0.5f, 0}, {40, 1, 40}));
        auto p = player(start);
        hero = p.id;
        s.entities.push_back(p);
        for (auto& e : extra) s.entities.push_back(std::move(e));
        return s;
    }
    // Steers every frame, like a script would.
    void walk(math::Vec3 velocity, double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            physics->character_move(hero, velocity);
            world.frame(1.0 / 60.0, {});
        }
    }
    math::Vec3 pos() { return world.find(hero)->transform.position; }
};

} // namespace

TEST_CASE("character: stands on the floor, walks at the asked speed, and stops when not steered") {
    Game g({});
    g.walk({0, 0, 0}, 0.5);
    CHECK(g.physics->has_character(g.hero));
    CHECK(g.physics->is_grounded(g.hero));
    CHECK(g.pos().y == doctest::Approx(0.0f).epsilon(0.03));  // feet on the floor
    g.walk({0, 0, -3}, 1.0);
    CHECK(g.pos().z == doctest::Approx(-3.0f).epsilon(0.1));
    const float z = g.pos().z;
    g.walk({0, 0, 0}, 0.5);
    CHECK(g.pos().z == doctest::Approx(z).epsilon(0.01));  // no drifting
}

TEST_CASE("character: is drawn smoothly between fixed steps when frame times wobble") {
    // A 60 Hz screen: frames take about 1/60 s, but never exactly. Physics steps at exactly 60 Hz, so
    // some frames get no step and some get two: the simulated position stalls and skips. Where the
    // World is told to draw the character moves the same amount every frame.
    Game g({});
    g.walk({0, 0, 0}, 0.3);
    const float speed = 3.0f;
    float last_raw = 0, last_drawn = 0;
    int stalls = 0;
    double worst = 0;
    std::uint32_t seed = 12345;  // a fixed pseudo-random wobble of +-1.5 ms around 1/60 s
    for (int i = 0; i < 120; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const double dt = 1.0 / 60.0 + (static_cast<double>(seed >> 8) / 16777216.0 - 0.5) * 0.003;
        g.physics->character_move(g.hero, {0, 0, -speed});
        g.world.frame(dt, {});
        const float raw = g.pos().z;
        const auto pose = g.world.render_poses().find(g.hero);
        REQUIRE(pose != g.world.render_poses().end());
        const float drawn = pose->second.transform_point({0, 0, 0}).z;
        if (i > 10) {  // after the walk has started
            if (std::abs(raw - last_raw) < 0.001f) ++stalls;
            worst = std::max(worst, std::abs(std::abs(drawn - last_drawn) - speed * dt));
        }
        last_raw = raw;
        last_drawn = drawn;
    }
    CHECK(stalls > 0);              // the simulation alone stutters...
    CHECK(worst < speed * 0.004);   // ...the drawn position moves smoothly (within 12 mm of speed * dt per frame)
}

TEST_CASE("character: jumps only from the ground and lands again") {
    Game g({});
    g.walk({0, 0, 0}, 0.3);
    g.physics->character_jump(g.hero, 5.0f);
    float peak = 0;
    for (int i = 0; i < 30; ++i) {
        g.world.frame(1.0 / 60.0, {});
        peak = std::max(peak, g.pos().y);
    }
    CHECK(peak > 1.0f);  // v^2 / 2g = 1.27 m
    CHECK_FALSE(g.physics->is_grounded(g.hero));
    g.physics->character_jump(g.hero, 5.0f);  // mid-air: ignored
    g.walk({0, 0, 0}, 1.5);
    CHECK(g.physics->is_grounded(g.hero));
    CHECK(g.pos().y == doctest::Approx(0.0f).epsilon(0.03));
}

TEST_CASE("character: walks up a kerb-height step, is stopped by a wall") {
    Game g({box("Kerb", {0, 0.15f, -3}, {4, 0.3f, 2}), box("Wall", {6, 1, 0}, {1, 2, 8})}, {0, 0.05f, 0});
    g.walk({0, 0, -2}, 1.8);  // onto the 0.3 m kerb
    CHECK(g.pos().y == doctest::Approx(0.3f).epsilon(0.05));
    g.walk({0, 0, 2}, 1.8);   // back down
    g.walk({4, 0, 0}, 2.5);   // into the 2 m wall at x = 5.5
    CHECK(g.pos().x < 5.5f - 0.25f);
    CHECK(g.pos().x > 4.5f);  // got there and stopped
}

TEST_CASE("character: raycasts and triggers see the character (inner body), ignore skips it") {
    auto sensor = box("Zone", {0, 1, -4}, {2, 2, 1});
    sensor.components["collider"]["trigger"] = true;
    Game g({sensor});
    g.walk({0, 0, 0}, 0.2);
    const auto hit = g.physics->raycast({0, 1, 5}, {0, 0, -1}, 20);
    REQUIRE(hit);
    CHECK(hit->entity == g.hero);
    CHECK(hit->point.z == doctest::Approx(0.3f).epsilon(0.05));  // the capsule's front
    // Shooting from the character's own eyes: ignoring the character skips its inner body (it used
    // to hit itself at distance 0).
    const auto from_eyes = g.physics->raycast({0, 1.6f, 0}, {0, 0, -1}, 20, g.hero);
    CHECK((!from_eyes || from_eyes->entity != g.hero));

    struct Recorder final : runtime::System {
        int enters = 0;
        Guid hero;
        std::string_view name() const override { return "recorder"; }
        void late_update(runtime::World& w, double) override {
            for (const auto& e : w.events())
                if (e.entity == hero && e.kind == runtime::Event::Kind::TriggerEnter) ++enters;
        }
    };
    auto rec = std::make_unique<Recorder>();
    rec->hero = g.hero;
    auto* r = rec.get();
    g.world.add_system(std::move(rec));
    g.walk({0, 0, -3}, 1.5);
    CHECK(r->enters == 1);
}

TEST_CASE("character + lua: a script steers with input and asks for the mouse") {
    Game g({});
    g.world.find(g.hero)->components["script"] = {{"file", "Scripts/walker.luau"}, {"params", {{"speed", 4}}}};
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
    lua->add_source("Scripts/walker.luau", R"(
        local W = {}
        function W:on_update(dt)
            local move = self.entity:forward() * noor.input.axis("move_y")
            self.entity:character_move(move * self.params.speed)
            if noor.input.pressed("fire") then noor.input.capture_mouse(true) end
            self.entity:set("state", { grounded = self.entity:is_grounded(), captured = noor.input.mouse_captured() })
        end
        return W
    )");
    auto* lua_ptr = lua.get();
    // Scripts first, like play mode: rebuild the system order.
    runtime::World world(g.world.scene());
    world.add_system(std::move(lua));
    auto phys = std::make_unique<PhysicsSystem>(PhysicsSystem::Options{});
    world.add_system(std::move(phys));

    runtime::InputState w_and_click;
    w_and_click.set(runtime::Key::W);
    w_and_click.set(runtime::Key::MouseLeft);
    world.frame(1.0 / 60.0, w_and_click);
    CHECK(world.take_mouse_capture_request() == std::optional<bool>(true));
    runtime::InputState w;
    w.set(runtime::Key::W);
    w.mouse_captured = true;  // the host captured it
    for (int i = 0; i < 60; ++i) world.frame(1.0 / 60.0, w);
    CHECK(lua_ptr->errors().empty());
    const auto p = runtime::api::position(world, g.hero);
    REQUIRE(p);
    CHECK(p->z == doctest::Approx(-4.0f).epsilon(0.15));  // forward is -Z
    const auto* state = runtime::api::component(world, g.hero, "state");
    REQUIRE(state);
    CHECK((*state)["grounded"] == true);
    CHECK((*state)["captured"] == true);
}
