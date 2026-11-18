// Night and space (Milestone 8A, part 5): the moon, the star field and automatic exposure, against astronomy
// and photography worked out by hand.
#include "noor/asset/atmosphere.h"
#include "noor/render/exposure.h"
#include "noor/render/stars.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>

using namespace noor;
using math::Vec3;

namespace {
float degrees_between(Vec3 a, Vec3 b) { return std::acos(std::clamp(math::dot(a, b), -1.0f, 1.0f)) * 180.0f / 3.14159265f; }
} // namespace

TEST_CASE("night: a full moon rises as the sun sets, a new moon stays beside the sun") {
    // Full: about opposite the sun (the model leaves out the moon's 5-degree tilt, so within a few degrees).
    for (const float hours : {6.0f, 12.0f, 18.0f, 23.0f}) {
        const Vec3 sun = asset::solar_direction(hours, 50.0f, 172.0f);
        const Vec3 full = asset::lunar_direction(hours, 50.0f, 172.0f, 0.5f);
        CAPTURE(hours);
        CHECK(degrees_between(sun, full) > 165.0f);
        const Vec3 fresh = asset::lunar_direction(hours, 50.0f, 172.0f, 0.0f);
        CHECK(degrees_between(sun, fresh) < 1.0f);
    }
    // A first-quarter moon is about 90 degrees from the sun (east of it in the evening sky).
    const Vec3 sun = asset::solar_direction(16.0f, 50.0f, 100.0f);
    CHECK(degrees_between(sun, asset::lunar_direction(16.0f, 50.0f, 100.0f, 0.25f)) == doctest::Approx(90.0f).epsilon(0.12));
    // A winter full moon stands higher at midnight than a summer one (it takes the summer sun's path).
    const float winter = asset::lunar_direction(0.0f, 50.0f, 355.0f, 0.5f).y, summer = asset::lunar_direction(0.0f, 50.0f, 172.0f, 0.5f).y;
    CHECK(winter > summer + 0.4f);
    // The stylised calendar: one cycle in 29.53 days.
    CHECK(asset::default_moon_phase(11.0f) == doctest::Approx(0.0f).scale(1.0f));
    CHECK(asset::default_moon_phase(11.0f + 29.530588f / 2.0f) == doctest::Approx(0.5f));
}

TEST_CASE("night: moonlight is what a light meter finds under a real moon") {
    // A full moon under a 100 klx sun: about 0.16 lux (measured full moons: 0.1-0.3).
    const float full = asset::moon_illuminance(100000.0f, 0.0f);
    CHECK(full > 0.1f);
    CHECK(full < 0.3f);
    // A half moon gives about a third of a full one (the Lambert phase function at 90 degrees: 1 / pi); new: nothing.
    CHECK(asset::moon_illuminance(100000.0f, 3.14159265f / 2.0f) == doctest::Approx(full / 3.14159265f).epsilon(0.01));
    CHECK(asset::moon_illuminance(100000.0f, 3.14159265f) == doctest::Approx(0.0f).scale(1.0f));
}

TEST_CASE("night: the star field has the real sky's numbers and never changes") {
    int total = 0, brighter4 = 0, brighter1 = 0;
    float brightest = 99.0f;
    for (std::uint32_t face = 0; face < 6; ++face)
        for (std::uint32_t y = 0; y < render::kStarGrid; ++y)
            for (std::uint32_t x = 0; x < render::kStarGrid; ++x)
                if (const auto s = render::star_in_cell(face, x, y, 1)) {
                    ++total;
                    if (s->magnitude < 4.0f) ++brighter4;
                    if (s->magnitude < 1.0f) ++brighter1;
                    brightest = std::min(brightest, s->magnitude);
                    CHECK(std::abs(math::length(s->direction) - 1.0f) < 1e-4f);
                }
    // About 8,400 stars to magnitude 6.5, 470 brighter than 4, 15 brighter than 1 (counting noise allowed).
    CHECK(total == doctest::Approx(render::star_count_brighter(6.5f)).epsilon(0.05));
    CHECK(brighter4 == doctest::Approx(render::star_count_brighter(4.0f)).epsilon(0.25));
    CHECK(brighter1 >= 5);
    CHECK(brighter1 <= 30);
    CHECK(brightest >= render::kBrightestStar);
    // Deterministic, and another seed is another sky.
    const auto a = render::star_in_cell(2, 100, 100, 1), b = render::star_in_cell(2, 100, 100, 1);
    CHECK(a.has_value() == b.has_value());
    int same = 0;
    for (std::uint32_t x = 0; x < 2000; ++x)
        if (render::star_in_cell(4, x % 256, x / 256, 1).has_value() && render::star_in_cell(4, x % 256, x / 256, 2).has_value()) ++same;
    CHECK(same < 10);
    // A magnitude-0 star gives 2.08e-6 lux; five magnitudes is a factor 100.
    CHECK(render::star_illuminance(0.0f) == doctest::Approx(2.08e-6f));
    CHECK(render::star_illuminance(5.0f) == doctest::Approx(2.08e-8f));
}

TEST_CASE("night: the sky turns about the celestial pole") {
    const float lat = 50.0f;
    const Vec3 pole = math::normalize(Vec3{0, std::sin(math::radians(lat)), -std::cos(math::radians(lat))});
    for (const float h : {0.0f, 3.0f, 7.5f, 18.0f}) {
        const Vec3 p = render::star_frame(pole, lat, h);
        CHECK(p.y == doctest::Approx(1.0f).epsilon(1e-4));  // the pole stays put
    }
    // Any other star moves: 6 hours turn it 90 degrees about the pole.
    const Vec3 east{1, 0, 0};
    CHECK(degrees_between(render::star_frame(east, lat, 0.0f), render::star_frame(east, lat, 6.0f)) == doctest::Approx(90.0f).epsilon(0.001));
}

TEST_CASE("night: automatic exposure meters like a photographer, and keeps night dark") {
    CHECK(render::auto_ev100(100000.0f) == doctest::Approx(15.29f).epsilon(0.01));  // sunny 16
    CHECK(render::night_exposure_bias(100000.0f) == 0.0f);
    CHECK(render::night_exposure_bias(400.0f) == 0.0f);
    // Moonlight (0.16 lux): metered EV -4, held back 2.5 stops to about -1.5: less exposure than the meter wants,
    // so the picture is darker than a dull afternoon (a higher EV100 is less exposure).
    CHECK(render::night_exposure_bias(0.16f) == doctest::Approx(2.5f));
    CHECK(render::auto_ev100(0.16f) == doctest::Approx(std::log2(0.16f / 2.5f) + 2.5f).epsilon(0.02));
    CHECK(render::auto_ev100(0.16f) > std::log2(0.16f / 2.5f));  // darker than metered, never brighter
    CHECK(render::night_exposure_bias(0.001f) == 2.5f);
    // Brighter scenes never get more exposure, and a dark scene's picture never ends up brighter than a lit one's:
    // EV100 rises with the light, and the light reaching the picture (lux / 2^EV) does too.
    float last_ev = -100.0f, last_picture = 0.0f;
    for (float lux = 1e-4f; lux < 2e5f; lux *= 1.5f) {
        const float ev = render::auto_ev100(lux);
        const float picture = lux / std::exp2(ev);
        CHECK(ev >= last_ev);
        CHECK(picture >= last_picture * 0.999f);
        last_ev = ev;
        last_picture = picture;
    }
}

TEST_CASE("night: skylight on the ground by the sun's height") {
    static const auto a = asset::Atmosphere::shared({});
    // Noon: the blue sky adds about a fifth to a seventh of the direct sun's light on the ground (clear sky).
    const float noon = a->sky_illuminance(math::radians(60.0f));
    CHECK(noon > 0.05f);
    CHECK(noon < 0.3f);
    // Twilight: the sun is down but the sky still lights the ground; deep night: nothing from the sun.
    const float civil = a->sky_illuminance(math::radians(-3.0f));
    CHECK(civil > 0.0f);
    CHECK(civil < a->sky_illuminance(math::radians(5.0f)));
    CHECK(a->sky_illuminance(math::radians(-20.0f)) < civil * 0.01f);
}
