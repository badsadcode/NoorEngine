// Time (gameplay foundations, part 2): pause, time scale and the real clock in the World; Lua timers, sequences that
// wait, scripts that run while paused, and what happens to all of it when an entity goes.
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <string>

using namespace noor;
using asset::Json;
namespace api = runtime::api;

namespace {

constexpr double kStep = 1.0 / 60.0;

struct Counter final : runtime::System {
    int fixed = 0;
    double update_dt = -1, late_dt = -1;
    std::string_view name() const override { return "counter"; }
    void update(runtime::World&, double dt) override { update_dt = dt; }
    void fixed_update(runtime::World&, double) override { ++fixed; }
    void late_update(runtime::World&, double dt) override { late_dt = dt; }
};

// A world with Lua scripts on entities named after their files ("pause": "always" if the name starts with "always").
struct Scripted {
    runtime::World world;
    scripting::LuaScriptSystem* lua;
    explicit Scripted(const std::vector<std::pair<std::string, std::string>>& scripts) : world(make(scripts)) {
        auto l = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
        for (const auto& [name, source] : scripts) l->add_source("Scripts/" + name + ".luau", source);
        lua = l.get();
        world.add_system(std::move(l));
    }
    static scene::Scene make(const std::vector<std::pair<std::string, std::string>>& scripts) {
        scene::Scene s;
        for (const auto& [name, source] : scripts) {
            scene::Entity e;
            e.id = Guid::generate();
            e.name = name;
            e.components["script"] = {{"file", "Scripts/" + name + ".luau"}};
            if (name.starts_with("always")) e.components["script"]["pause"] = "always";
            s.entities.push_back(e);
        }
        return s;
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) world.frame(kStep, {});
    }
    Json get(const std::string& entity, const std::string& component) {
        const auto id = api::find(world, entity);
        const Json* c = id ? api::component(world, *id, component) : nullptr;
        return c ? *c : Json();
    }
};

} // namespace

TEST_CASE("world clock: paused, nothing moves on (update dt 0, no fixed steps), slow motion takes fewer fixed steps") {
    runtime::World w{scene::Scene{}};
    auto c = std::make_unique<Counter>();
    auto* counter = c.get();
    w.add_system(std::move(c));
    for (int i = 0; i < 60; ++i) w.frame(kStep, {});
    CHECK(counter->fixed == 60);
    CHECK(w.time() == doctest::Approx(1.0));
    w.set_paused(true);
    for (int i = 0; i < 30; ++i) w.frame(kStep, {});
    CHECK(counter->fixed == 60);
    CHECK(counter->update_dt == 0.0);
    CHECK(counter->late_dt == 0.0);
    CHECK(w.time() == doctest::Approx(1.0));
    CHECK(w.real_time() == doctest::Approx(1.5));
    CHECK(w.real_dt() == doctest::Approx(kStep));
    w.set_paused(false);
    w.set_time_scale(0.5);
    for (int i = 0; i < 60; ++i) w.frame(kStep, {});
    CHECK(counter->fixed == 90);  // half as many steps, each still 1/60 s
    CHECK(w.time() == doctest::Approx(1.5));
    w.set_time_scale(-3);
    CHECK(w.time_scale() == 0.0);  // clamped
    w.set_time_scale(100);
    CHECK(w.time_scale() == 10.0);
}

TEST_CASE("world clock: a pause made during update holds for the rest of that frame (physics doesn't step once more)") {
    scene::Scene s;
    scene::Entity box;
    box.id = Guid::generate();
    box.name = "Box";
    box.transform.position = {0, 10, 0};
    box.components["collider"] = {{"shape", "box"}, {"size", {1, 1, 1}}};
    box.components["rigidbody"] = {{"type", "dynamic"}};
    s.entities.push_back(box);
    runtime::World w(s);
    struct Pauser final : runtime::System {
        std::string_view name() const override { return "pauser"; }
        void update(runtime::World& world, double) override {
            if (world.frame_count() == 30) world.set_paused(true);
        }
    };
    w.add_system(std::make_unique<Pauser>());
    w.add_system(std::make_unique<physics::PhysicsSystem>(physics::PhysicsSystem::Options{}));
    for (int i = 0; i < 30; ++i) w.frame(kStep, {});
    const float y = api::world_position(w, box.id)->y;
    for (int i = 0; i < 30; ++i) w.frame(kStep, {});
    CHECK(api::world_position(w, box.id)->y == y);  // to the bit
}

TEST_CASE("lua timers: after once on time, every until it returns false or is cancelled, errors stop only that timer") {
    Scripted g({{"t", R"(
        local T = {}
        function T:on_start()
            self.log = {}
            noor.after(0.5, function() table.insert(self.log, "after " .. string.format("%.3f", noor.time())) end)
            self.n = 0
            noor.every(0.25, function() self.n += 1; return self.n < 3 end)
            self.m = 0
            self.handle = noor.every(0.1, function() self.m += 1 end)
            noor.after(0.05, function() error("boom") end)
        end
        function T:on_update(dt)
            if self.m == 4 then self.handle:cancel() end
            self.entity:set("out", { log = self.log, n = self.n, m = self.m })
        end
        return T
    )"}});
    g.frames(120);
    const Json out = g.get("t", "out");
    CHECK(out["log"] == Json::array({"after 0.500"}));
    CHECK(out["n"] == 3);
    CHECK(out["m"] == 4);
    REQUIRE(g.lua->errors().size() == 1);
    CHECK(g.lua->errors()[0].find("boom") != std::string::npos);
    CHECK(g.lua->errors()[0].find("a timer on 't'") != std::string::npos);
}

TEST_CASE("lua sequences: noor.start runs until noor.wait, carries on later, a wait outside one is an error") {
    Scripted g({{"s", R"(
        local S = {}
        function S:on_start()
            self.steps = {}
            noor.start(function()
                table.insert(self.steps, string.format("a %.3f", noor.time()))
                noor.wait(0.25)
                table.insert(self.steps, string.format("b %.3f", noor.time()))
                for i = 1, 3 do noor.wait(0.1) end
                table.insert(self.steps, string.format("c %.3f", noor.time()))
            end)
            table.insert(self.steps, "started")  -- noor.start returns at the first wait
            local ok, err = pcall(noor.wait, 1)
            self.outside = not ok and string.find(err, "inside noor.start") ~= nil
        end
        function S:on_update() self.entity:set("out", { steps = self.steps, outside = self.outside }) end
        return S
    )"}});
    g.frames(60);
    const Json out = g.get("s", "out");
    CHECK(out["steps"] == Json::array({"a 0.000", "started", "b 0.250", "c 0.550"}));
    CHECK(out["outside"] == true);
}

TEST_CASE("lua pause: hooks, timers and waits stop, except on scripts that run while paused (on the real clock)") {
    Scripted g({{"game", R"(
        local G = {}
        function G:on_start()
            self.updates = 0
            noor.after(0.5, function() self.fired = noor.real_time() end)
        end
        function G:on_update() self.updates += 1; self.entity:set("out", { updates = self.updates, fired = self.fired or -1 }) end
        return G
    )"},
                {"always_menu", R"(
        local M = {}
        function M:on_start()
            self.updates = 0
            noor.set_paused(true)
            noor.after(1.0, function() noor.set_paused(false) end, { real = true })
            noor.start(function() noor.wait(0.25, { real = true }); self.waited = noor.real_time() end)
        end
        function M:on_update(dt) self.updates += 1; self.dt = dt; self.entity:set("out", { updates = self.updates, dt = self.dt, waited = self.waited or -1 }) end
        return M
    )"}});
    g.frames(30);  // half a second, paused
    CHECK(g.get("game", "out").is_null());          // never updated yet
    CHECK(g.get("always_menu", "out")["updates"] == 30);
    CHECK(g.get("always_menu", "out")["dt"].get<double>() == doctest::Approx(kStep));  // the real dt while paused
    CHECK(g.get("always_menu", "out")["waited"].get<double>() == doctest::Approx(0.25).epsilon(1e-6));
    g.frames(62);  // unpaused at 1 s real, the game clock running from the next frame; its timer due at 0.5 s of it
    const Json game = g.get("game", "out");
    CHECK(game["updates"].get<int>() > 0);
    CHECK(game["fired"].get<double>() == doctest::Approx(1.5).epsilon(1e-6));  // 1 s paused + 0.5 s of game time
    CHECK(g.lua->errors().empty());
}

TEST_CASE("lua timers and sequences go with their entity, and a failing sequence is reported and dropped") {
    Scripted g({{"doomed", R"(
        local D = {}
        function D:on_start()
            noor.every(0.1, function() noor.log("still here") end)
            noor.start(function() noor.wait(0.2); error("late") end)
            noor.after(0.05, function() self.entity:destroy() end)
        end
        return D
    )"},
                {"broken", R"(
        local B = {}
        function B:on_start()
            noor.start(function() noor.wait(0.1); error("sequence fails") end)
            noor.start(function() coroutine.yield() end)
        end
        return B
    )"}});
    g.frames(60);
    CHECK_FALSE(api::find(g.world, "doomed"));
    // Only "broken"'s two: the failure, and a yield that wasn't a noor.wait; nothing from "doomed" after it went.
    REQUIRE(g.lua->errors().size() == 2);
    CHECK(g.lua->errors()[0].find("yielded without noor.wait") != std::string::npos);
    CHECK(g.lua->errors()[1].find("sequence fails") != std::string::npos);
}

TEST_CASE("lua pause: the paused game's HUD stays on screen under the pause menu, once each") {
    Scripted g({{"game", R"(
        local G = {}
        function G:on_update() noor.ui.text("score 7", 0, 0) end
        return G
    )"},
                {"always_menu", R"(
        local M = {}
        function M:on_update()
            if noor.time() >= 0.25 and not noor.paused() and not self.done then noor.set_paused(true); self.done = true end
            if noor.paused() then noor.ui.text("PAUSED", 0, 0, { anchor = "center" }) end
        end
        return M
    )"}});
    g.frames(30);
    REQUIRE(g.world.paused());
    std::vector<std::string> texts;
    for (const auto& c : g.world.ui_commands()) texts.push_back(c.text);
    CHECK(texts == std::vector<std::string>{"score 7", "PAUSED"});  // the frozen HUD first, the menu over it
    g.world.set_paused(false);
    g.frames(1);
    texts.clear();
    for (const auto& c : g.world.ui_commands()) texts.push_back(c.text);
    CHECK(texts == std::vector<std::string>{"score 7"});
}
