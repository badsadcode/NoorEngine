// Weather zones (Milestone 8A, part 7): blending environments and how much of a zone applies where.
#include "noor/asset/atmosphere.h"
#include "noor/asset/environment_asset.h"
#include "noor/game/game.h"
#include "noor/game/weather.h"
#include "noor/runtime/world.h"
#include "noor/scene/scene.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>

using namespace noor;
using asset::Json;

TEST_CASE("weather: environments blend number by number, colour by colour, switch by switch") {
    const Json clear{{"sky", "atmosphere"}, {"time_of_day", 23.0}, {"turbidity", 2.0}, {"fog_color", "#000000"}, {"stars", true}, {"ev100", "auto"}};
    const Json foggy{{"sky", "atmosphere"}, {"time_of_day", 1.0}, {"turbidity", 4.0}, {"fog_color", "#FFFFFF"}, {"stars", false},
                     {"ev100", 12.0}, {"fog_density", 0.04}, {"cloud_coverage", 0.8}, {"cloud_altitude", 1200}};
    CHECK(asset::blend_environments(clear, foggy, 0.0f) == clear);
    CHECK(asset::blend_environments(clear, foggy, 1.0f) == foggy);
    const Json half = asset::blend_environments(clear, foggy, 0.5f);
    CHECK(half["turbidity"] == doctest::Approx(3.0));
    CHECK(half["time_of_day"] == doctest::Approx(0.0));        // 23:00 to 01:00 the short way: through midnight
    CHECK(half["fog_density"] == doctest::Approx(0.02));       // from no fog
    CHECK(half["cloud_coverage"] == doctest::Approx(0.4));     // from no cloud
    CHECK(half["cloud_altitude"] == 1200);                     // no neutral altitude: taken as it is
    CHECK(half["fog_color"] == "#BCBCBC");                     // halfway in linear light, not "#808080"
    CHECK(half["stars"] == false);                             // switches at the halfway point
    CHECK(asset::blend_environments(clear, foggy, 0.49f)["stars"] == true);
    CHECK(half["ev100"] == 12.0);
    // Seeds never blend (a half seed is a different sky).
    CHECK(asset::blend_environments(Json{{"cloud_seed", 2}}, Json{{"cloud_seed", 9}}, 0.4f)["cloud_seed"] == 2);
    // Fully on, a zone that only says fog keeps the scene's sky and time (it once replaced the whole environment).
    const Json fog_only = asset::blend_environments(clear, Json{{"fog_density", 0.04}}, 1.0f);
    CHECK(fog_only["sky"] == "atmosphere");
    CHECK(fog_only["time_of_day"] == 23.0);
    CHECK(fog_only["turbidity"] == 1.0);  // back to its neutral
    CHECK(fog_only["fog_density"] == 0.04);
    // A clear zone inside a foggy scene takes the fog away.
    CHECK(asset::blend_environments(Json{{"fog_density", 0.04}}, Json::object(), 0.75f)["fog_density"] == doctest::Approx(0.01));
}

TEST_CASE("weather: a zone is on inside, fades smoothly over its blend distance outside") {
    const Json box{{"shape", "box"}, {"size", {40, 20, 40}}, {"blend", 10}};
    const math::Mat4 at = math::translation({100, 0, 0});
    CHECK(game::weather_zone_weight(box, at, {100, 0, 0}) == 1.0f);
    CHECK(game::weather_zone_weight(box, at, {119, 0, 0}) == 1.0f);   // inside (half size 20)
    CHECK(game::weather_zone_weight(box, at, {125, 0, 0}) == doctest::Approx(0.5f));  // halfway through the blend
    CHECK(game::weather_zone_weight(box, at, {130.5f, 0, 0}) == 0.0f);
    // Smooth: no jump anywhere along a walk in.
    float last = 0.0f;
    for (float x = 140.0f; x >= 100.0f; x -= 0.25f) {
        const float w = game::weather_zone_weight(box, at, {x, 0, 0});
        CHECK(w >= last);
        CHECK(w - last < 0.05f);
        last = w;
    }
    // The box turns with its entity.
    const math::Mat4 turned = math::compose({0, 0, 0}, math::Quat::from_axis_angle({0, 1, 0}, math::radians(90.0f)), {1, 1, 1});
    const Json long_box{{"size", {100, 10, 10}}, {"blend", 1}};
    CHECK(game::weather_zone_weight(long_box, turned, {0, 0, 40}) == 1.0f);  // its long side now runs along Z
    CHECK(game::weather_zone_weight(long_box, turned, {40, 0, 0}) == 0.0f);
    // Spheres; and a script's weight wins.
    const Json ball{{"shape", "sphere"}, {"radius", 10}, {"blend", 10}};
    CHECK(game::weather_zone_weight(ball, math::Mat4::identity(), {0, 15, 0}) == doctest::Approx(0.5f));
    CHECK(game::weather_zone_weight(Json{{"shape", "sphere"}, {"radius", 10}, {"weight", 0.3}}, math::Mat4::identity(), {0, 99, 0}) ==
          doctest::Approx(0.3f));
}

TEST_CASE("weather: the scene's view blends a zone's environment by where the camera is") {
    const auto view_at = [](float camera_z) {
        scene::Scene s;
        scene::Entity cam;
        cam.id = Guid::generate();
        cam.name = "Camera";
        cam.transform.position = {0, 2, camera_z};
        cam.components["camera"] = Json{{"fov", 60}, {"ev100", 14}};
        s.entities.push_back(cam);
        scene::Entity env;
        env.id = Guid::generate();
        env.name = "Sky";
        env.components["environment"] = Json{{"sky", "atmosphere"}, {"time_of_day", 12.0}};
        s.entities.push_back(env);
        scene::Entity zone;
        zone.id = Guid::generate();
        zone.name = "Fog bank";
        zone.transform.position = {0, 0, -100};
        zone.components["weather_zone"] = Json{{"size", {60, 40, 60}}, {"blend", 20},
                                               {"environment", {{"sky", "atmosphere"}, {"time_of_day", 12.0}, {"fog_density", 0.03}, {"fog_height", 5}}}};
        s.entities.push_back(zone);
        return game::collect_view(s, {});
    };
    CHECK(view_at(0.0f).lighting.fog.density == 0.0f);                          // far outside: clear
    CHECK(view_at(-60.0f).lighting.fog.density == doctest::Approx(0.015f));      // halfway through the blend
    CHECK(view_at(-100.0f).lighting.fog.density == doctest::Approx(0.03f));      // inside: the zone's fog
    const auto inside = view_at(-100.0f);
    REQUIRE(inside.weather.size() == 1);
    CHECK(inside.weather[0].weight == 1.0f);
}

TEST_CASE("weather: scripts roll a storm in by a zone's weight, and move the time of day") {
    // A zone far from the camera, turned up by its own script (weight 0 to 1 over 2 s); the sky's script runs the clock.
    scene::Scene s;
    scene::Entity cam;
    cam.id = Guid::generate();
    cam.name = "Camera";
    cam.components["camera"] = Json{{"fov", 60}};
    s.entities.push_back(cam);
    scene::Entity sky;
    sky.id = Guid::generate();
    sky.name = "Sky";
    sky.components["environment"] = Json{{"sky", "atmosphere"}, {"time_of_day", 12.0}};
    sky.components["script"] = Json{{"file", "Scripts/clock.luau"}, {"params", Json::object()}};
    s.entities.push_back(sky);
    scene::Entity storm;
    storm.id = Guid::generate();
    storm.name = "Storm";
    storm.transform.position = {0, 0, -5000};
    storm.components["weather_zone"] = Json{{"weight", 0.0}, {"environment", {{"fog_density", 0.04}, {"turbidity", 4.0}}}};
    storm.components["script"] = Json{{"file", "Scripts/storm.luau"}, {"params", Json::object()}};
    s.entities.push_back(storm);
    runtime::World world(std::move(s));
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/storm.luau", R"(
        local Storm = {}
        function Storm:on_update(dt)
            local zone = self.entity:get("weather_zone")
            zone.weight = math.min(1, zone.weight + dt * 0.5)
            self.entity:set("weather_zone", zone)
        end
        return Storm
    )");
    lua->add_source("Scripts/clock.luau", R"(
        local Clock = {}
        function Clock:on_update(dt)
            local env = self.entity:get("environment")
            env.time_of_day = (env.time_of_day + dt) % 24  -- an hour a second
            self.entity:set("environment", env)
        end
        return Clock
    )");
    const auto* system = lua.get();
    world.add_system(std::move(lua));
    float last_fog = -1.0f;
    for (int frame = 0; frame < 30; ++frame) {  // 3 s at 10 frames a second
        world.frame(0.1, {});
        const auto view = game::collect_view(world.scene(), {});
        CHECK(view.lighting.fog.density >= last_fog);
        last_fog = view.lighting.fog.density;
    }
    CHECK(system->errors().empty());
    const auto view = game::collect_view(world.scene(), {});
    REQUIRE(view.weather.size() == 1);
    CHECK(view.weather[0].weight == doctest::Approx(1.0f));
    CHECK(view.lighting.fog.density == doctest::Approx(0.04f));
    CHECK(view.environment.atmosphere_settings.turbidity == doctest::Approx(4.0f));
    // Three hours on: 15:00, the sun where the clock says, lower than at noon.
    CHECK(world.find(sky.id)->components["environment"]["time_of_day"].get<double>() == doctest::Approx(15.0).epsilon(0.01));
    CHECK(view.environment.to_sun.y == doctest::Approx(asset::solar_direction(15.0f, 50.0f, 172.0f).y).epsilon(0.01));
    CHECK(view.environment.to_sun.y < asset::solar_direction(12.0f, 50.0f, 172.0f).y);
}
