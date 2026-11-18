// Layered clouds (Milestone 8A, part 4): the maths in noor/render/clouds.h, the reference for the shaders.
#include "noor/render/clouds.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>

using namespace noor;
using math::Vec3;

namespace {

render::CloudLayer layer(float coverage, std::uint32_t seed = 7) {
    render::CloudLayer l;
    l.enabled = true;
    l.coverage = coverage;
    l.seed = seed;
    return l;
}

// The share of the sky (over a 40 x 40 km patch) that is mostly cloud.
float cloud_fraction(const render::CloudLayer& l) {
    int cloudy = 0, n = 0;
    for (int i = 0; i < 120; ++i)
        for (int j = 0; j < 120; ++j, ++n)
            if (render::cloud_cover_at(l, i * 0.33f, j * 0.33f, {0, 0}) > 0.5f) ++cloudy;
    return static_cast<float>(cloudy) / n;
}

} // namespace

TEST_CASE("clouds: the same seed, wind and time give the same sky, another seed doesn't") {
    const auto a = layer(0.5f, 7), b = layer(0.5f, 7), c = layer(0.5f, 8);
    int differ = 0;
    for (int i = 0; i < 200; ++i) {
        const float x = i * 0.37f, z = i * 0.91f;
        CHECK(render::cloud_cover_at(a, x, z, {0.3f, 1.2f}) == render::cloud_cover_at(b, x, z, {0.3f, 1.2f}));
        if (std::abs(render::cloud_cover_at(a, x, z, {0, 0}) - render::cloud_cover_at(c, x, z, {0, 0})) > 0.1f) ++differ;
    }
    CHECK(differ > 40);
}

TEST_CASE("clouds: the noise stays in 0..1 and averages about a half") {
    double sum = 0;
    float lo = 1, hi = 0;
    for (int i = 0; i < 300; ++i)
        for (int j = 0; j < 300; ++j) {
            const float n = render::cloud_fbm(i * 0.173f, j * 0.219f, 3);
            sum += n;
            lo = std::min(lo, n);
            hi = std::max(hi, n);
        }
    CHECK(lo >= 0.0f);
    CHECK(hi <= 1.0f);
    CHECK(sum / (300.0 * 300.0) == doctest::Approx(0.5).epsilon(0.08));
}

TEST_CASE("clouds: coverage goes from a clear sky to overcast, steadily") {
    CHECK(cloud_fraction(layer(0.0f)) == 0.0f);
    CHECK(cloud_fraction(layer(0.1f)) < 0.05f);
    const float half = cloud_fraction(layer(0.5f));
    CHECK(half > 0.25f);
    CHECK(half < 0.75f);
    CHECK(cloud_fraction(layer(0.9f)) > 0.9f);
    CHECK(cloud_fraction(layer(1.0f)) == 1.0f);
    CHECK(cloud_fraction(layer(0.3f)) < cloud_fraction(layer(0.6f)));
    render::CloudLayer off = layer(1.0f);
    off.enabled = false;
    CHECK(render::cloud_cover_at(off, 1, 1, {0, 0}) == 0.0f);
}

TEST_CASE("clouds: the wind slides the pattern by wind x time") {
    auto l = layer(0.5f);
    l.wind_speed = 20.0f;      // m/s
    l.wind_direction = 90.0f;  // towards +X
    const auto o = render::cloud_offset(l, 100.0f);  // 2 km in 100 s
    CHECK(o.x == doctest::Approx(2.0f));
    CHECK(o.y == doctest::Approx(0.0f).scale(1.0f));
    // What was at x after 100 s is 2 km further along +X.
    for (int i = 0; i < 50; ++i)
        CHECK(render::cloud_cover_at(l, i * 0.5f + 2.0f, 3.0f, o) == doctest::Approx(render::cloud_cover_at(l, i * 0.5f, 3.0f, {0, 0})).epsilon(1e-4));
}

TEST_CASE("clouds: overcast dims the sun on the ground, a clear sky doesn't") {
    const Vec3 to_sun = math::normalize(Vec3{0.3f, 0.8f, -0.2f});
    std::array<render::CloudLayer, render::kCloudLayers> layers{};
    // Clear: all the sun.
    CHECK(render::cloud_sun_transmittance(layers, {0, 0, 0}, to_sun, 0.0f) == 1.0f);
    // Overcast, a heavy layer (density 6): e^-6, a quarter of a per cent: shadows all but vanish.
    layers[0] = layer(1.0f);
    CHECK(render::cloud_sun_transmittance(layers, {10, 0, 10}, to_sun, 0.0f) == doctest::Approx(std::exp(-6.0f)).epsilon(1e-3));
    // Broken cloud: on average in between, and some places in full sun.
    layers[0] = layer(0.4f);
    double sum = 0;
    int sunny = 0;
    for (int i = 0; i < 400; ++i) {
        const float t = render::cloud_sun_transmittance(layers, {i * 25.0f, 0, i * 13.0f}, to_sun, 0.0f);
        sum += t;
        if (t > 0.9f) ++sunny;
    }
    CHECK(sum / 400 > 0.2);
    CHECK(sum / 400 < 0.95);
    CHECK(sunny > 50);
    // Above a layer, it doesn't shade you; with the sun set, nothing to dim.
    layers[0] = layer(1.0f);
    CHECK(render::cloud_sun_transmittance(layers, {0, 3000, 0}, to_sun, 0.0f) == 1.0f);
    CHECK(render::cloud_sun_transmittance(layers, {0, 0, 0}, {1, -0.1f, 0}, 0.0f) == 1.0f);
}

TEST_CASE("clouds: far away the fine detail fades to its average, never past it") {
    // Full detail at footprint 0 (what the other tests use); a footprint bigger than every octave leaves the mean.
    CHECK(render::cloud_fbm(3.3f, 7.1f, 5, 0.0f) == render::cloud_fbm(3.3f, 7.1f, 5));
    CHECK(render::cloud_fbm(3.3f, 7.1f, 5, 10.0f) == doctest::Approx(0.5f));
    // In between, the values pull towards the mean: less contrast, the same average.
    double near_spread = 0, far_spread = 0, far_mean = 0;
    for (int i = 0; i < 400; ++i) {
        const float x = i * 0.71f, y = i * 0.37f;
        near_spread += std::abs(render::cloud_fbm(x, y, 5, 0.0f) - 0.5f);
        const float far = render::cloud_fbm(x, y, 5, 0.3f);
        far_spread += std::abs(far - 0.5f);
        far_mean += far;
    }
    CHECK(far_spread < near_spread);
    CHECK(far_mean / 400 == doctest::Approx(0.5).epsilon(0.1));
}

TEST_CASE("clouds: thick clouds let less light through, diffused (two-stream, g = 0.85)") {
    CHECK(render::cloud_diffuse_transmittance(0.0f) == 1.0f);
    CHECK(render::cloud_diffuse_transmittance(8.0f) == doctest::Approx(1.0f / 1.9f));    // an overcast deck: about half
    CHECK(render::cloud_diffuse_transmittance(40.0f) == doctest::Approx(1.0f / 5.5f));   // a storm cloud: under a fifth
    CHECK(render::cloud_diffuse_transmittance(40.0f) < render::cloud_diffuse_transmittance(8.0f));
}
