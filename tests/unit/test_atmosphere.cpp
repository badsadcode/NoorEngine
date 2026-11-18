// The physically based atmosphere (Milestone 8A, part 1, ADR-033), checked against physics we can work
// out by hand, not against how it looks.
#include "noor/asset/atmosphere.h"
#include "noor/game/game.h"
#include "noor/scene/scene.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>

using namespace noor;
using math::Vec3;

namespace {

const asset::Atmosphere& earth() {
    static const auto a = asset::Atmosphere::shared({});
    return *a;
}

Vec3 sun_at(float elevation_deg) {
    const float e = elevation_deg * 3.14159265f / 180.0f;
    return {0.0f, std::sin(e), -std::cos(e)};
}

float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

} // namespace

TEST_CASE("atmosphere: straight up from sea level, transmittance is exp(-optical depth) worked out by hand") {
    // Optical depth of each layer straight up: Rayleigh beta*H, Mie (scattering+absorption)*H, ozone's
    // tent integrates to peak*half_width (the top at 100 km cuts off a negligible exp(-12.5)).
    const asset::AtmosphereSettings s;
    const Vec3 tau = s.rayleigh_scattering * s.rayleigh_height_km + Vec3{1, 1, 1} * ((s.mie_scattering + s.mie_absorption) * s.mie_height_km) +
                     s.ozone_absorption * s.ozone_half_width_km;
    const Vec3 t = earth().transmittance_exact(0.0f, 1.0f);
    CHECK(t.x == doctest::Approx(std::exp(-tau.x)).epsilon(0.01));
    CHECK(t.y == doctest::Approx(std::exp(-tau.y)).epsilon(0.01));
    CHECK(t.z == doctest::Approx(std::exp(-tau.z)).epsilon(0.01));
    // The published figures for this model's Earth (about 0.94, 0.86, 0.76):
    CHECK(t.x == doctest::Approx(0.936f).epsilon(0.01));
    CHECK(t.y == doctest::Approx(0.864f).epsilon(0.01));
    CHECK(t.z == doctest::Approx(0.758f).epsilon(0.01));
}

TEST_CASE("atmosphere: at the horizon the air mass matches Chapman's sqrt(pi R / 2H)") {
    asset::AtmosphereSettings s;  // Rayleigh only, so one scale height
    s.mie_scattering = s.mie_absorption = 0.0f;
    s.ozone_absorption = {0, 0, 0};
    const asset::Atmosphere a(s);
    const float zenith = -std::log(a.transmittance_exact(0.0f, 1.0f).z);
    const float horizon = -std::log(a.transmittance_exact(0.0f, 0.0f).z);
    const float chapman = std::sqrt(3.14159265f * s.planet_radius_km / (2.0f * s.rayleigh_height_km));  // about 35.3
    CHECK(horizon / zenith == doctest::Approx(chapman).epsilon(0.05));
}

TEST_CASE("atmosphere: the transmittance table agrees with exact marches") {
    const auto& a = earth();
    for (const float h : {0.0f, 0.5f, 2.0f, 10.0f, 40.0f})
        for (const float mu : {1.0f, 0.7f, 0.3f, 0.1f, 0.02f, 0.0f, -0.02f}) {
            CAPTURE(h);
            CAPTURE(mu);
            const Vec3 exact = a.transmittance_exact(h, mu), table = a.transmittance(h, mu);
            CHECK(table.x == doctest::Approx(exact.x).epsilon(0.03).scale(0.01));
            CHECK(table.z == doctest::Approx(exact.z).epsilon(0.03).scale(0.01));
        }
    CHECK(a.transmittance(0.0f, -0.5f).x == 0.0f);  // into the ground
}

TEST_CASE("atmosphere: the sun at the horizon is red, at noon nearly white") {
    const auto& a = earth();
    const Vec3 low = a.sun_transmittance(sun_at(2.0f));
    CHECK(low.x > 3.0f * low.z);  // red-dominant
    CHECK(low.x > low.y);
    const Vec3 high = a.sun_transmittance(sun_at(80.0f));
    CHECK(high.z / high.x > 0.75f);
    CHECK(a.sun_transmittance(sun_at(-5.0f)).x == 0.0f);  // set
}

TEST_CASE("atmosphere: the day sky is blue and as bright as the CIE clear-sky formula says") {
    const auto& a = earth();
    const Vec3 sun = sun_at(60.0f);
    const Vec3 zenith = a.sky_radiance({0, 1, 0}, sun);
    CHECK(zenith.z > zenith.x);
    CHECK(zenith.z > zenith.y);
    // Measured clear skies (the CIE's zenith luminance formula, Linke turbidity T):
    //   Lz [kcd/m2] = (1.376 T - 1.81) tan(sun elevation) + 0.38
    // For a very clear sky (T = 2), relative to the sun outside the atmosphere (about 128 klx).
    for (const float elevation : {30.0f, 45.0f, 60.0f}) {
        CAPTURE(elevation);
        const float cie = ((1.376f * 2.0f - 1.81f) * std::tan(elevation * 3.14159265f / 180.0f) + 0.38f) / 128.0f;
        CHECK(luminance(a.sky_radiance({0, 1, 0}, sun_at(elevation))) == doctest::Approx(cie).epsilon(0.35));
    }
    // The horizon is brighter and paler than the zenith (the long path through the air).
    const Vec3 horizon = a.sky_radiance(math::normalize(Vec3{1, 0.02f, 0}), sun);
    CHECK(luminance(horizon) > luminance(zenith));
    CHECK(horizon.x / horizon.z > zenith.x / zenith.z);
}

TEST_CASE("atmosphere: at sunset the horizon under the sun glows warm and ozone keeps the zenith blue") {
    const auto& a = earth();
    const Vec3 sun = sun_at(1.0f);
    const Vec3 toward = a.sky_radiance(math::normalize(Vec3{0, 0.05f, -1}), sun);
    const Vec3 away = a.sky_radiance(math::normalize(Vec3{0, 0.05f, 1}), sun);
    CHECK(toward.x > toward.z);
    CHECK(luminance(toward) > 3.0f * luminance(away));
    // Hulburt (1953): without ozone the twilight zenith would turn grey-yellow; its absorption of
    // orange light is what keeps it blue.
    const Vec3 zenith = a.sky_radiance({0, 1, 0}, sun);
    CHECK(zenith.z > zenith.x);
    asset::AtmosphereSettings no_ozone;
    no_ozone.ozone_absorption = {0, 0, 0};
    const Vec3 bare = asset::Atmosphere(no_ozone).sky_radiance({0, 1, 0}, sun);
    // Its absorption is strongest in green-orange (the Chappuis band): green-to-blue drops by about 30%.
    CHECK(zenith.y / zenith.z < 0.8f * (bare.y / bare.z));
    CHECK(zenith.z == doctest::Approx(bare.z).epsilon(0.05));  // blue hardly touched
}

TEST_CASE("atmosphere: haze and altitude do what they say") {
    asset::AtmosphereSettings hazy;
    hazy.turbidity = 5.0f;
    const Vec3 sun = sun_at(30.0f);
    const Vec3 clear_t = earth().sun_transmittance(sun);
    const Vec3 hazy_t = asset::Atmosphere(hazy).sun_transmittance(sun);
    CHECK(hazy_t.y < clear_t.y);  // the sun dims in haze

    asset::AtmosphereSettings high;
    high.altitude_m = 30000.0f;  // a stratospheric balloon: darker sky overhead
    const asset::Atmosphere up(high);
    CHECK(luminance(up.sky_radiance({0, 1, 0}, sun)) < 0.3f * luminance(earth().sky_radiance({0, 1, 0}, sun)));
}

TEST_CASE("atmosphere: a blend's settings are answered from two cached grid atmospheres, smoothly") {
    // On a grid point (turbidity 1, 2.5, 4...: every preset) the bracket is exactly that atmosphere.
    const auto on = asset::Atmosphere::bracket({});
    CHECK(on.lo.get() == &earth());
    CHECK(on.t == 0.0f);
    const Vec3 sun = sun_at(25.0f);
    CHECK(on.sun_transmittance(sun).y == earth().sun_transmittance(sun).y);
    CHECK(on.sky_illuminance(0.4f) == earth().sky_illuminance(0.4f));
    // Between grid points: between the neighbours, and close to what its own tables would say.
    asset::AtmosphereSettings mid;
    mid.turbidity = 1.25f;
    const auto b = asset::Atmosphere::bracket(mid);
    CHECK(b.lo->settings().turbidity == 1.0f);
    CHECK(b.hi->settings().turbidity == 1.5f);
    CHECK(b.t == doctest::Approx(0.5f));
    const asset::Atmosphere exact(mid);
    CHECK(b.sun_transmittance(sun).y == doctest::Approx(exact.sun_transmittance(sun).y).epsilon(0.01));
    CHECK(b.sky_illuminance(0.4f) == doctest::Approx(exact.sky_illuminance(0.4f)).epsilon(0.01));
    // A haze rolling in (turbidity 1 to 4 a little at a time) never jumps, crossing grid points included.
    asset::Atmosphere::prewarm({asset::AtmosphereSettings{}, [] {
                                    asset::AtmosphereSettings s;
                                    s.turbidity = 4.0f;
                                    return s;
                                }()});
    float last_lux = -1.0f, last_t = -1.0f;
    for (float turbidity = 1.0f; turbidity <= 4.0f; turbidity += 0.01f) {
        asset::AtmosphereSettings s;
        s.turbidity = turbidity;
        const auto k = asset::Atmosphere::bracket(s);
        const float lux = k.sky_illuminance(0.4f), t = k.sun_transmittance(sun).y;
        if (last_lux >= 0.0f) {
            CHECK(std::abs(lux - last_lux) < 0.01f * last_lux);
            CHECK(t <= last_t + 1e-6f);  // more haze, less sun
        }
        last_lux = lux, last_t = t;
    }
    // The observer's height is used as it is for the sun's light (only the sky's table snaps it to the grid).
    asset::AtmosphereSettings up;
    up.altitude_m = 1234.0f;
    CHECK(asset::Atmosphere::bracket(up).sun_transmittance(sun).y == asset::Atmosphere(up).sun_transmittance(sun).y);
}

TEST_CASE("atmosphere: the sky image follows the equirect layout and is deterministic") {
    const auto& a = earth();
    const Vec3 sun = sun_at(20.0f);
    const asset::EquirectImage img = a.sky_image(sun, 64, 32);
    REQUIRE(img.width == 64);
    REQUIRE(img.rgb.size() == 64u * 32u * 3u);
    // The top row is the zenith: compare with the direct call.
    const Vec3 d = asset::equirect_direction(0.5f / 64.0f, 0.5f / 32.0f);
    const Vec3 direct = a.sky_radiance(d, sun);
    CHECK(img.rgb[0] == doctest::Approx(direct.x));
    CHECK(img.rgb[2] == doctest::Approx(direct.z));
    CHECK(a.sky_image(sun, 64, 32).rgb == img.rgb);
    CHECK(asset::Atmosphere::shared({}).get() == &a);  // built once
}


namespace {

// A scene with a sun `elevation` degrees up and an environment component.
game::SceneView view_with(float elevation, asset::Json environment, asset::Json light = {{"type", "directional"}, {"illuminance", 100000}}) {
    scene::Scene s;
    scene::Entity sun;
    sun.id = Guid::generate();
    sun.name = "Sun";
    // Forward (-Z) tipped down by `elevation`: the way sunlight travels.
    sun.transform.rotation = math::Quat::from_axis_angle({1, 0, 0}, math::radians(-elevation));
    sun.components["light"] = std::move(light);
    s.entities.push_back(sun);
    scene::Entity sky;
    sky.id = Guid::generate();
    sky.name = "Sky";
    sky.components["environment"] = std::move(environment);
    s.entities.push_back(sky);
    return game::collect_view(s, {});
}

} // namespace

TEST_CASE("atmosphere: a scene's environment {sky: atmosphere} colours the sun and lights the sky from it") {
    const auto noon = view_with(60.0f, {{"sky", "atmosphere"}});
    REQUIRE(noon.environment.atmosphere);
    CHECK(noon.lighting.sun_disc);
    CHECK(noon.lighting.environment_intensity == doctest::Approx(100000.0f));  // the sky's brightness follows the sun's
    CHECK(noon.environment.to_sun.y == doctest::Approx(std::sin(math::radians(60.0f))).epsilon(0.001));
    // White outside the air, nearly white at noon; red at sunset.
    CHECK(noon.lighting.sun_color.z / noon.lighting.sun_color.x > 0.75f);
    const auto sunset = view_with(2.0f, {{"sky", "atmosphere"}});
    CHECK(sunset.lighting.sun_color.x > 3.0f * sunset.lighting.sun_color.z);
    // The scene's knobs reach the settings.
    const auto hazy = view_with(30.0f, {{"sky", "atmosphere"}, {"turbidity", 4}, {"altitude", 1500}, {"ground_albedo", 0.1}});
    CHECK(hazy.environment.atmosphere_settings.turbidity == 4.0f);
    CHECK(hazy.environment.atmosphere_settings.altitude_m == 1500.0f);
    CHECK(hazy.environment.atmosphere_settings.ground_albedo.x == doctest::Approx(0.1f));
    // A given colour is the sun's own (outside the air), still tinted by it.
    const auto tinted = view_with(60.0f, {{"sky", "atmosphere"}}, {{"type", "directional"}, {"illuminance", 100000}, {"color", "#FF0000"}});
    CHECK(tinted.lighting.sun_color.y == doctest::Approx(0.0f));
    // Other skies don't draw a disc or touch the sun's colour.
    const auto gradient = view_with(60.0f, {{"sky", "gradient"}});
    CHECK_FALSE(gradient.environment.atmosphere);
    CHECK_FALSE(gradient.lighting.sun_disc);
}

TEST_CASE("atmosphere: the sun's position by the hour, against textbook astronomy") {
    const auto elevation = [](Vec3 d) { return std::asin(d.y) * 180.0f / 3.14159265f; };
    // March equinox (declination about 0) on the equator: overhead at noon, rising due east at 6:00.
    const Vec3 noon_eq = asset::solar_direction(12.0f, 0.0f, 80.0f);
    CHECK(elevation(noon_eq) == doctest::Approx(90.0f).epsilon(0.02));
    const Vec3 dawn_eq = asset::solar_direction(6.0f, 0.0f, 80.0f);
    CHECK(dawn_eq.x == doctest::Approx(1.0f).epsilon(0.01));  // east is +X
    CHECK(std::abs(dawn_eq.y) < 0.02f);
    // Midsummer at 50 degrees north: noon elevation 90 - 50 + 23.44 = 63.44, due south (+Z).
    const Vec3 summer = asset::solar_direction(12.0f, 50.0f, 172.0f);
    CHECK(elevation(summer) == doctest::Approx(63.44f).epsilon(0.005));
    CHECK(summer.z > 0.0f);
    CHECK(std::abs(summer.x) < 1e-3f);
    // Midwinter there: 90 - 50 - 23.44 = 16.56; and night at midnight.
    CHECK(elevation(asset::solar_direction(12.0f, 50.0f, 355.0f)) == doctest::Approx(16.56f).epsilon(0.01));
    CHECK(asset::solar_direction(0.0f, 50.0f, 172.0f).y < 0.0f);
    // Afternoon sun is in the west (-X); the southern hemisphere sees the noon sun in the north (-Z).
    CHECK(asset::solar_direction(16.0f, 50.0f, 172.0f).x < 0.0f);
    CHECK(asset::solar_direction(12.0f, -35.0f, 172.0f).z < 0.0f);
}

TEST_CASE("atmosphere: time_of_day on the environment places the sun") {
    // The sun light points straight down; the time of day overrides it.
    const auto view = view_with(90.0f, {{"sky", "atmosphere"}, {"time_of_day", 7.5}, {"latitude", 50}, {"day_of_year", 172}});
    const Vec3 expected = asset::solar_direction(7.5f, 50.0f, 172.0f);
    CHECK(view.environment.to_sun.x == doctest::Approx(expected.x).epsilon(1e-4));
    CHECK(view.environment.to_sun.y == doctest::Approx(expected.y).epsilon(1e-4));
    CHECK(view.lighting.sun_direction.y == doctest::Approx(-expected.y).epsilon(1e-4));
    // "north" turns the compass by 90 degrees about +Y: the equinox sunrise (east, +X) comes from -Z
    // instead, so its light travels along +Z.
    const auto turned = view_with(90.0f, {{"time_of_day", 6.0}, {"latitude", 0}, {"day_of_year", 80}, {"north", 90}});
    CHECK(std::abs(turned.lighting.sun_direction.x) < 0.05f);
    CHECK(turned.lighting.sun_direction.z > 0.95f);
    // Hours wrap: 25 is 1 in the morning.
    const auto wrapped = view_with(90.0f, {{"time_of_day", 25.0}, {"latitude", 50}});
    CHECK(wrapped.lighting.sun_direction.y > 0.0f);  // the sun is below the horizon: light points up
}

TEST_CASE("atmosphere: aerial perspective grows with distance and meets the sky") {
    const auto& a = earth();
    const Vec3 sun = sun_at(40.0f);
    const Vec3 view = math::normalize(Vec3{1, 0.02f, 0});
    const auto none = a.aerial_perspective(view, 0.0f, sun, 0.0f);
    CHECK(none.transmittance.x == 1.0f);
    CHECK(none.inscatter.z == 0.0f);
    // A hill 1 km away: a few per cent of blue haze; 20 km: much more, and bluer light gets through less.
    const auto near = a.aerial_perspective(view, 1.0f, sun, 0.0f);
    const auto far = a.aerial_perspective(view, 20.0f, sun, 0.0f);
    CHECK(near.transmittance.z > 0.95f);
    CHECK(near.transmittance.z < 0.99f);
    CHECK(far.inscatter.z > 5.0f * near.inscatter.z);
    CHECK(far.transmittance.z < far.transmittance.x);
    // Looking up through the whole atmosphere, the aerial perspective is the sky itself.
    const Vec3 up = math::normalize(Vec3{0.3f, 1, 0});
    const auto whole = a.aerial_perspective(up, 400.0f, sun, 0.0f);
    const Vec3 sky = a.sky_radiance(up, sun);
    CHECK(whole.inscatter.x == doctest::Approx(sky.x).epsilon(0.03));
    CHECK(whole.inscatter.z == doctest::Approx(sky.z).epsilon(0.03));
}

TEST_CASE("atmosphere: fog and aerial fields on the environment reach the view") {
    const auto view = view_with(30.0f, {{"sky", "hdri"},
                                        {"fog_density", 0.004},
                                        {"fog_height", 3},
                                        {"fog_falloff", 0.1},
                                        {"fog_anisotropy", 0.5},
                                        {"fog_color", "#FF0000"},
                                        {"aerial_scale", 30}});
    CHECK(view.lighting.fog.density == doctest::Approx(0.004f));
    CHECK(view.lighting.fog.height == 3.0f);
    CHECK(view.lighting.fog.falloff == doctest::Approx(0.1f));
    CHECK(view.lighting.fog.anisotropy == doctest::Approx(0.5f));
    CHECK(view.lighting.fog.albedo.y == doctest::Approx(0.0f));
    CHECK(view.environment.aerial_scale == 30.0f);
    // No fog fields: no fog (an old scene looks as it did).
    CHECK(view_with(30.0f, {{"sky", "atmosphere"}}).lighting.fog.density == 0.0f);
}
