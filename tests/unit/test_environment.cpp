// Environments and image-based lighting maths (Milestone 5, part 5), without a GPU.
#include "noor/asset/environment.h"
#include "noor/render/ibl.h"

#include <ostream>
#include <doctest/doctest.h>

#include <bit>
#include <cmath>

using namespace noor;
using math::Vec3;

namespace {

asset::EquirectImage constant(float value, std::uint32_t w = 64, std::uint32_t h = 32) {
    return {w, h, std::vector<float>(static_cast<std::size_t>(w) * h * 3, value)};
}

float half_to_float(std::uint16_t h) {
    const std::uint32_t exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
    if (exp == 0) return 0.0f;
    return std::bit_cast<float>(((exp + 112) << 23) | (mant << 13));
}

} // namespace

TEST_CASE("environment: equirect and cube directions agree with each other") {
    const Vec3 forward = asset::equirect_direction(0.5f, 0.5f);
    CHECK(forward.z == doctest::Approx(-1.0f));  // the middle of the picture looks forward (-Z)
    CHECK(asset::equirect_direction(0.75f, 0.5f).x == doctest::Approx(1.0f));  // a quarter to the right: +X
    CHECK(asset::equirect_direction(0.5f, 0.0f).y == doctest::Approx(1.0f));  // the top row: straight up
    for (const Vec3 d : {Vec3{0.3f, 0.5f, -0.8f}, Vec3{-0.7f, -0.2f, 0.4f}, Vec3{0.1f, 0.95f, 0.2f}}) {
        const Vec3 n = math::normalize(d);
        const auto uv = asset::equirect_uv(n);
        const Vec3 back = asset::equirect_direction(uv.x, uv.y);
        CHECK(math::dot(back, n) == doctest::Approx(1.0f).epsilon(1e-4));
    }
    // Vulkan cube faces: the centre of +X looks along +X, of -Z along -Z.
    CHECK(asset::cube_direction(0, 7, 7, 15).x == doctest::Approx(1.0f));
    CHECK(asset::cube_direction(5, 7, 7, 15).z == doctest::Approx(-1.0f));
    CHECK(asset::cube_direction(2, 7, 7, 15).y == doctest::Approx(1.0f));
}

TEST_CASE("environment: a constant sky gives irradiance pi * L everywhere, and the same in every mip") {
    const auto env = asset::make_environment(constant(2.0f), 16);
    for (const Vec3 n : {Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, math::normalize(Vec3{1, 1, -1})}) {
        const Vec3 e = asset::evaluate_sh(env.irradiance_sh, n);
        CHECK(e.x == doctest::Approx(2.0f * math::kPi).epsilon(0.02));
    }
    REQUIRE(env.mips.size() == 5);  // 16, 8, 4, 2, 1
    for (std::size_t m = 0; m < env.mips.size(); ++m)
        CHECK(half_to_float(env.mips[m][3][0]) == doctest::Approx(2.0f).epsilon(0.02));  // blurring a constant changes nothing
}

TEST_CASE("environment: a bright sky over a dark ground lights upward faces more") {
    auto img = constant(0.1f);
    for (std::uint32_t y = 0; y < img.height / 2; ++y)
        for (std::uint32_t x = 0; x < img.width * 3; ++x) img.rgb[static_cast<std::size_t>(y) * img.width * 3 + x] = 4.0f;
    const auto env = asset::make_environment(img, 16);
    const Vec3 up = asset::evaluate_sh(env.irradiance_sh, {0, 1, 0});
    const Vec3 down = asset::evaluate_sh(env.irradiance_sh, {0, -1, 0});
    const Vec3 side = asset::evaluate_sh(env.irradiance_sh, {1, 0, 0});
    CHECK(up.x > side.x);
    CHECK(side.x > down.x);
    // The roughest mip of the down-facing cube face is darker than its sharp one is bright up top.
    CHECK(half_to_float(env.mips.back()[2][0]) > half_to_float(env.mips.back()[3][0]));  // +Y face vs -Y face

    const auto sky = asset::procedural_sky({0.2f, 0.4f, 1.0f}, {0.8f, 0.8f, 0.9f}, {0.3f, 0.25f, 0.2f}, 32, 16);
    CHECK(sky.rgb[2] == doctest::Approx(1.0f).epsilon(0.1));  // top row: the zenith colour (blue)
    CHECK(sky.rgb[(15 * 32) * 3] == doctest::Approx(0.3f).epsilon(0.05));  // bottom row: the ground
}

TEST_CASE("ibl: the BRDF lookup table is a sensible split-sum table") {
    const auto lut = render::brdf_lut(16, 128);
    REQUIRE(lut.size() == 16 * 16 * 2);
    for (std::size_t i = 0; i < lut.size(); i += 2) {
        CHECK(lut[i] >= 0.0f);
        CHECK(lut[i + 1] >= 0.0f);
        CHECK(lut[i] + lut[i + 1] <= 1.02f);  // reflects at most what arrives
    }
    // Smooth surface seen head-on: specular = F0 (A ~ 1, B ~ 0).
    const float a_smooth = lut[(0 * 16 + 15) * 2], b_smooth = lut[(0 * 16 + 15) * 2 + 1];
    CHECK(a_smooth > 0.9f);
    CHECK(b_smooth < 0.05f);
    // At grazing angles Fresnel takes over: B grows.
    CHECK(lut[(0 * 16 + 0) * 2 + 1] > b_smooth);
}
