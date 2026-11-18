// Height fog maths (Milestone 8A, part 3): the closed forms in noor/render/fog.h against numerical
// integration and the properties the docs promise.
#include "noor/render/fog.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>

using namespace noor;
using math::Vec3;

namespace {

// The optical depth by brute force: a fine midpoint sum of the density along the ray.
float numeric_depth(const render::FogParams& f, Vec3 o, Vec3 d, float distance) {
    constexpr int kSteps = 200000;
    const double dt = distance / kSteps;
    double sum = 0;
    for (int i = 0; i < kSteps; ++i) {
        const double t = (i + 0.5) * dt;
        sum += render::fog_density_at(f, static_cast<float>(o.y + d.y * t)) * dt;
    }
    return static_cast<float>(sum);
}

render::FogParams thick() {
    render::FogParams f;
    f.density = 0.01f;
    f.falloff = 0.08f;
    f.height = 5.0f;
    return f;
}

} // namespace

TEST_CASE("fog: the closed-form optical depth equals numerical integration") {
    const auto f = thick();
    for (const Vec3 o : {Vec3{0, 12, 0}, Vec3{0, 2, 0}}) for (const Vec3 d : {Vec3{1, 0, 0}, math::normalize(Vec3{1, 0.2f, 0}), math::normalize(Vec3{1, -0.1f, 0.3f}), Vec3{0, 1, 0},
                         math::normalize(Vec3{0, -1, 1})}) {
        for (const float distance : {1.0f, 50.0f, 400.0f}) {
            CAPTURE(o.y);
            CAPTURE(d.y);
            CAPTURE(distance);
            const float exact = render::fog_optical_depth(f, o, d, distance);
            CHECK(exact == doctest::Approx(numeric_depth(f, o, d, distance)).epsilon(0.002));
        }
    }
}

TEST_CASE("fog: density, height and falloff do what the docs say") {
    const Vec3 level{1, 0, 0};
    auto f = thick();
    // A level ray at the fog's height: density × distance.
    CHECK(render::fog_optical_depth(f, {0, 5, 0}, level, 100.0f) == doctest::Approx(1.0f));
    // Twice the density, twice the depth.
    const float base = render::fog_optical_depth(f, {0, 12, 0}, math::normalize(Vec3{1, 0.1f, 0}), 300.0f);
    auto twice = f;
    twice.density *= 2;
    CHECK(render::fog_optical_depth(twice, {0, 12, 0}, math::normalize(Vec3{1, 0.1f, 0}), 300.0f) == doctest::Approx(base * 2));
    // Below the fog's height it's at full density, whatever the depth.
    CHECK(render::fog_density_at(f, -100.0f) == f.density);
    // ln 2 / falloff metres higher, half the density.
    CHECK(render::fog_density_at(f, 5.0f + std::log(2.0f) / f.falloff) == doctest::Approx(f.density * 0.5f));
    // Raising the fog's height thickens it for the same camera.
    auto higher = f;
    higher.height = 15.0f;
    CHECK(render::fog_optical_depth(higher, {0, 12, 0}, level, 100.0f) > render::fog_optical_depth(f, {0, 12, 0}, level, 100.0f));
    // A camera above the fog looking up sees less than one looking down into it.
    CHECK(render::fog_optical_depth(f, {0, 30, 0}, {0, 1, 0}, 100.0f) < render::fog_optical_depth(f, {0, 30, 0}, {0, -1, 0}, 20.0f));
    // No density, no fog.
    render::FogParams none;
    const auto clear = render::fog(none, {0, 1, 0}, level, 1000.0f, {0, 1, 0}, {5, 5, 5}, {1, 1, 1});
    CHECK(clear.transmittance == 1.0f);
    CHECK(clear.inscatter.x == 0.0f);
}

TEST_CASE("fog: towards the sky the depth is finite climbing, total on the horizon") {
    const auto f = thick();
    const Vec3 o{0, 12, 0};
    const Vec3 up = math::normalize(Vec3{1, 0.3f, 0});
    CHECK(render::fog_optical_depth_to_infinity(f, o, up) == doctest::Approx(render::fog_optical_depth(f, o, up, 5000.0f)).epsilon(1e-3));
    CHECK(std::exp(-render::fog_optical_depth_to_infinity(f, o, {1, 0, 0})) < 1e-6f);  // the horizon: fully fogged
}

TEST_CASE("fog: far away everything becomes the fog's own colour (the seam closes)") {
    const auto f = thick();
    const Vec3 o{0, 6, 0}, d{1, 0, 0}, to_sun = math::normalize(Vec3{1, 0.3f, 0});
    const Vec3 sun{8, 7, 6}, ambient{0.4f, 0.5f, 0.7f};
    const auto far_hill = render::fog(f, o, d, 2000.0f, to_sun, sun, ambient);
    const auto sky = render::fog(f, o, d, 0.0f, to_sun, sun, ambient, true);
    CHECK(far_hill.transmittance < 1e-3f);
    CHECK(far_hill.inscatter.x == doctest::Approx(sky.inscatter.x).epsilon(0.002));
    CHECK(far_hill.inscatter.z == doctest::Approx(sky.inscatter.z).epsilon(0.002));
    // ...which is the light it scatters: sun through the phase function plus the sky's average.
    const float phase = render::fog_phase(math::dot(d, to_sun), f.anisotropy);
    CHECK(sky.inscatter.y == doctest::Approx(sun.y * phase + ambient.y).epsilon(1e-4));
    // Fog glows towards the sun (forward scattering) and is dimmer away from it.
    const auto toward = render::fog(f, o, to_sun, 1e5f, to_sun, sun, ambient);
    const auto away = render::fog(f, o, to_sun * -1.0f, 1e5f, to_sun, sun, ambient);
    CHECK(toward.inscatter.x > 2.0f * away.inscatter.x);
}

TEST_CASE("fog: the phase function integrates to one") {
    for (const float g : {0.0f, 0.3f, 0.6f, 0.85f}) {
        double sum = 0;
        constexpr int kSteps = 20000;
        for (int i = 0; i < kSteps; ++i) {
            const double c = -1.0 + (i + 0.5) * 2.0 / kSteps;
            sum += render::fog_phase(static_cast<float>(c), g) * 2.0 * 3.14159265358979 * (2.0 / kSteps);
        }
        CAPTURE(g);
        CHECK(sum == doctest::Approx(1.0).epsilon(0.005));
    }
}
