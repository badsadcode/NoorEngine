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

// ---- Volumetric layers (part 7b, ADR-034) ----

namespace {

render::CloudLayer volume(float coverage, float density = 8.0f, std::uint32_t seed = 7) {
    render::CloudLayer l = layer(coverage, seed);
    l.volume = true;
    l.density = density;
    l.altitude_km = 1.5f;
    l.thickness_km = 1.5f;
    l.scale_km = 2.5f;
    return l;
}

constexpr float kPlanet = 6360.0f;
const Vec3 kSun{0.0f, 1.0f, 0.0f};    // overhead
const Vec3 kLight{1.0f, 1.0f, 1.0f};  // sunlight at the layer (relative)
const Vec3 kSky{0.1f, 0.12f, 0.15f};

float luminance(Vec3 v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; }

} // namespace

TEST_CASE("clouds: a volume has a shape: flat bases, rounded tops, taller where it's cloudier") {
    CHECK(render::cloud_height_profile(0.0f, 1.0f) == 0.0f);
    CHECK(render::cloud_height_profile(0.1f, 1.0f) == doctest::Approx(1.0f));   // full just above the base
    CHECK(render::cloud_height_profile(0.5f, 1.0f) == doctest::Approx(1.0f));
    CHECK(render::cloud_height_profile(0.9f, 1.0f) < render::cloud_height_profile(0.7f, 1.0f));  // rounding off
    CHECK(render::cloud_height_profile(0.5f, 0.2f) == 0.0f);                     // a wisp stays low
    CHECK(render::cloud_height_profile(0.2f, 0.2f) > 0.0f);
    // The column mean the extinction is scaled by.
    double sum = 0.0;
    constexpr int kN = 10000;
    for (int i = 0; i < kN; ++i) sum += render::cloud_height_profile((i + 0.5f) / kN, 1.0f);
    CHECK(sum / kN == doctest::Approx(render::kCloudColumnMean).epsilon(0.02));
    // Outside the slab, nothing; a flat layer has no volume at all.
    const auto v = volume(0.6f);
    CHECK(render::cloud_volume_density(v, 0.3f, 1.4f, 0.2f, {}) == 0.0f);
    CHECK(render::cloud_volume_density(v, 0.3f, 3.1f, 0.2f, {}) == 0.0f);
    render::CloudLayer flat = v;
    flat.volume = false;
    const auto none = render::cloud_volume_march(flat, {0, 0, 0}, kPlanet, {0, 1, 0}, kSun, kLight, kSky, {});
    CHECK(none.transmittance == 1.0f);
}

TEST_CASE("clouds: a volume's column keeps the layer's density as its optical thickness, and the march converges") {
    // Overcast (coverage 1): looking straight up from the ground crosses a full cloud.
    const auto v = volume(1.0f, 8.0f);
    const auto fine = render::cloud_volume_march(v, {0.4f, 0.0f, 0.7f}, kPlanet, {0, 1, 0}, kSun, kLight, kSky, {}, 512, 6);
    const auto coarse = render::cloud_volume_march(v, {0.4f, 0.0f, 0.7f}, kPlanet, {0, 1, 0}, kSun, kLight, kSky, {}, 48, 6);
    const float tau = -std::log(std::max(1e-9f, fine.transmittance));
    CHECK(tau > 0.5f * 8.0f);   // erosion takes some away
    CHECK(tau < 1.05f * 8.0f);  // never more than the layer's density
    CHECK(-std::log(std::max(1e-9f, coarse.transmittance)) == doctest::Approx(tau).epsilon(0.05));
    CHECK(luminance(coarse.radiance) == doctest::Approx(luminance(fine.radiance)).epsilon(0.05));
}

TEST_CASE("clouds: a volume has sides, where a flat layer has none") {
    // From a mountain at cloud height, looking level: through the slab, into the clouds.
    const auto v = volume(0.7f);
    const Vec3 eye{0.0f, v.altitude_km + 0.3f * v.thickness_km, 0.0f};
    float hits = 0.0f;
    for (int i = 0; i < 16; ++i) {
        const float a = static_cast<float>(i) * 0.3927f;
        hits += 1.0f - render::cloud_volume_march(v, eye, kPlanet, {std::sin(a), 0.0f, -std::cos(a)}, kSun, kLight, kSky, {}).transmittance;
    }
    CHECK(hits / 16.0f > 0.5f);
    // Near the horizon, seen from the ground, clouds stack up behind each other: on average more opaque than a flat
    // layer's single crossing of the same coverage (a patchy sky, where a flat layer still shows gaps).
    const auto patchy = volume(0.35f);
    float vol = 0.0f, flat = 0.0f;
    const Vec3 ground{0, 0, 0};
    const float up = std::sin(math::radians(5.0f)), level = std::cos(math::radians(5.0f));
    for (int i = 0; i < 64; ++i) {
        const float a = static_cast<float>(i) * 0.0982f;
        const Vec3 view{std::sin(a) * level, up, -std::cos(a) * level};
        vol += 1.0f - render::cloud_volume_march(patchy, ground, kPlanet, view, kSun, kLight, kSky, {}).transmittance;
        const float r = kPlanet, shell = kPlanet + patchy.altitude_km;
        const float t = -r * view.y + std::sqrt(r * r * (view.y * view.y - 1.0f) + shell * shell);
        const float cover = render::cloud_cover_at(patchy, view.x * t, view.z * t, {});
        flat += (1.0f - std::exp(-cover * patchy.density)) * std::exp(-t / 250.0f);
    }
    MESSAGE("near the horizon: volume ", vol / 64.0f, ", flat ", flat / 64.0f);
    CHECK(vol > 1.2f * flat);
}

TEST_CASE("clouds: sunlit tops are bright, bases and thick clouds dark") {
    // Inside an overcast volume, looking level: cloud near the top sees more sun than cloud near the base. Judged per
    // bit of cloud (radiance over opacity): with puffs, a look near the top crosses gaps between domes, so its total
    // light also depends on how much cloud it meets, which isn't the question.
    const auto v = volume(1.0f, 12.0f);
    const auto at = [&](float h) {
        float lit = 0.0f, seen = 0.0f;
        for (int i = 0; i < 16; ++i) {  // several directions: one look may miss the cloud entirely
            const float a = static_cast<float>(i) * 0.3927f;
            const Vec3 eye{0.0f, v.altitude_km + h * v.thickness_km, 0.0f};
            const auto c = render::cloud_volume_march(v, eye, kPlanet, {std::sin(a), 0.0f, -std::cos(a)}, kSun, kLight, kSky, {});
            lit += luminance(c.radiance);
            seen += 1.0f - c.transmittance;
        }
        REQUIRE(seen > 0.5f);
        return lit / seen;
    };
    CHECK(at(0.8f) > 1.5f * at(0.15f));
    // From the ground under the sun, a thick cloud is darker than a thin one.
    const auto from_below = [&](float density) {
        return luminance(render::cloud_volume_march(volume(1.0f, density), {0, 0, 0}, kPlanet, {0, 1, 0}, kSun, kLight, kSky, {}).radiance);
    };
    CHECK(from_below(40.0f) < from_below(4.0f));
    // The same inputs give the same cloud (no hidden state: goldens and built games depend on it).
    const auto a = render::cloud_volume_march(volume(0.5f), {0.1f, 0.0f, 0.2f}, kPlanet, math::normalize(Vec3{0.3f, 0.5f, -0.8f}), kSun, kLight, kSky, {0.2f, 0.1f}, 48, 6, 0.37f);
    const auto b = render::cloud_volume_march(volume(0.5f), {0.1f, 0.0f, 0.2f}, kPlanet, math::normalize(Vec3{0.3f, 0.5f, -0.8f}), kSun, kLight, kSky, {0.2f, 0.1f}, 48, 6, 0.37f);
    CHECK(a.transmittance == b.transmittance);
    CHECK(a.radiance.x == b.radiance.x);
}

TEST_CASE("clouds: the volume noises tile, so the GPU can bake them once") {
    const float p = static_cast<float>(render::kCloudNoisePeriod);
    for (int i = 0; i < 20; ++i) {
        const float x = 0.37f * static_cast<float>(i), y = 0.21f * static_cast<float>(i) + 0.1f, z = 1.7f - 0.13f * static_cast<float>(i);
        CHECK(render::cloud_billow(x + p, y, z) == doctest::Approx(render::cloud_billow(x, y, z)).epsilon(1e-4));
        CHECK(render::cloud_billow(x, y - p, z + p) == doctest::Approx(render::cloud_billow(x, y, z)).epsilon(1e-4));
        CHECK(render::cloud_detail(x - p, y, z) == doctest::Approx(render::cloud_detail(x, y, z)).epsilon(1e-4));
        CHECK(render::cloud_detail(x, y + p, z - p) == doctest::Approx(render::cloud_detail(x, y, z)).epsilon(1e-4));
    }
    // Different seeds read different places.
    CHECK(render::cloud_noise_offset(1).x != render::cloud_noise_offset(2).x);
}
