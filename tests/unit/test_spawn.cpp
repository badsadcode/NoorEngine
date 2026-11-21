// Spawning, teleport, groups and require (gameplay foundations, part 1).
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scene/prefab.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>

using namespace noor;
using asset::Json;
namespace api = runtime::api;

namespace {

const Guid kPrefabId = *Guid::parse("9a080000-0000-4000-8000-0000000000b1");

// A bullet: a dynamic sphere tagged "bullet", with a child trail.
scene::Prefab bullet_prefab() {
    scene::Prefab p;
    p.id = kPrefabId;
    p.name = "Bullet";
    scene::Entity root;
    root.id = *Guid::parse("9a080000-0000-4000-8000-0000000000b2");
    root.name = "Bullet";
    root.tags = {"bullet"};
    root.transform.position = {0, 0.5f, 0};  // the prefab's root sits a little up from its origin
    root.components["collider"] = {{"shape", "sphere"}, {"radius", 0.2}};
    root.components["rigidbody"] = {{"type", "dynamic"}, {"mass", 0.2}};
    scene::Entity trail;
    trail.id = *Guid::parse("9a080000-0000-4000-8000-0000000000b3");
    trail.name = "Trail";
    trail.parent = root.id;
    trail.transform.position = {0, 0, 0.3f};
    p.entities = {root, trail};
    return p;
}

struct Spawning {
    scene::Prefab prefab = bullet_prefab();
    runtime::World world;
    explicit Spawning(scene::Scene s = {}) : world(std::move(s)) {
        world.set_prefabs([this](const Guid& id) { return id == kPrefabId ? &prefab : nullptr; },
                          [](std::string_view path) -> std::optional<Guid> {
                              return path == "Assets/Bullet.noorprefab" ? std::optional(kPrefabId) : std::nullopt;
                          });
    }
};

scene::Scene with_floor() {
    scene::Scene s;
    s.id = *Guid::parse("9a080000-0000-4000-8000-0000000000aa");
    scene::Entity floor;
    floor.id = Guid::generate();
    floor.name = "Floor";
    floor.transform.position = {0, -0.5f, 0};
    floor.components["collider"] = {{"shape", "box"}, {"size", {40, 1, 40}}};
    s.entities.push_back(floor);
    return s;
}

// Counts the entities systems were told about.
struct Starts final : runtime::System {
    int started = 0;
    std::string_view name() const override { return "starts"; }
    void entity_started(runtime::World&, scene::Entity&) override { ++started; }
};

} // namespace

TEST_CASE("spawn: a prefab by id or path, flattened, placed and turned, its ids the same every run") {
    Spawning a(with_floor());
    const auto turned = api::quat_from_euler({0, 90, 0});
    const auto b = api::spawn(a.world, "Assets/Bullet.noorprefab", {2, 3, 4}, turned);
    REQUIRE(b);
    const auto* root = a.world.find(*b);
    REQUIRE(root);
    CHECK(root->name == "Bullet");           // the prefab's root itself, no placement entity above it
    CHECK_FALSE(root->parent.has_value());
    CHECK(root->tags == std::vector<std::string>{"bullet"});
    CHECK(root->components.contains("rigidbody"));
    // Its own offset turned and moved with the placement.
    CHECK(root->transform.position.x == doctest::Approx(2));
    CHECK(root->transform.position.y == doctest::Approx(3.5));
    CHECK(root->transform.position.z == doctest::Approx(4));
    const auto trail = api::child(a.world, *b, "Trail");
    REQUIRE(trail);
    const auto tw = api::world_position(a.world, *trail);
    CHECK(tw->x == doctest::Approx(2.3).epsilon(1e-4));  // its +0.3 z turned 90 degrees about y: +0.3 x
    CHECK(tw->z == doctest::Approx(4).epsilon(1e-4));
    // By id too; unknown prefabs give nothing and spawn nothing.
    const std::size_t before = a.world.scene().entities.size();
    CHECK(api::spawn(a.world, kPrefabId.to_string(), {0, 0, 0}));
    CHECK(a.world.scene().entities.size() == before + 2);
    CHECK_FALSE(api::spawn(a.world, "Assets/Nothing.noorprefab", {0, 0, 0}));
    CHECK(a.world.scene().entities.size() == before + 2);
    // Deterministic: another world from the same scene spawns the same ids (built games stay identical).
    Spawning other(with_floor());
    CHECK(api::spawn(other.world, "Assets/Bullet.noorprefab", {0, 0, 0}) == b);
}

TEST_CASE("spawn: under a parent, and spawned entities start next frame like any other") {
    Spawning a(with_floor());
    auto starts = std::make_unique<Starts>();
    auto* counter = starts.get();
    a.world.add_system(std::move(starts));
    a.world.frame(1.0 / 60.0, {});
    const int at_start = counter->started;
    const auto floor = api::find(a.world, "Floor");
    const auto b = api::spawn(a.world, "Assets/Bullet.noorprefab", {1, 0, 0}, {}, floor);
    REQUIRE(b);
    CHECK(a.world.find(*b)->parent == floor);
    CHECK(api::world_position(a.world, *b)->y == doctest::Approx(-0.5f + 0.5f));  // local to the floor (at y -0.5)
    CHECK(counter->started == at_start);  // not yet
    a.world.frame(1.0 / 60.0, {});
    CHECK(counter->started == at_start + 2);  // the bullet and its trail
}

TEST_CASE("spawn: a thousand bullets a second, spawned and destroyed, with physics (cost measured)") {
    Spawning a(with_floor());
    a.world.add_system(std::make_unique<physics::PhysicsSystem>(physics::PhysicsSystem::Options{}));
    std::vector<std::pair<Guid, int>> alive;  // a bullet lives one second (60 frames)
    const auto t0 = std::chrono::steady_clock::now();
    int frames = 0, spawned = 0;
    for (; frames < 180; ++frames) {
        for (int k = 0; k < 17; ++k) {  // ~1,000 a second
            const auto b = api::spawn(a.world, "Assets/Bullet.noorprefab", {static_cast<float>(k) - 8.0f, 4.0f, static_cast<float>(frames % 10)});
            REQUIRE(b);
            alive.emplace_back(*b, frames + 60);
            ++spawned;
        }
        std::erase_if(alive, [&](const auto& p) {
            if (p.second > frames) return false;
            api::destroy(a.world, p.first);
            return true;
        });
        a.world.frame(1.0 / 60.0, {});
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    MESSAGE("spawned " << spawned << ", about 1,000 alive at a time: " << ms << " ms a frame (spawning, destroying and simulating)");
    // Nothing leaks: only the floor and what's alive (two entities each) are left.
    CHECK(a.world.scene().entities.size() == 1 + 2 * alive.size());
    CHECK(api::find_all(a.world, "bullet").size() == alive.size());
    CHECK(ms < 30.0);  // generous: the point is the measurement, this only catches a disaster
}

TEST_CASE("teleport: a character stops where it's put, a body too, anything else just moves") {
    scene::Scene s = with_floor();
    scene::Entity hero;
    hero.id = Guid::generate();
    hero.name = "Hero";
    hero.transform.position = {0, 0.05f, 0};
    hero.components["character"] = {{"radius", 0.3}, {"height", 1.0}, {"plane", "xy"}, {"gravity_scale", 3}};
    scene::Entity crate;
    crate.id = Guid::generate();
    crate.name = "Crate";
    crate.transform.position = {5, 6, 0};
    crate.components["collider"] = {{"shape", "box"}, {"size", {1, 1, 1}}};
    crate.components["rigidbody"] = {{"type", "dynamic"}};
    scene::Entity marker;
    marker.id = Guid::generate();
    marker.name = "Marker";
    s.entities = {s.entities[0], hero, crate, marker};
    runtime::World world(s);
    auto p = std::make_unique<physics::PhysicsSystem>(physics::PhysicsSystem::Options{});
    auto* phys = p.get();
    world.add_system(std::move(p));
    // The hero runs right and jumps; the crate falls.
    for (int i = 0; i < 30; ++i) {
        phys->character_move(hero.id, {6, 0, 0});
        if (i == 20) phys->character_jump(hero.id, 8);
        world.frame(1.0 / 60.0, {});
    }
    CHECK(api::world_position(world, hero.id)->x > 1.0f);
    // Back to a checkpoint mid-jump: it's there, it isn't moving, and it doesn't carry its run or its jump.
    CHECK(api::teleport(world, hero.id, {-3, 4, 0.7f}));  // z is ignored for a 2D character: it stays in its plane
    auto at = *api::world_position(world, hero.id);
    CHECK(at.x == doctest::Approx(-3));
    CHECK(at.y == doctest::Approx(4));
    CHECK(at.z == doctest::Approx(0));
    world.frame(1.0 / 60.0, {});
    at = *api::world_position(world, hero.id);
    CHECK(at.x == doctest::Approx(-3).epsilon(1e-3));  // no leftover running speed
    CHECK(at.y < 4.0f);                                // gravity from rest: a little lower, not rising
    CHECK(at.y > 3.9f);
    for (int i = 0; i < 90; ++i) world.frame(1.0 / 60.0, {});
    CHECK(api::world_position(world, hero.id)->y == doctest::Approx(0.0).epsilon(0.02));  // landed on the floor
    // The crate: moved and stopped, then falls from rest.
    CHECK(api::teleport(world, crate.id, {-5, 10, 0}, api::quat_from_euler({0, 0, 45})));
    CHECK(phys->velocity(crate.id)->y == doctest::Approx(0));
    CHECK(api::world_position(world, crate.id)->y == doctest::Approx(10));
    // A plain entity is simply placed; unknown ones aren't.
    CHECK(api::teleport(world, marker.id, {1, 2, 3}));
    CHECK(api::world_position(world, marker.id)->z == doctest::Approx(3));
    CHECK_FALSE(api::teleport(world, Guid::generate(), {0, 0, 0}));
}

TEST_CASE("groups: tags added, removed, found in scene order") {
    scene::Scene s;
    for (const char* n : {"A", "B", "C"}) {
        scene::Entity e;
        e.id = Guid::generate();
        e.name = n;
        s.entities.push_back(e);
    }
    s.entities[2].tags = {"enemy"};
    runtime::World w(s);
    const Guid a = s.entities[0].id, b = s.entities[1].id, c = s.entities[2].id;
    api::add_tag(w, a, "enemy");
    api::add_tag(w, a, "enemy");  // once only
    api::add_tag(w, b, "friend");
    CHECK(w.find(a)->tags.size() == 1);
    CHECK(api::find_all(w, "enemy") == std::vector<Guid>{a, c});
    CHECK(api::has_tag(w, b, "friend"));
    api::remove_tag(w, b, "friend");
    CHECK_FALSE(api::has_tag(w, b, "friend"));
    CHECK(api::find_all(w, "friend").empty());
}

TEST_CASE("lua: require shares a file's table, finds Scripts/ by name, refuses cycles and paths outside") {
    scene::Scene s;
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "User";
    e.components["script"] = {{"file", "Scripts/user.luau"}};
    s.entities.push_back(e);
    runtime::World world(s);
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
    lua->add_source("Scripts/user.luau", R"(
        local util = require("util")                 -- Scripts/util.luau, found by name
        local again = require("Scripts/util.luau")   -- the same file by its path: the same table
        local U = {}
        function U:on_start()
            util.count += 1
            self.entity:set("result", { same = (util == again), count = again.count, answer = util.answer })
            local ok1, e1 = pcall(require, "../secrets")
            local ok2, e2 = pcall(require, "loop_a")
            local ok3, e3 = pcall(require, "nothing_here")
            self.entity:set("errors", { outside = not ok1 and string.find(e1, "inside the project") ~= nil,
                                        cycle = not ok2 and string.find(e2, "cycle") ~= nil, missing = not ok3 })
        end
        return U
    )");
    lua->add_source("Scripts/util.luau", "local M = { answer = 42, count = 0 } return M");
    lua->add_source("Scripts/loop_a.luau", "return require('loop_b')");
    lua->add_source("Scripts/loop_b.luau", "return require('loop_a')");
    auto* l = lua.get();
    world.add_system(std::move(lua));
    world.frame(1.0 / 60.0, {});
    const auto& r = world.find(e.id)->components;
    REQUIRE(r.contains("result"));
    CHECK(r["result"]["same"] == true);
    CHECK(r["result"]["count"] == 1);
    CHECK(r["result"]["answer"] == 42);
    CHECK(r["errors"]["outside"] == true);
    CHECK(r["errors"]["cycle"] == true);
    CHECK(r["errors"]["missing"] == true);
    MESSAGE(l->errors().size() << " script error(s) reported (the failed requires)");
}
