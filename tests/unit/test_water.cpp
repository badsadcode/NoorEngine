// Water (Milestone 8D, part 1, ADR-037): the look's numbers (absorption, transmittance, presets, a .noorwater file)
// against values worked out by hand, and a `water` component becoming the renderer's WaterDraw.
#include "noor/asset/water_asset.h"
#include "noor/foundation/guid.h"
#include "noor/game/game.h"
#include "noor/game/water.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"
#include "noor/scene/scene.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
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

// ---- Waves (part 2) ----

namespace {

game::Waves sea_waves() {
    asset::WaterLook look;
    look.apply((*asset::water_preset("Sea"))["water"]);
    return game::make_waves(look, 0.0f);
}

} // namespace

TEST_CASE("water: waves keep deep water's pace, and the wind raises them") {
    const auto w = sea_waves();
    CHECK(w.height == doctest::Approx(0.6f));
    CHECK(w.length == doctest::Approx(14.0f));
    // Deep water: w = sqrt(g k). The 14 m main wave: k = 2 pi / 14 = 0.44880, w = sqrt(9.81 k) = 2.0983 rad/s, so a
    // period of 2.9944 s: one period on, its phase is back where it started.
    const double period = 2.0 * std::numbers::pi / std::sqrt(9.81 * 2.0 * std::numbers::pi / 14.0);
    CHECK(period == doctest::Approx(2.9944).epsilon(1e-4));
    const auto ph = w.phases(period);
    CHECK((ph[0] < 1e-4f || ph[0] > 2.0f * std::numbers::pi_v<float> - 1e-4f));
    CHECK(w.phases(period / 4)[0] == doctest::Approx(std::numbers::pi_v<float> / 2).epsilon(1e-4));
    // Phases are worked out in double precision: an hour in, still exact (a float time would be off by radians).
    const double hour = 3600.0 + period / 4 - std::fmod(3600.0, period);
    CHECK(w.phases(hour)[0] == doctest::Approx(std::numbers::pi_v<float> / 2).epsilon(1e-3));
    // The wind: the Sea preset takes 0.3 of a fully developed sea's height, 0.21 U^2 / g: at 10 m/s, 0.6422 m more.
    asset::WaterLook look;
    look.apply((*asset::water_preset("Sea"))["water"]);
    CHECK(game::make_waves(look, 10.0f).height == doctest::Approx(0.6f + 0.3f * 0.21f * 100.0f / 9.81f).epsilon(1e-4));
    CHECK(game::make_waves(look, 10.0f).length == doctest::Approx(14.0f));  // the wind doesn't stretch them
    CHECK(game::make_waves(asset::WaterLook{}, 30.0f).flat());              // no "wind": calm whatever blows
}

TEST_CASE("water: the height at a point is the surface the waves moved there") {
    const auto w = sea_waves();
    for (const double t : {0.0, 1.7, 42.25}) {
        const auto ph = w.phases(t);
        for (const math::Vec2 xz : {math::Vec2{0, 0}, math::Vec2{3.3f, -7.1f}, math::Vec2{-120.5f, 64.0f}, math::Vec2{1000.0f, 1000.0f}}) {
            math::Vec3 n;
            const float h = w.height_at(xz, ph, &n);
            // Which rest point the waves moved here: stepping back from xz must land on xz again.
            math::Vec2 rest = xz;
            for (int i = 0; i < 40; ++i) {
                const auto o = w.offset(rest, ph);
                rest = {xz.x - o.x, xz.y - o.z};
            }
            const auto o = w.offset(rest, ph);
            CHECK(rest.x + o.x == doctest::Approx(xz.x).epsilon(1e-5));
            CHECK(h == doctest::Approx(o.y).epsilon(1e-4));
            // At most the four amplitudes added up (0.3 + 0.15 + 0.084 + 0.048 m).
            CHECK(std::abs(h) <= 0.582f + 1e-4f);
            // The normal is the surface's slope: compared with the height 5 cm either way (a centimetre is too fine 1 km out,
            // where a float phase of thousands of radians moves in steps of 2e-4: harmless for the height, not for a slope).
            const float e = 0.05f;
            const float dx = (w.height_at({xz.x + e, xz.y}, ph) - w.height_at({xz.x - e, xz.y}, ph)) / (2 * e);
            const float dz = (w.height_at({xz.x, xz.y + e}, ph) - w.height_at({xz.x, xz.y - e}, ph)) / (2 * e);
            CHECK(-n.x / n.y == doctest::Approx(dx).epsilon(0.02).scale(1.0));
            CHECK(-n.z / n.y == doctest::Approx(dz).epsilon(0.02).scale(1.0));
        }
    }
    // Choppiness 0: plain sines. At the origin at time 0 every wave is at its rest height.
    game::Waves round = w;
    round.choppiness = 0.0f;
    CHECK(round.height_at({0, 0}, round.phases(0.0)) == doctest::Approx(0.0f).scale(1.0));
}

TEST_CASE("water: scripts read the surface through the World, the highest body where they overlap") {
    scene::Scene s;
    scene::Entity sea;
    sea.id = Guid::generate();
    sea.name = "Sea";
    sea.transform.position = {0, -1, 0};
    sea.components["water"] = Json{{"shape", "sea"}, {"preset", "Lake"}};  // calm
    s.entities.push_back(sea);
    scene::Entity pond;
    pond.id = Guid::generate();
    pond.name = "Pond";
    pond.transform.position = {10, 2, 0};
    pond.transform.rotation = math::Quat::from_axis_angle({0, 1, 0}, std::numbers::pi_v<float> / 2);  // turned: 4 m along x
    pond.components["water"] = Json{{"preset", "Lake"}, {"size", {8, 4}}};
    s.entities.push_back(pond);
    scene::Entity buoy;
    buoy.id = Guid::generate();
    buoy.name = "Buoy";
    buoy.transform.position = {11, 0, 3};
    buoy.components["script"] = Json{{"file", "Scripts/float.luau"}, {"params", Json::object()}};
    s.entities.push_back(buoy);
    runtime::World world(std::move(s));
    world.add_system(std::make_unique<game::WaterSystem>(game::AssetPath{}));
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/float.luau", R"(
        local F = {}
        function F:on_update(dt)
            local p = self.entity:position()
            local h = noor.water_height(p.x, p.z)
            local n = noor.water_normal(p.x, p.z)
            if h and n.y > 0.99 then self.entity:set_position(vector.create(p.x, h, p.z)) end
            self.entity:set("seen", {nothing = noor.water_height(p.x, p.z) == nil, far = noor.water_height(0, 1e6)})
        end
        return F
    )");
    const auto* system = lua.get();
    world.add_system(std::move(lua));
    world.frame(1.0 / 60.0, {});
    world.frame(1.0 / 60.0, {});
    CHECK(system->errors().empty());
    // In the pond (turned, so 4 m wide along x and 8 m along z: (11, 3) is inside), the pond is above the sea.
    CHECK(runtime::api::water_height(world, 11.0f, 3.0f) == doctest::Approx(2.0f));
    CHECK(runtime::api::water_height(world, 13.0f, 0.0f) == doctest::Approx(-1.0f));  // outside the turned pond: the sea
    CHECK(world.find(buoy.id)->transform.position.y == doctest::Approx(2.0f));          // the script floated it
    CHECK(world.find(buoy.id)->components["seen"]["far"].get<float>() == doctest::Approx(-1.0f));  // the sea is everywhere
    // Without a water system nobody answers.
    runtime::World dry(scene::Scene{});
    CHECK_FALSE(runtime::api::water_height(dry, 0.0f, 0.0f).has_value());
}

// ---- Shorelines (part 3) ----------------------------------------------------------------------------------------

namespace {

// A hand-made depth map: `depth(x, z)` sampled every half metre over 20 x 20 m round the origin.
template <typename F>
std::shared_ptr<render::WaterDepthMap> depth_map(F depth) {
    auto m = std::make_shared<render::WaterDepthMap>();
    m->origin = {-10.0f, -10.0f};
    m->cell = 0.5f;
    m->nx = m->nz = 41;
    for (std::uint32_t z = 0; z < m->nz; ++z)
        for (std::uint32_t x = 0; x < m->nx; ++x) m->depth.push_back(depth(-10.0f + 0.5f * static_cast<float>(x), -10.0f + 0.5f * static_cast<float>(z)));
    return m;
}

} // namespace

TEST_CASE("water: waves calm where the water is shallow for them, and stop where the ground is above it") {
    // What's left of a wave: tanh(k depth), as a Pade fit: 0 at 0 (and over dry ground), 1 from 3 on, close to tanh.
    CHECK(game::shoal(0.0f) == 0.0f);
    CHECK(game::shoal(-2.0f) == 0.0f);
    CHECK(game::shoal(3.0f) == 1.0f);
    CHECK(game::shoal(7.0f) == 1.0f);
    for (float x = 0.05f; x < 3.0f; x += 0.05f) {
        CHECK(std::abs(game::shoal(x) - std::tanh(x)) < 0.025f);
        float slope = 0.0f;
        (void)game::shoal(x, &slope);
        const float e = 1e-3f;
        CHECK(slope == doctest::Approx((game::shoal(x + e) - game::shoal(x - e)) / (2 * e)).epsilon(0.01).scale(1.0));
    }

    const auto w = sea_waves();
    const float k_main = 2.0f * std::numbers::pi_v<float> / w.length;
    const float k_small = k_main / game::kWaveLength[3];
    // Long waves feel the bottom first.
    CHECK(game::shoal(k_main * 0.5f) < game::shoal(k_small * 0.5f));

    // Half a metre of water everywhere: every wave is cut to its share, so the surface stays within their sum.
    const auto shallow = depth_map([](float, float) { return 0.5f; });
    float limit = 0.0f;
    for (int i = 0; i < game::kWaveCount; ++i) {
        const float k = 2.0f * std::numbers::pi_v<float> / (w.length * game::kWaveLength[static_cast<std::size_t>(i)]);
        limit += 0.5f * w.height * game::kWaveAmplitude[static_cast<std::size_t>(i)] * game::shoal(k * 0.5f);
    }
    CHECK(limit < 0.3f);  // the deep sea's sum is 0.582 m
    float highest = 0.0f;
    for (const double t : {0.0, 0.8, 1.9, 2.6})
        for (float x = -6.0f; x <= 6.0f; x += 1.5f) highest = std::max(highest, std::abs(w.height_at({x, 1.0f}, w.phases(t), nullptr, shallow.get())));
    CHECK(highest > 0.02f);  // still moving
    CHECK(highest <= limit + 1e-4f);

    // Dry ground: nothing moves at all, the surface is level (under the ground, so never seen).
    const auto dry = depth_map([](float, float) { return -0.5f; });
    math::Vec3 n;
    CHECK(w.height_at({1.0f, 2.0f}, w.phases(1.3), &n, dry.get()) == 0.0f);
    CHECK(n.y == 1.0f);
    // Off the map: deep water, the full waves.
    CHECK(game::depth_at(shallow.get(), {30.0f, 0.0f}) == game::kDeepWater);
    CHECK(w.height_at({30.0f, 2.0f}, w.phases(1.3), nullptr, shallow.get()) == w.height_at({30.0f, 2.0f}, w.phases(1.3)));
}

TEST_CASE("water: over a shelving bottom the normal is still the surface's slope, and the depth is read bilinearly") {
    // A bottom shelving from 0.1 m deep at x = -10 to 4.1 m at x = 10.
    const auto shelf = depth_map([](float x, float) { return 0.1f + 0.2f * (x + 10.0f); });
    math::Vec2 grad;
    CHECK(game::depth_at(shelf.get(), {0.25f, 3.3f}, &grad) == doctest::Approx(2.15f).epsilon(1e-5));
    CHECK(grad.x == doctest::Approx(0.2f).epsilon(1e-4));
    CHECK(grad.y == doctest::Approx(0.0f).scale(1.0));
    const auto w = sea_waves();
    for (const double t : {0.4, 2.2})
        for (const math::Vec2 xz : {math::Vec2{-8.0f, 1.0f}, math::Vec2{-4.3f, -2.2f}, math::Vec2{0.6f, 5.1f}, math::Vec2{6.0f, -6.0f}}) {
            const auto ph = w.phases(t);
            math::Vec3 n;
            (void)w.height_at(xz, ph, &n, shelf.get());
            const float e = 0.05f;
            const float dx = (w.height_at({xz.x + e, xz.y}, ph, nullptr, shelf.get()) - w.height_at({xz.x - e, xz.y}, ph, nullptr, shelf.get())) / (2 * e);
            const float dz = (w.height_at({xz.x, xz.y + e}, ph, nullptr, shelf.get()) - w.height_at({xz.x, xz.y - e}, ph, nullptr, shelf.get())) / (2 * e);
            CHECK(-n.x / n.y == doctest::Approx(dx).epsilon(0.02).scale(1.0));
            CHECK(-n.z / n.y == doctest::Approx(dz).epsilon(0.02).scale(1.0));
        }
}

TEST_CASE("water: a body's depth map comes from the terrain under it") {
    terrain::Settings flat;
    flat.tiles_x = flat.tiles_z = 1;
    flat.base = terrain::Settings::Base::Flat;
    flat.flat_height = 0.0f;
    auto heights = terrain::Heights::shared(flat);
    REQUIRE(heights);
    const std::vector<game::ParticleGround::Piece> ground{{{0.0f, -2.0f, 0.0f}, *heights}};  // 256 m square, 2 m under the water

    // A 20 x 16 m pond at the water level 0: two metres deep everywhere, samples a quarter metre apart over its bounds.
    const auto pond = game::water_depth_map(false, {3.0f, 0.0f, -1.0f}, {10.0f, 8.0f}, 0.0f, ground);
    REQUIRE(pond);
    CHECK(pond->cell == 0.25f);
    CHECK(pond->nx == 81);
    CHECK(pond->nz == 65);
    CHECK(pond->origin.x == -7.0f);
    CHECK(pond->origin.y == -9.0f);
    CHECK(pond->depth.size() == 81u * 65u);
    CHECK(std::ranges::all_of(pond->depth, [](float d) { return d == 2.0f; }));
    // Asked again (gameplay and drawing both do, every frame): the same map.
    CHECK(game::water_depth_map(false, {3.0f, 0.0f, -1.0f}, {10.0f, 8.0f}, 0.0f, ground) == pond);
    // Turned 30 degrees: it covers the turned footprint's bounds.
    const auto turned = game::water_depth_map(false, {0.0f, 0.0f, 0.0f}, {10.0f, 8.0f}, std::numbers::pi_v<float> / 6.0f, ground);
    REQUIRE(turned);
    CHECK(turned->origin.x == doctest::Approx(-(10.0f * std::cos(std::numbers::pi_v<float> / 6.0f) + 8.0f * 0.5f)));
    // A sea: the terrain's square (beyond it, deep). A pond off the terrain, or no terrain at all: none.
    const auto sea = game::water_depth_map(true, {0.0f, 0.0f, 0.0f}, {}, 0.0f, ground);
    REQUIRE(sea);
    CHECK(sea->origin.x == -128.0f);
    CHECK(sea->nx == 256);
    CHECK(game::depth_at(sea.get(), {0.0f, 0.0f}) == 2.0f);
    CHECK(game::depth_at(sea.get(), {200.0f, 0.0f}) == game::kDeepWater);
    CHECK(!game::water_depth_map(false, {1000.0f, 0.0f, 0.0f}, {10.0f, 10.0f}, 0.0f, ground));
    CHECK(!game::water_depth_map(true, {0.0f, 0.0f, 0.0f}, {}, 0.0f, {}));
}
