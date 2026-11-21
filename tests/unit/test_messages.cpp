// Messages between scripts (gameplay foundations, part 3): signals from an entity and game-wide, delivered at once in
// the order listeners came, data and sender, calling another script's function, engine events as messages, and
// everything tidied away with its entity.
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

using namespace noor;
using asset::Json;
namespace api = runtime::api;

namespace {

struct Scripted {
    runtime::World world;
    scripting::LuaScriptSystem* lua = nullptr;
    explicit Scripted(scene::Scene s, const std::vector<std::pair<std::string, std::string>>& files, bool physics = false)
        : world(std::move(s)) {
        if (physics) world.add_system(std::make_unique<physics::PhysicsSystem>(physics::PhysicsSystem::Options{}));
        auto l = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
        for (const auto& [name, source] : files) l->add_source("Scripts/" + name + ".luau", source);
        lua = l.get();
        world.add_system(std::move(l));
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) world.frame(1.0 / 60.0, {});
    }
    Json out(const std::string& entity) {
        const auto id = api::find(world, entity);
        const Json* c = id ? api::component(world, *id, "out") : nullptr;
        return c ? *c : Json();
    }
};

scene::Entity scripted(const std::string& name, const std::string& file) {
    scene::Entity e;
    e.id = Guid::generate();
    e.name = name;
    if (!file.empty()) e.components["script"] = {{"file", "Scripts/" + file + ".luau"}};
    return e;
}

scene::Scene scene_of(std::vector<scene::Entity> es) {
    scene::Scene s;
    s.entities = std::move(es);
    return s;
}

} // namespace

TEST_CASE("messages: a lever's signal reaches the door listening to it, with data and sender, at once") {
    Scripted g(scene_of({scripted("Lever", "lever"), scripted("Door", "door"), scripted("Bystander", "")}),
               {{"lever", R"(
        local Lever = {}
        function Lever:on_update()
            if noor.time() >= 0.5 and not self.pulled then
                self.pulled = true
                self.heard_by = self.entity:emit("pulled", { strength = 3, by = "hand" })
                self.after = noor.find("Door"):get("out")  -- delivered already: synchronous
                self.entity:set("out", { heard_by = self.heard_by, door_open_now = self.after and self.after.open })
            end
        end
        return Lever
    )"},
                {"door", R"(
        local Door = {}
        function Door:on_start()
            noor.find("Lever"):on("pulled", function(data, sender)
                self.entity:set("out", { open = true, strength = data.strength, by = data.by, sender = sender:name() })
            end)
        end
        return Door
    )"}});
    g.frames(40);
    CHECK(g.out("Door")["open"] == true);
    CHECK(g.out("Door")["strength"] == 3);
    CHECK(g.out("Door")["by"] == "hand");
    CHECK(g.out("Door")["sender"] == "Lever");
    CHECK(g.out("Lever")["heard_by"] == 1);
    CHECK(g.out("Lever")["door_open_now"] == true);
    CHECK(g.lua->errors().empty());
}

TEST_CASE("messages: game-wide ones, in the order listeners came, cancelled ones don't hear, e:call returns a result") {
    Scripted g(scene_of({scripted("A", "a"), scripted("B", "b"), scripted("C", "c")}),
               {{"a", R"(
        local A = {}
        function A:on_start()
            noor.on("tick", function(n) noor.find("C"):call("note", "a" .. n) end)
        end
        function A:double(x) return x * 2 end
        return A
    )"},
                {"b", R"(
        local B = {}
        function B:on_start()
            noor.on("tick", function(n) noor.find("C"):call("note", "b" .. n) end)
            self.l = noor.on("tick", function(n) noor.find("C"):call("note", "never") end)
            self.l:cancel()
        end
        return B
    )"},
                {"c", R"(
        local C = {}
        function C:on_start() self.log = {} end
        function C:note(s) table.insert(self.log, s) end
        function C:on_update()
            if noor.time() >= 0.1 and not self.done then
                self.done = true
                local heard = noor.emit("tick", 1)
                noor.emit("tick", 2)
                local doubled = noor.find("A"):call("double", 21)
                local ok, err = pcall(function() return noor.find("A"):call("nothing") end)
                self.entity:set("out", { log = self.log, heard = heard, doubled = doubled,
                                         missing = not ok and string.find(err, "no script function 'nothing'") ~= nil })
            end
        end
        return C
    )"}});
    g.frames(20);
    CHECK(g.out("C")["log"] == Json::array({"a1", "b1", "a2", "b2"}));
    CHECK(g.out("C")["heard"] == 2);
    CHECK(g.out("C")["doubled"] == 42);
    CHECK(g.out("C")["missing"] == true);
}

TEST_CASE("messages: listeners go with the listening entity or the emitter, nothing leaks over 10,000 spawns") {
    Scripted g(scene_of({scripted("Hub", "hub")}), {{"hub", R"(
        local Hub = {}
        function Hub:on_update()
            noor.emit("ping")
        end
        return Hub
    )"},
                                                    {"ear", R"(
        local Ear = {}
        function Ear:on_start()
            noor.on("ping", function() self.pings = (self.pings or 0) + 1 end)
            noor.find("Hub"):on("anything", function() end)
        end
        return Ear
    )"}});
    // Entities with the listening script come and go, a hundred at a time.
    for (int round = 0; round < 100; ++round) {
        std::vector<Guid> ears;
        for (int i = 0; i < 100; ++i) {
            auto e = scripted("Ear", "ear");
            ears.push_back(e.id);
            g.world.spawn(e);
        }
        g.frames(2);
        CHECK(g.world.listener_count() == 200);
        for (const auto& id : ears) g.world.destroy(id);
        g.frames(1);
        CHECK(g.world.listener_count() == 0);
    }
    // And when the emitter goes, listeners to it go too.
    auto e = scripted("Ear", "ear");
    g.world.spawn(e);
    g.frames(2);
    CHECK(g.world.listener_count() == 2);
    g.world.destroy(*api::find(g.world, "Hub"));
    g.frames(1);
    CHECK(g.world.listener_count() == 1);  // only the game-wide one, still owned by the living ear
    CHECK(g.lua->errors().empty());
}

TEST_CASE("messages: a listener that throws is stopped alone, a message loop is cut off with an error") {
    Scripted g(scene_of({scripted("Loop", "loop")}), {{"loop", R"(
        local Loop = {}
        function Loop:on_start()
            noor.on("echo", function() noor.emit("echo") end)          -- answers itself forever
            noor.on("bad", function() error("listener fails") end)
            self.count = 0
            noor.on("bad", function() self.count += 1 end)
        end
        function Loop:on_update()
            if not self.done then
                self.done = true
                noor.emit("echo")
                noor.emit("bad")
                noor.emit("bad")
                self.entity:set("out", { count = self.count })
            end
        end
        return Loop
    )"}});
    g.frames(2);
    CHECK(g.out("Loop")["count"] == 2);  // the second listener heard both
    REQUIRE(g.lua->errors().size() == 1);
    CHECK(g.lua->errors()[0].find("listener fails") != std::string::npos);
    REQUIRE(g.world.message_errors().size() == 1);
    CHECK(g.world.message_errors()[0].find("'echo'") != std::string::npos);
}

TEST_CASE("messages: engine events are signals another script can listen to (a pressure plate and a door)") {
    scene::Entity floor = scripted("Floor", "");
    floor.transform.position = {0, -0.5f, 0};
    floor.components["collider"] = {{"shape", "box"}, {"size", {10, 1, 10}}};
    scene::Entity plate = scripted("Plate", "");
    plate.transform.position = {0, 0.25f, 0};
    plate.components["collider"] = {{"shape", "box"}, {"size", {2, 0.5, 2}}, {"trigger", true}};
    scene::Entity crate = scripted("Crate", "");
    crate.transform.position = {0, 3, 0};
    crate.components["collider"] = {{"shape", "box"}, {"size", {0.5, 0.5, 0.5}}};
    crate.components["rigidbody"] = {{"type", "dynamic"}};
    Scripted g(scene_of({floor, plate, crate, scripted("Door", "door")}), {{"door", R"(
        local Door = {}
        function Door:on_start()
            noor.find("Plate"):on("trigger_enter", function(data, sender)
                self.entity:set("out", { opened_by = data.other, crate = noor.find("Crate"):id(), plate = sender:name() })
            end)
        end
        return Door
    )"}},
               true);
    g.frames(120);
    const Json out = g.out("Door");
    REQUIRE(out.is_object());
    CHECK(out["opened_by"] == out["crate"]);  // entities travel as their ids
    CHECK(out["plate"] == "Plate");
}
