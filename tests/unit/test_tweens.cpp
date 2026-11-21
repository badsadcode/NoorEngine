// Tweens (gameplay foundations, part 4): the easings against their formulas, tweens of transforms, component numbers
// and colours landing exactly on their targets, delay, loops and yoyo, replacing, pausing, and the Lua API.
#include "noor/runtime/script_api.h"
#include "noor/runtime/tween.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace noor;
using asset::Json;
namespace api = runtime::api;

namespace {

constexpr double kStep = 1.0 / 60.0;

struct OneEntity {
    Guid id = Guid::generate();
    runtime::World world;
    OneEntity() : world(make()) {}
    scene::Scene make() {
        scene::Scene s;
        scene::Entity e;
        e.id = id;
        e.name = "Thing";
        e.components["light"] = {{"type", "point"}, {"intensity", 100.0}, {"color", "#000000"}};
        e.components["sprite"] = {{"tint", {0.0, 0.0, 0.0}}};
        s.entities.push_back(e);
        return s;
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) world.frame(kStep, {});
    }
    std::uint64_t tween(std::string_view property, const Json& to, double seconds, std::string_view ease = "linear",
                        runtime::TweenOptions o = {}) {
        const auto r = api::tween(world, id, property, to, seconds, ease, o);
        REQUIRE(std::holds_alternative<std::uint64_t>(r));
        return std::get<std::uint64_t>(r);
    }
    math::Vec3 position() { return *api::position(world, id); }
};

} // namespace

TEST_CASE("tweens: every easing starts at 0, ends at 1, and matches its formula in between") {
    for (const char* name : {"linear", "ease_in", "ease_out", "ease_in_out", "quad_in", "quad_out", "quad_in_out", "cubic_in", "cubic_out",
                             "cubic_in_out", "sine_in", "sine_out", "sine_in_out", "back_in", "back_out", "back_in_out", "elastic_in",
                             "elastic_out", "elastic_in_out", "bounce_in", "bounce_out", "bounce_in_out"}) {
        CAPTURE(name);
        CHECK(*runtime::ease(name, 0.0) == doctest::Approx(0.0).epsilon(1e-9));
        CHECK(*runtime::ease(name, 1.0) == doctest::Approx(1.0).epsilon(1e-9));
    }
    const double pi = std::numbers::pi;
    CHECK(*runtime::ease("linear", 0.3) == doctest::Approx(0.3));
    CHECK(*runtime::ease("ease_in", 0.5) == doctest::Approx(0.25));
    CHECK(*runtime::ease("ease_out", 0.5) == doctest::Approx(0.75));
    CHECK(*runtime::ease("ease_in_out", 0.25) == doctest::Approx(0.125));
    CHECK(*runtime::ease("ease_in_out", 0.75) == doctest::Approx(0.875));
    CHECK(*runtime::ease("cubic_in", 0.5) == doctest::Approx(0.125));
    CHECK(*runtime::ease("cubic_out", 0.5) == doctest::Approx(0.875));
    CHECK(*runtime::ease("sine_out", 0.5) == doctest::Approx(std::sin(pi / 4)));
    CHECK(*runtime::ease("sine_in", 0.5) == doctest::Approx(1 - std::cos(pi / 4)));
    CHECK(*runtime::ease("back_in", 0.5) == doctest::Approx(2.70158 * 0.125 - 1.70158 * 0.25));  // dips below 0 first
    CHECK(*runtime::ease("back_in", 0.2) < 0.0);
    CHECK(*runtime::ease("back_out", 0.8) > 1.0);  // overshoots
    CHECK(*runtime::ease("bounce_out", 0.5) == doctest::Approx(0.765625));
    CHECK(*runtime::ease("elastic_out", 0.5) == doctest::Approx(1 + std::pow(2.0, -5) * std::sin((5 - 0.75) * (2 * pi / 3))));
    CHECK(*runtime::ease("linear", 1.5) == 1.0);  // clamped
    CHECK_FALSE(runtime::ease("wobbly", 0.5));
}

TEST_CASE("tweens: a position moves linearly, lands exactly on target, and an easing shapes the way") {
    OneEntity a;
    a.tween("position", {10, 0, -4}, 1.0);
    a.frames(30);
    CHECK(a.position().x == doctest::Approx(5).epsilon(1e-5));
    CHECK(a.position().z == doctest::Approx(-2).epsilon(1e-5));
    a.frames(40);
    CHECK(a.position().x == 10.0f);  // exactly, not 9.9999
    CHECK(a.position().z == -4.0f);
    CHECK(a.world.tweens().count() == 0);
    // ease_out from 10 to 20 over 1 s: at 0.25 s it's 10 + 10 * (1 - 0.75^2)
    a.tween("position", {20, 0, -4}, 1.0, "ease_out");
    a.frames(15);
    CHECK(a.position().x == doctest::Approx(10 + 10 * (1 - 0.75 * 0.75)).epsilon(1e-4));
}

TEST_CASE("tweens: delay, loops and yoyo, replacing, cancelling, a gone entity, bad requests") {
    OneEntity a;
    // A platform going 0 -> 4 -> 0 -> 4 (three plays, yoyo), after a 0.5 s delay.
    const auto id = a.tween("position", {4, 0, 0}, 1.0, "linear", {.delay = 0.5, .loops = 3, .yoyo = true});
    a.frames(30);
    CHECK(a.position().x == 0.0f);  // still waiting
    a.frames(60);
    CHECK(a.position().x == doctest::Approx(4).epsilon(1e-5));
    a.frames(30);
    CHECK(a.position().x == doctest::Approx(2).epsilon(1e-4));  // on its way back
    a.frames(30);
    CHECK(a.position().x == doctest::Approx(0).epsilon(1e-4));
    CHECK(api::tween_active(a.world, id));
    a.frames(60);
    CHECK(a.position().x == 4.0f);
    CHECK_FALSE(api::tween_active(a.world, id));
    // Forever, until cancelled; a new tween of the same property replaces the old.
    const auto forever = a.tween("position", {0, 0, 0}, 0.5, "linear", {.loops = -1, .yoyo = true});
    a.frames(200);
    CHECK(api::tween_active(a.world, forever));
    const auto replacing = a.tween("position", {1, 1, 1}, 0.1);
    CHECK_FALSE(api::tween_active(a.world, forever));
    api::tween_cancel(a.world, replacing);
    CHECK(a.world.tweens().count() == 0);
    // Its entity goes: it stops, quietly.
    a.tween("scale", {2, 2, 2}, 1.0);
    a.world.destroy(a.id);
    a.frames(2);
    CHECK(a.world.tweens().count() == 0);
    // Bad requests say why.
    OneEntity b;
    const auto bad_property = api::tween(b.world, b.id, "wings", Json::array({1, 2, 3}), 1.0);
    REQUIRE(std::holds_alternative<std::string>(bad_property));
    CHECK(std::get<std::string>(bad_property).find("isn't something this entity has") != std::string::npos);
    const auto bad_ease = api::tween(b.world, b.id, "position", Json::array({1, 2, 3}), 1.0, "wobbly");
    CHECK(std::get<std::string>(bad_ease).find("unknown easing") != std::string::npos);
    const auto bad_value = api::tween(b.world, b.id, "light.intensity", "bright", 1.0);
    CHECK(std::get<std::string>(bad_value).find("can't be tweened") != std::string::npos);
}

TEST_CASE("tweens: component numbers, arrays and colours, and rotation the short way round") {
    OneEntity a;
    a.tween("light.intensity", 300.0, 1.0);
    a.tween("light.color", "#FF8000", 1.0);
    a.tween("sprite.tint", Json::array({1.0, 0.5, 0.0}), 1.0);
    a.tween("rotation", Json::array({0, 270, 0}), 1.0);  // the short way to 270 is -90
    a.frames(30);
    const Json* light = api::component(a.world, a.id, "light");
    CHECK((*light)["intensity"].get<double>() == doctest::Approx(200));
    // Halfway to #FF8000: 255 x 0.5 is 127.5, and after thirty 1/60 s steps "half" is 0.49999..., so 7F or 80.
    const std::string half = (*light)["color"].get<std::string>();
    CHECK((half == "#7F4000" || half == "#804000"));
    CHECK(api::component(a.world, a.id, "sprite")->at("tint")[1].get<double>() == doctest::Approx(0.25));
    CHECK(api::euler(a.world, a.id)->y == doctest::Approx(-45).epsilon(1e-3));
    a.frames(40);
    CHECK((*api::component(a.world, a.id, "light"))["color"] == "#FF8000");
    CHECK(api::euler(a.world, a.id)->y == doctest::Approx(-90).epsilon(1e-3));
    // "euler" goes the long way when asked: 0 to 360 is a full turn.
    a.tween("euler", Json::array({0, 270, 0}), 1.0);
    a.frames(30);
    CHECK(api::euler(a.world, a.id)->y == doctest::Approx(90).epsilon(1e-3));  // halfway from -90 to 270
}

TEST_CASE("tweens: paused, game-time tweens wait and real-time ones carry on, slow motion slows them") {
    OneEntity a;
    a.tween("position", {10, 0, 0}, 1.0);
    a.tween("scale", {3, 3, 3}, 1.0, "linear", {.real = true});
    a.world.set_paused(true);
    a.frames(30);
    CHECK(a.position().x == 0.0f);
    CHECK(api::scale(a.world, a.id)->x == doctest::Approx(2).epsilon(1e-4));
    a.world.set_paused(false);
    a.world.set_time_scale(0.5);
    a.frames(30);
    CHECK(a.position().x == doctest::Approx(2.5).epsilon(1e-4));  // a quarter of the way in 0.5 real s at half speed
}

TEST_CASE("lua tweens: e:tween with on_done, t:wait() in a sequence, errors that say what's wrong") {
    scene::Scene s;
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Door";
    e.components["script"] = {{"file", "Scripts/door.luau"}};
    s.entities.push_back(e);
    runtime::World world(s);
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
    lua->add_source("Scripts/door.luau", R"(
        local Door = {}
        function Door:on_start()
            self.log = {}
            self.entity:tween("position", vector.create(0, 3, 0), 0.5, "ease_out", {
                on_done = function() table.insert(self.log, string.format("up %.2f", noor.time())) end })
            noor.start(function()
                local t = self.entity:tween("scale", vector.create(2, 2, 2), 1.0, "bounce_out")
                t:wait()
                table.insert(self.log, string.format("grown %.2f", noor.time()))
            end)
            local ok, err = pcall(function() self.entity:tween("wings", 1, 1) end)
            self.err = err
        end
        function Door:on_update()
            self.entity:set("out", { log = self.log, y = self.entity:position().y, err = self.err })
        end
        return Door
    )");
    auto* l = lua.get();
    world.add_system(std::move(lua));
    for (int i = 0; i < 80; ++i) world.frame(kStep, {});
    const Json out = *api::component(world, e.id, "out");
    CHECK(out["y"].get<double>() == doctest::Approx(3));
    // on_done and the wait run the frame after the tween ends (before that frame's on_update).
    CHECK(out["log"] == Json::array({"up 0.50", "grown 1.00"}));
    CHECK(out["err"].get<std::string>().find("'wings' isn't something this entity has") != std::string::npos);
    CHECK(l->errors().empty());
}
