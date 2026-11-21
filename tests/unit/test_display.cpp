// Display settings, camera follow and parallax (2D foundations, part 6): how a game's picture fills the screen
// (render::layout_display), Settings/display.json, the follow step (dead zone, smoothing, bounds) and parallax offsets.
#include "noor/game/camera_follow.h"
#include "noor/game/display.h"
#include "noor/render/renderer.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>

using namespace noor;
using asset::Json;
using render::DisplayDesc;
using render::layout_display;

namespace {
DisplayDesc viewport(std::uint32_t w, std::uint32_t h, bool keep = true, bool integer = true) {
    DisplayDesc d;
    d.stretch = DisplayDesc::Stretch::Viewport;
    d.base_width = w;
    d.base_height = h;
    d.keep_aspect = keep;
    d.integer_scale = integer;
    return d;
}
} // namespace

TEST_CASE("display off: the scene is drawn at the screen's size, times the render scale") {
    DisplayDesc d;
    auto l = layout_display(d, 1920, 1080);
    CHECK(l.render_width == 1920);
    CHECK(l.render_height == 1080);
    CHECK(l.x == 0);
    CHECK(l.y == 0);
    CHECK(l.width == 1920);
    CHECK(l.height == 1080);
    CHECK_FALSE(l.nearest);
    d.render_scale = 0.5f;
    l = layout_display(d, 1920, 1080);
    CHECK(l.render_width == 960);
    CHECK(l.render_height == 540);
    CHECK(l.width == 1920);  // still fills the screen
    d.render_scale = 0.01f;  // clamped to 0.25
    CHECK(layout_display(d, 1920, 1080).render_width == 480);
    // A base resolution without a stretch mode changes nothing.
    d = {};
    d.base_width = 320;
    d.base_height = 180;
    CHECK(layout_display(d, 1280, 720).render_width == 1280);
}

TEST_CASE("display viewport, keep: the base resolution, whole-number steps, centred with black bars") {
    // 320 x 180 at 1280 x 720: exactly 4 x 4 screen pixels per game pixel, no bars.
    auto l = layout_display(viewport(320, 180), 1280, 720);
    CHECK(l.render_width == 320);
    CHECK(l.render_height == 180);
    CHECK(l.width == 1280);
    CHECK(l.height == 720);
    CHECK(l.x == 0);
    CHECK(l.y == 0);
    CHECK(l.nearest);
    // At 1280 x 800: still 4x (no 4.44x blur), bars above and below.
    l = layout_display(viewport(320, 180), 1280, 800);
    CHECK(l.width == 1280);
    CHECK(l.height == 720);
    CHECK(l.y == 40);
    // At 1920 x 1080: 6x. At 2560 x 1440: 8x. At 1366 x 768 (fit 4.27): 4x, centred.
    CHECK(layout_display(viewport(320, 180), 1920, 1080).width == 1920);
    CHECK(layout_display(viewport(320, 180), 2560, 1440).height == 1440);
    l = layout_display(viewport(320, 180), 1366, 768);
    CHECK(l.width == 1280);
    CHECK(l.height == 720);
    CHECK(l.x == 43);
    CHECK(l.y == 24);
    // Not whole numbers: the fit (4.27x), the screen's height filled.
    l = layout_display(viewport(320, 180, true, false), 1366, 768);
    CHECK(l.height == 768);
    CHECK(l.width == 1365);
    // A window smaller than the base: scaled down (the fit), never cropped.
    l = layout_display(viewport(320, 180), 160, 90);
    CHECK(l.width == 160);
    CHECK(l.height == 90);
    CHECK(l.render_width == 320);
}

TEST_CASE("display viewport, expand: more game pixels instead of bars, the same size each") {
    // 320 x 180 at 1280 x 800, 4x: 320 x 200 game pixels, the whole screen.
    auto l = layout_display(viewport(320, 180, false), 1280, 800);
    CHECK(l.render_width == 320);
    CHECK(l.render_height == 200);
    CHECK(l.width == 1280);
    CHECK(l.height == 800);
    CHECK(l.y == 0);
    CHECK(l.view_scale == doctest::Approx(200.0 / 180.0));  // the camera sees 20 game pixels more world, not squashed
    // An ultrawide 2560 x 1080 (6x): 426 x 180, 2556 wide, 2 pixels of bar either side.
    l = layout_display(viewport(320, 180, false), 2560, 1080);
    CHECK(l.render_width == 426);
    CHECK(l.render_height == 180);
    CHECK(l.width == 2556);
    CHECK(l.x == 2);
    CHECK(l.view_scale == doctest::Approx(1.0));  // wider only: the camera's height is the same
    CHECK(layout_display(viewport(320, 180), 1280, 800).view_scale == 1.0f);  // keep: never
}

TEST_CASE("display canvas: the screen's own resolution in the base's shape, render scale for the scene") {
    DisplayDesc d;
    d.stretch = DisplayDesc::Stretch::Canvas;
    d.base_width = 1280;
    d.base_height = 720;
    auto l = layout_display(d, 1920, 1200);  // 16:10 screen, 16:9 game: bars above and below
    CHECK(l.width == 1920);
    CHECK(l.height == 1080);
    CHECK(l.y == 60);
    CHECK(l.render_width == 1920);  // drawn sharp, at the screen's pixels
    CHECK_FALSE(l.nearest);
    d.render_scale = 0.5f;
    l = layout_display(d, 1920, 1200);
    CHECK(l.render_width == 960);
    CHECK(l.render_height == 540);
    d.keep_aspect = false;
    l = layout_display(d, 1920, 1200);
    CHECK(l.width == 1920);
    CHECK(l.height == 1200);
    CHECK(l.y == 0);
    CHECK(l.view_scale == doctest::Approx(1200.0 / 1080.0));
}

TEST_CASE("display settings: Settings/display.json, with defaults for anything missing") {
    auto s = game::display_settings(Json::object());
    CHECK(s.window_width == 1280);
    CHECK(s.window_height == 720);
    CHECK(s.window_mode == game::DisplaySettings::WindowMode::Windowed);
    CHECK(s.display.stretch == DisplayDesc::Stretch::Off);
    CHECK(s.aspect() == doctest::Approx(16.0 / 9.0));

    s = game::display_settings(Json::parse(R"({"window": {"width": 1920, "height": 1080, "mode": "fullscreen"},
        "base_resolution": [320, 180], "aspect": "expand", "integer_scale": false, "render_scale": 9})"));
    CHECK(s.window_width == 1920);
    CHECK(s.window_mode == game::DisplaySettings::WindowMode::Fullscreen);
    CHECK(s.display.stretch == DisplayDesc::Stretch::Viewport);  // a base resolution alone means viewport
    CHECK(s.display.base_width == 320);
    CHECK_FALSE(s.display.keep_aspect);
    CHECK_FALSE(s.display.integer_scale);
    CHECK(s.display.render_scale == 2.0f);  // clamped

    s = game::display_settings(Json::parse(R"({"base_resolution": [640, 480], "stretch": "canvas", "window": {"mode": "borderless"}})"));
    CHECK(s.display.stretch == DisplayDesc::Stretch::Canvas);
    CHECK(s.window_mode == game::DisplaySettings::WindowMode::Borderless);
    CHECK(s.aspect() == doctest::Approx(4.0 / 3.0));  // kept: the base's shape
    // Nonsense is ignored.
    s = game::display_settings(Json::parse(R"({"base_resolution": "big", "window": {"width": "wide"}})"));
    CHECK(s.display.base_width == 0);
    CHECK(s.window_width == 1280);
}

TEST_CASE("camera follow: a dead zone, smoothing that doesn't depend on the frame rate, and level bounds") {
    game::FollowSettings s;
    // No dead zone, no smoothing: on the target at once.
    auto c = game::follow_step({0, 0}, {3, -2}, s, 1.0 / 60.0);
    CHECK(c.x == doctest::Approx(3));
    CHECK(c.y == doctest::Approx(-2));
    // A 4 x 2 dead zone: the target moves freely inside, then the camera is pushed by its edge.
    s.dead_zone = {4, 2};
    c = game::follow_step({0, 0}, {1.5f, 0.5f}, s, 1.0 / 60.0);
    CHECK(c.x == doctest::Approx(0));
    CHECK(c.y == doctest::Approx(0));
    c = game::follow_step({0, 0}, {5, -3}, s, 1.0 / 60.0);
    CHECK(c.x == doctest::Approx(3));   // target 2 m right of centre: the zone's edge
    CHECK(c.y == doctest::Approx(-2));
    // Smoothing: one second at 60 fps or at 30 fps ends in the same place.
    s.dead_zone = {0, 0};
    s.smoothing = 0.25f;
    math::Vec2 a{0, 0}, b{0, 0};
    for (int i = 0; i < 60; ++i) a = game::follow_step(a, {10, 0}, s, 1.0 / 60.0);
    for (int i = 0; i < 30; ++i) b = game::follow_step(b, {10, 0}, s, 1.0 / 30.0);
    CHECK(a.x == doctest::Approx(b.x).epsilon(1e-4));
    CHECK(a.x == doctest::Approx(10.0 * (1.0 - std::exp(-4.0))).epsilon(1e-3));
    // Bounds: the view (16 x 9) stays inside a 0..40 x 0..20 level.
    s.smoothing = 0.0f;
    s.bounded = true;
    s.bounds = {0, 0, 40, 20};
    s.half_view = {8, 4.5f};
    c = game::follow_step({10, 10}, {-5, -5}, s, 1.0 / 60.0);
    CHECK(c.x == doctest::Approx(8));
    CHECK(c.y == doctest::Approx(4.5));
    c = game::follow_step({10, 10}, {100, 100}, s, 1.0 / 60.0);
    CHECK(c.x == doctest::Approx(32));
    CHECK(c.y == doctest::Approx(15.5));
    // A level narrower than the view: centred on it.
    s.bounds = {0, 0, 10, 20};
    c = game::follow_step({0, 10}, {50, 10}, s, 1.0 / 60.0);
    CHECK(c.x == doctest::Approx(5));
}

TEST_CASE("camera follow system: the camera entity follows its target by name, in late_update") {
    scene::Scene scene;
    scene::Entity player;
    player.id = Guid::generate();
    player.name = "Hero";
    player.transform.position = {12, 3, 0};
    scene::Entity cam;
    cam.id = Guid::generate();
    cam.name = "Camera";
    cam.transform.position = {0, 0, 10};
    cam.components["camera"] = Json::parse(R"({"projection": "orthographic", "height": 9})");
    cam.components["camera_follow"] = Json::parse(R"({"target": "Hero", "offset": [0, 1], "bounds": [0, 0, 100, 50]})");
    scene.entities = {player, cam};
    runtime::World world(scene);
    world.add_system(std::make_unique<game::CameraFollowSystem>(16.0f / 9.0f));
    world.frame(1.0 / 60.0, {});
    const auto p = runtime::api::world_position(world, cam.id);
    REQUIRE(p);
    CHECK(p->x == doctest::Approx(12));
    CHECK(p->y == doctest::Approx(4.5));  // 3 + 1 wanted, but the view's bottom edge stops at 0: 9 / 2
    CHECK(p->z == doctest::Approx(10));   // its distance kept
}

TEST_CASE("parallax offset: factor 1 is the world, 0 stays with the camera, between is depth") {
    const auto o = game::parallax_offset({10, 4}, {0, 0}, {0.5f, 1.0f});
    CHECK(o.x == doctest::Approx(5));
    CHECK(o.y == doctest::Approx(0));
    const auto sky = game::parallax_offset({10, 4}, {2, 1}, {0, 0});
    CHECK(sky.x == doctest::Approx(8));
    CHECK(sky.y == doctest::Approx(3));
    // At the anchor nothing moves: a layer is placed as authored when the camera is there.
    const auto at = game::parallax_offset({2, 1}, {2, 1}, {0.3f, 0.3f});
    CHECK(at.x == doctest::Approx(0));
    CHECK(at.y == doctest::Approx(0));
}
