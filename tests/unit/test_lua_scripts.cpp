// Lua scripting (Luau, ADR-009) as a runtime system: hooks, API, errors, sandbox, budget.
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <string>

using namespace noor;
using noor::scripting::LuaScriptSystem;

namespace {

struct Fixture {
    Guid id = Guid::generate();  // before `world`: the scene is built from it
    runtime::World world;
    LuaScriptSystem* lua = nullptr;

    explicit Fixture(const std::string& source, const asset::Json& params = asset::Json::object(), double budget = 0.25)
        : world(make_scene(params)) {
        auto system = std::make_unique<LuaScriptSystem>(LuaScriptSystem::Options{".", budget});
        system->add_source("Scripts/test.luau", source);
        lua = system.get();
        world.add_system(std::move(system));
    }

    scene::Scene make_scene(const asset::Json& params) {
        scene::Scene s;
        scene::Entity e;
        e.id = id;
        e.name = "Cube";
        e.components["script"] = {{"file", "Scripts/test.luau"}, {"params", params}};
        s.entities.push_back(e);
        scene::Entity target;
        target.id = Guid::generate();
        target.name = "Target";
        target.transform.position = {0, 0, -10};
        s.entities.push_back(target);
        return s;
    }

    scene::Entity& entity() { return *world.find(id); }
};

} // namespace

TEST_CASE("lua: hooks run with self.entity and self.params, vectors move entities") {
    Fixture f(R"(
        local Mover = {}
        function Mover:on_start()
            self.started = true
            self.entity:set_position(vector.create(1, 2, 3))
        end
        function Mover:on_update(dt)
            self.entity:translate(vector.create(self.params.speed * dt, 0, 0))
        end
        function Mover:on_fixed_update(dt)
            self.fixed = (self.fixed or 0) + 1
            self.entity:set("counter", { fixed = self.fixed })
        end
        return Mover
    )", {{"speed", 4}});
    for (int i = 0; i < 10; ++i) f.world.frame(0.05, {});  // 0.5 s
    CHECK(f.lua->errors().empty());
    CHECK(f.lua->instance_count() == 1);
    CHECK(f.entity().transform.position.x == doctest::Approx(1.0f + 2.0f).epsilon(1e-4));
    CHECK(f.entity().transform.position.y == doctest::Approx(2.0f));
    CHECK(f.entity().components["counter"]["fixed"].get<int>() == static_cast<int>(f.world.fixed_step_count()));
}

TEST_CASE("lua: input actions, find, rotation helpers and directions") {
    Fixture f(R"(
        local S = {}
        function S:on_update(dt)
            if noor.input.pressed("jump") then
                self.entity:rotate(vector.create(0, 1, 0), 90)
            end
            local target = noor.find("Target")
            local d = target:world_position() - self.entity:world_position()
            self.entity:set("seen", { distance = vector.magnitude(d), name = target:name(), fwd = self.entity:forward() })
        end
        return S
    )");
    runtime::InputState jump;
    jump.set(runtime::Key::Space);
    f.world.frame(0.016, jump);
    f.world.frame(0.016, jump);  // held: not pressed again
    CHECK(f.lua->errors().empty());
    const auto e = runtime::api::euler(f.world, f.id);
    REQUIRE(e);
    CHECK(e->y == doctest::Approx(90.0f).epsilon(1e-3));
    const auto& seen = f.entity().components["seen"];
    CHECK(seen["distance"].get<double>() == doctest::Approx(10.0));
    CHECK(seen["name"] == "Target");
    CHECK(seen["fwd"][0].get<double>() == doctest::Approx(-1.0).epsilon(1e-4));  // -Z turned +90 about Y = -X
}

TEST_CASE("lua: a runtime error names the file and line, stops that script, and the game goes on") {
    Fixture f(R"(
        local S = {}
        function S:on_update(dt)
            self.ticks = (self.ticks or 0) + 1
            if self.ticks == 2 then
                local nothing = nil
                nothing.boom()
            end
        end
        return S
    )");
    for (int i = 0; i < 5; ++i) f.world.frame(0.016, {});
    REQUIRE(f.lua->errors().size() == 1);
    CHECK(f.lua->errors()[0].find("Scripts/test.luau:7") != std::string::npos);
    CHECK(f.lua->errors()[0].find("on_update on 'Cube'") != std::string::npos);
    CHECK(f.lua->instance_count() == 0);
    CHECK(f.world.frame_count() == 5);
}

TEST_CASE("lua: syntax errors and missing return are reported when the script loads") {
    Fixture bad_syntax("local S = {\nfunction S:on_update( end\nreturn S");
    bad_syntax.world.frame(0.016, {});
    REQUIRE(bad_syntax.lua->errors().size() == 1);
    CHECK(bad_syntax.lua->errors()[0].find("test.luau:2") != std::string::npos);

    Fixture no_return("local S = {}");
    no_return.world.frame(0.016, {});
    REQUIRE(no_return.lua->errors().size() == 1);
    CHECK(no_return.lua->errors()[0].find("must end with `return") != std::string::npos);
}

TEST_CASE("lua: the sandbox has no file or OS access and built-ins are read-only") {
    Fixture f(R"(
        local S = {}
        function S:on_start()
            local results = {}
            results.io = io == nil
            results.execute = os.execute == nil
            results.loadstring = loadstring == nil
            results.patch_math = not pcall(function() math.floor = nil end)
            results.patch_noor = not pcall(function() noor.log = nil end)
            leaked_global = 42
            self.entity:set("sandbox", results)
        end
        return S
    )");
    f.world.frame(0.016, {});
    CHECK(f.lua->errors().empty());
    const auto& r = f.entity().components["sandbox"];
    CHECK(r["io"] == true);
    CHECK(r["execute"] == true);
    CHECK(r["loadstring"] == true);
    CHECK(r["patch_math"] == true);
    CHECK(r["patch_noor"] == true);
}

TEST_CASE("lua: an endless loop is stopped by the call budget") {
    Fixture f(R"(
        local S = {}
        function S:on_update(dt)
            while true do end
        end
        return S
    )", asset::Json::object(), 0.05);
    f.world.frame(0.016, {});
    REQUIRE(f.lua->errors().size() == 1);
    CHECK(f.lua->errors()[0].find("ran longer than") != std::string::npos);
    f.world.frame(0.016, {});  // stopped, not retried every frame
    CHECK(f.lua->errors().size() == 1);
}

TEST_CASE("lua: destroy runs on_destroy and removes the entity at frame end") {
    Fixture f(R"(
        local S = {}
        function S:on_update(dt)
            if noor.time() > 0.03 then self.entity:destroy() end
        end
        function S:on_destroy()
            noor.find("Target"):set("obituary", { gone = true })
        end
        return S
    )");
    for (int i = 0; i < 4; ++i) f.world.frame(0.016, {});
    CHECK(f.lua->errors().empty());
    CHECK(f.world.find(f.id) == nullptr);
    const auto target = runtime::api::find(f.world, "Target");
    REQUIRE(target);
    CHECK((*runtime::api::component(f.world, *target, "obituary"))["gone"] == true);
}

TEST_CASE("script api: Euler angles use the editor's convention and round-trip") {
    const math::Vec3 in{-20, 135, 10};
    const auto out = runtime::api::euler_from_quat(runtime::api::quat_from_euler(in));
    CHECK(out.x == doctest::Approx(in.x).epsilon(1e-3));
    CHECK(out.y == doctest::Approx(in.y).epsilon(1e-3));
    CHECK(out.z == doctest::Approx(in.z).epsilon(1e-3));
}
