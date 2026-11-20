// Water (Milestone 8D, part 1, ADR-037): the look's numbers (absorption, transmittance, presets, a .noorwater file)
// against values worked out by hand, and a `water` component becoming the renderer's WaterDraw.
#include "noor/asset/water_asset.h"
#include "noor/foundation/guid.h"
#include "noor/game/game.h"
#include "noor/scene/scene.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>

using namespace noor;
using asset::Json;

namespace {

std::filesystem::path write_temp(const std::string& text) {
    const auto path = std::filesystem::temp_directory_path() / ("noor_water_" + Guid::generate().to_string() + ".noorwater");
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

} // namespace

TEST_CASE("water: transmittance is exp(-absorption * metres), colour by colour") {
    asset::WaterLook look;  // the defaults: absorption 0.45, 0.09, 0.06 per metre
    const auto t = asset::water_transmittance(look, 2.0f);
    CHECK(t.x == doctest::Approx(0.40657f).epsilon(1e-4));  // e^-0.90
    CHECK(t.y == doctest::Approx(0.83527f).epsilon(1e-4));  // e^-0.18
    CHECK(t.z == doctest::Approx(0.88692f).epsilon(1e-4));  // e^-0.12
    const auto none = asset::water_transmittance(look, 0.0f);
    CHECK(none.x == 1.0f);
    CHECK(none.z == 1.0f);
    CHECK(asset::water_transmittance(look, -3.0f).x == 1.0f);  // no negative depths
    // Red goes first: 10 m down, under 2% of the red is left but 55% of the blue.
    const auto deep = asset::water_transmittance(look, 10.0f);
    CHECK(deep.x < 0.02f);
    CHECK(deep.z == doctest::Approx(0.5488f).epsilon(1e-3));
}

TEST_CASE("water: a tint and a clarity give the absorption that turns white into the tint over that distance") {
    const auto a = asset::absorption_from_tint({0.5f, 0.25f, 1.0f}, 2.0f);
    CHECK(a.x == doctest::Approx(0.34657f).epsilon(1e-4));  // ln 2 / 2
    CHECK(a.y == doctest::Approx(0.69315f).epsilon(1e-4));  // ln 4 / 2
    CHECK(a.z == doctest::Approx(0.0f));                     // white light keeps all its blue
    asset::WaterLook look;
    look.absorption = a;
    const auto back = asset::water_transmittance(look, 2.0f);  // after "clarity" metres: the tint again
    CHECK(back.x == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(back.y == doctest::Approx(0.25f).epsilon(1e-4));
    // A black tint doesn't divide by zero: it's very murky, not infinitely.
    const auto black = asset::absorption_from_tint({0.0f, 0.0f, 0.0f}, 1.0f);
    CHECK(std::isfinite(black.x));
    CHECK(black.x == doctest::Approx(9.2103f).epsilon(1e-3));  // -ln 1e-4
}

TEST_CASE("water: the Lake preset, worked through by hand") {
    const Json* preset = asset::water_preset("Lake");
    REQUIRE(preset != nullptr);
    CHECK(asset::water_preset("Swamp of Unreasonable Ideas") == nullptr);
    asset::WaterLook look;
    look.apply((*preset)["water"]);
    // Tint #6FB7A8 at 4 m. Red 0x6F = 111/255 = 0.43529 (sRGB) = ((0.43529 + 0.055) / 1.055)^2.4 = 0.15896 linear,
    // so its absorption is -ln 0.15896 / 4 = 0.45977 per metre. Green 0xB7: 0.47353 linear, 0.18688; blue 0xA8: 0.39157, 0.23439.
    CHECK(look.absorption.x == doctest::Approx(0.45977f).epsilon(1e-3));
    CHECK(look.absorption.y == doctest::Approx(0.18688f).epsilon(1e-3));
    CHECK(look.absorption.z == doctest::Approx(0.23439f).epsilon(1e-3));
    // Scatter #1F4A48: 0x1F = 31/255 = 0.12157 sRGB = 0.013702 linear.
    CHECK(look.scatter.x == doctest::Approx(0.013702f).epsilon(1e-3));
    CHECK(look.refraction == doctest::Approx(0.06f));
    CHECK(look.shore_fade == doctest::Approx(0.12f));
    CHECK(look.ripple_scale == doctest::Approx(1.2f));
}

TEST_CASE("water: fields replace the look's, bad values are clamped or ignored") {
    asset::WaterLook look;
    look.apply(Json{{"absorption", {0.1, -2.0, 0.3}}, {"refraction", 9.0}, {"roughness", 0.3}, {"ripple", "lots"},
                    {"scatter", "teal"}, {"tint", "#12345"}, {"unknown", 1}});
    CHECK(look.absorption.x == doctest::Approx(0.1f));
    CHECK(look.absorption.y == 0.0f);       // no negative absorption (light doesn't grow in water)
    CHECK(look.refraction == 0.5f);         // clamped
    CHECK(look.roughness == doctest::Approx(0.3f));
    CHECK(look.ripple == 1.0f);             // not a number: kept
    CHECK(look.scatter.x == doctest::Approx(0.02f));  // not a colour: kept
    // A tint wins over an absorption in the same settings (it's read after it); no clarity: 3 m.
    look.apply(Json{{"absorption", {5, 5, 5}}, {"tint", "#FFFFFF"}});
    CHECK(look.absorption.x == doctest::Approx(0.0f));
    look.apply(Json::array());  // not an object: nothing
    CHECK(look.absorption.x == doctest::Approx(0.0f));
}

TEST_CASE("water: a .noorwater file is checked before it's used") {
    const auto good = write_temp(R"({"noor_water": 1, "name": "Pond", "water": {"tint": "#808080", "clarity": 1}})");
    const auto newer = write_temp(R"({"noor_water": 99, "water": {}})");
    const auto empty = write_temp(R"({"noor_water": 1})");
    const auto other = write_temp(R"({"noor_environment": 1})");
    auto loaded = asset::load_water_file(good);
    REQUIRE(loaded.has_value());
    asset::WaterLook look;
    look.apply((*loaded)["water"]);
    CHECK(look.absorption.x == doctest::Approx(1.5331f).epsilon(1e-3));  // #80 = 0.21586 linear; -ln of it over 1 m
    CHECK_FALSE(asset::load_water_file(newer).has_value());
    CHECK_FALSE(asset::load_water_file(empty).has_value());
    CHECK_FALSE(asset::load_water_file(other).has_value());
    CHECK_FALSE(asset::load_water_file(good.parent_path() / "no_such_water.noorwater").has_value());
    for (const auto& p : {good, newer, empty, other}) std::filesystem::remove(p);
}

TEST_CASE("water: a component becomes a level rectangle where its entity is (sized by its scale, turned with it) or a sea") {
    scene::Scene s;
    scene::Entity lake;
    lake.id = Guid::generate();
    lake.name = "Lake";
    lake.transform.position = {1, 2, 3};
    lake.transform.rotation = math::Quat::from_axis_angle({0, 1, 0}, std::numbers::pi_v<float> / 2);
    lake.transform.scale = {2, 1, 1};
    lake.components["water"] = Json{{"preset", "Lake"}, {"size", {6, 4}}, {"roughness", 0.2}};
    s.entities.push_back(lake);
    scene::Entity sea;  // a sea: level at its height, under the camera (the renderer places it)
    sea.id = Guid::generate();
    sea.name = "Sea";
    sea.transform.position = {0, -1.5f, 0};
    sea.components["water"] = Json{{"shape", "sea"}, {"preset", "Lake"}};
    s.entities.push_back(sea);
    scene::Entity plain;  // no size: 10 by 10 m, the default look
    plain.id = Guid::generate();
    plain.name = "Pond";
    plain.components["water"] = Json::object();
    s.entities.push_back(plain);

    const auto view = game::collect_view(s, {});
    REQUIRE(view.water.size() == 3);
    const auto& w = view.water[0];
    CHECK(w.center.x == doctest::Approx(1.0f));
    CHECK(w.center.y == doctest::Approx(2.0f));
    CHECK(w.center.z == doctest::Approx(3.0f));
    CHECK(w.half_size.x == doctest::Approx(6.0f));  // 6 m times a scale of 2, halved
    CHECK(w.half_size.y == doctest::Approx(2.0f));
    CHECK(w.yaw == doctest::Approx(std::numbers::pi_v<float> / 2).epsilon(1e-4));
    CHECK(w.absorption.x == doctest::Approx(0.45977f).epsilon(1e-3));  // the preset's
    CHECK(w.roughness == doctest::Approx(0.2f));                        // its own field over the preset
    CHECK(w.time == 0.0);                                               // the caller's clock
    CHECK_FALSE(w.endless);
    CHECK(view.water[1].endless);
    CHECK(view.water[1].center.y == doctest::Approx(-1.5f));
    const auto& p = view.water[2];
    CHECK(p.half_size.x == doctest::Approx(5.0f));
    CHECK(p.absorption.x == doctest::Approx(0.45f));
}
