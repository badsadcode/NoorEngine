// The HUD: script API (colors, anchors, commands), layout (placement, measuring) and the built-in font.
#include "noor/game/hud.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace noor;
using runtime::UiAnchor;
namespace api = runtime::api;

TEST_CASE("hud: colors are sRGB hex (made linear), anchors have names, bad ones are refused") {
    const auto white = api::parse_color("#FFFFFF");
    REQUIRE(white);
    CHECK(white->x == doctest::Approx(1.0f));
    const auto grey = api::parse_color("#808080");  // sRGB 50% grey is ~21.6% linear
    REQUIRE(grey);
    CHECK(grey->x == doctest::Approx(0.2158f).epsilon(0.01));
    const auto half = api::parse_color("#FF000080");
    REQUIRE(half);
    CHECK(half->w == doctest::Approx(128.0f / 255.0f));
    CHECK_FALSE(api::parse_color("red"));
    CHECK_FALSE(api::parse_color("#12345"));
    CHECK_FALSE(api::parse_color("#GG0000"));
    CHECK(api::parse_anchor("center") == UiAnchor::Center);
    CHECK(api::parse_anchor("bottom_right") == UiAnchor::BottomRight);
    CHECK_FALSE(api::parse_anchor("centre"));
}

TEST_CASE("hud: anchors place an element's matching point at screen point + offset") {
    const math::Vec2 screen{1280, 720}, box{100, 50};
    auto at = [&](UiAnchor a, math::Vec2 offset) { return game::Hud::place(a, offset, box, screen); };
    CHECK(at(UiAnchor::TopLeft, {10, 20}).x == doctest::Approx(10));
    CHECK(at(UiAnchor::TopLeft, {10, 20}).y == doctest::Approx(20));
    CHECK(at(UiAnchor::Center, {0, 0}).x == doctest::Approx(590));   // centred: 640 - 50
    CHECK(at(UiAnchor::Center, {0, 0}).y == doctest::Approx(335));   // 360 - 25
    CHECK(at(UiAnchor::BottomRight, {-20, -20}).x == doctest::Approx(1160));  // 1280 - 20 - 100
    CHECK(at(UiAnchor::BottomRight, {-20, -20}).y == doctest::Approx(650));   // 720 - 20 - 50
}

TEST_CASE("hud: the built-in font covers Polish and common symbols, text measures sensibly") {
    const game::Hud hud;
    for (char32_t c : {U'A', U'z', U'0', U'ą', U'ł', U'Ż', U'ś', U'é', U'ß', U'€', U'°', U'—', U'…'}) CHECK(hud.has_glyph(c));
    CHECK(hud.atlas_width() == 1024);
    CHECK(hud.atlas().size() == static_cast<std::size_t>(hud.atlas_width()) * hud.atlas_height());
    const auto one = hud.measure("Score", 24);
    const auto two = hud.measure("Score", 48);
    CHECK(one.y == doctest::Approx(24));
    CHECK(two.x == doctest::Approx(one.x * 2).epsilon(0.001));  // scales with size
    CHECK(hud.measure("Score: 10", 24).x > one.x);
    CHECK(hud.measure("ab\nabcd", 24).y == doctest::Approx(48));  // two lines
    CHECK(hud.measure("ab\nabcd", 24).x == doctest::Approx(hud.measure("abcd", 24).x));  // widest line
}

TEST_CASE("hud: commands become 6 vertices per rectangle and per visible glyph, in pixels") {
    const game::Hud hud;
    runtime::World world{scene::Scene{}};
    api::ui_rect(world, {10, 10}, {60, 60}, {0, 1, 0, 1}, UiAnchor::TopLeft);
    api::ui_text(world, "Zażółć gęślą", {0, 0}, 32, {1, 1, 1, 1}, UiAnchor::Center);  // 11 visible glyphs
    std::vector<render::OverlayVertex> out;
    hud.build(world.ui_commands(), 1920, 1080, out);  // 1080 px tall: one UI unit = 1.5 px
    CHECK(out.size() == 6 + 11 * 6);
    CHECK(out[0].position.x == doctest::Approx(15));  // UI 10 -> 15 px
    CHECK(out[2].position.x == doctest::Approx(105)); // UI 70 -> 105 px
    CHECK(out[0].uv.x < 0);                           // solid
    CHECK(out[6].uv.x >= 0);                          // text samples the atlas
    world.frame(1.0 / 60.0, {});
    CHECK(world.ui_commands().empty());  // immediate mode: drawn again every frame or gone
}

TEST_CASE("hud + lua: noor.ui.text and noor.ui.rect with options, bad options are script errors") {
    const auto dir = std::filesystem::temp_directory_path() / ("noor_hud_" + Guid::generate().to_string());
    std::filesystem::create_directories(dir / "Scripts");
    std::ofstream(dir / "Scripts" / "Hud.luau") << R"(
        local Hud = {}
        function Hud:on_update(dt)
            noor.ui.rect(0, 0, 100, 8, { color = "#FF000080", anchor = "bottom" })
            noor.ui.text("Hi", 5, 6, { size = 40, anchor = "center" })
            noor.ui.text("plain", 1, 2)
        end
        return Hud
    )";
    std::ofstream(dir / "Scripts" / "Bad.luau") << R"(
        local Bad = {}
        function Bad:on_update(dt) noor.ui.text("x", 0, 0, { anchor = "middle" }) end
        return Bad
    )";
    scene::Scene s;
    for (const char* file : {"Scripts/Hud.luau", "Scripts/Bad.luau"}) {
        scene::Entity e;
        e.id = Guid::generate();
        e.name = file;
        e.components["script"] = {{"file", file}};
        s.entities.push_back(e);
    }
    runtime::World world(std::move(s));
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{dir});
    auto* scripts = lua.get();
    world.add_system(std::move(lua));
    world.frame(1.0 / 60.0, {});
    const auto& ui = world.ui_commands();
    REQUIRE(ui.size() == 3);
    CHECK(ui[0].kind == runtime::UiCommand::Kind::Rect);
    CHECK(ui[0].anchor == UiAnchor::Bottom);
    CHECK(ui[0].color.w == doctest::Approx(128.0f / 255.0f));
    CHECK(ui[1].text == "Hi");
    CHECK(ui[1].text_size == doctest::Approx(40));
    CHECK(ui[1].anchor == UiAnchor::Center);
    CHECK(ui[2].anchor == UiAnchor::TopLeft);  // defaults
    CHECK(ui[2].text_size == doctest::Approx(24));
    bool reported = false;
    for (const auto& e : scripts->errors()) reported = reported || e.find("anchor 'middle'") != std::string::npos;
    CHECK(reported);
    std::filesystem::remove_all(dir);
}
