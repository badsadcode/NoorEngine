// Sun shadow cascades (Milestone 5, part 4): coverage, ordering and stability, without a GPU.
#include "noor/render/renderer.h"
#include "noor/render/shadows.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>

using namespace noor;
using math::Vec3;

namespace {

render::Camera camera_at(Vec3 eye, Vec3 target) {
    render::Camera c;
    c.view = math::look_at(eye, target, {0, 1, 0});
    c.fov_y = math::radians(60.0f);
    c.near_plane = 0.1f;
    return c;
}

Vec3 shadow_space(const render::ShadowCascade& c, Vec3 p) {
    const math::Mat4& m = c.clip_from_world;
    return m.transform_point(p);  // orthographic: w stays 1
}

} // namespace

TEST_CASE("shadows: cascades split the distance in order, finer near the camera") {
    const auto cam = camera_at({0, 2, 10}, {0, 1, 0});
    render::ShadowCascadeParams params;
    params.shadow_distance = 80.0f;
    const auto cascades = render::compute_cascades(cam, 16.0f / 9.0f, math::normalize(Vec3{-0.4f, -0.8f, -0.45f}), params);
    float previous = 0.0f;
    for (const auto& c : cascades) {
        CHECK(c.split_far > previous);
        CHECK(c.texel_world > 0.0f);
        previous = c.split_far;
    }
    CHECK(cascades.back().split_far == doctest::Approx(80.0f));
    CHECK(cascades[0].split_far < 80.0f / 4.0f);              // the first slice is shorter than an even split
    CHECK(cascades[0].texel_world < cascades[3].texel_world);  // and sharper
}

TEST_CASE("shadows: every point of a cascade's slice of the view lands inside its shadow map") {
    const Vec3 eye{3, 1.7f, -4}, target{10, 1, 12};
    const auto cam = camera_at(eye, target);
    const float aspect = 16.0f / 9.0f;
    render::ShadowCascadeParams params;
    const auto cascades = render::compute_cascades(cam, aspect, math::normalize(Vec3{0.3f, -1.0f, 0.2f}), params);
    const math::Mat4 world_from_view = math::inverse(cam.view);
    const float tan_half = std::tan(cam.fov_y * 0.5f);
    float near_d = cam.near_plane;
    for (const auto& c : cascades) {
        for (const float d : {near_d, c.split_far}) {
            for (const float sx : {-1.0f, 1.0f})
                for (const float sy : {-1.0f, 1.0f}) {
                    const Vec3 corner = world_from_view.transform_point({sx * d * tan_half * aspect, sy * d * tan_half, -d});
                    const Vec3 s = shadow_space(c, corner);
                    CHECK(std::abs(s.x) <= 1.0001f);
                    CHECK(std::abs(s.y) <= 1.0001f);
                    CHECK(s.z >= 0.0f);
                    CHECK(s.z <= 1.0f);
                }
        }
        near_d = c.split_far;
    }
}

TEST_CASE("shadows: moving the camera a little moves the shadow map by whole texels (no shimmering)") {
    const Vec3 sun = math::normalize(Vec3{-0.4f, -0.8f, -0.45f});
    render::ShadowCascadeParams params;
    const auto a = render::compute_cascades(camera_at({0, 2, 10}, {0, 1, 0}), 1.5f, sun, params);
    const auto b = render::compute_cascades(camera_at({0.037f, 2, 10.011f}, {0.037f, 1, 0.011f}), 1.5f, sun, params);
    // Turning the camera keeps the map's size (texel) the same.
    const auto turned = render::compute_cascades(camera_at({0, 2, 10}, {5, 1, 0}), 1.5f, sun, params);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].texel_world == doctest::Approx(b[i].texel_world));
        CHECK(a[i].texel_world == doctest::Approx(turned[i].texel_world));
        // A fixed world point's position in the map changes by a whole number of texels.
        const Vec3 p{1, 0, 2};
        const Vec3 pa = shadow_space(a[i], p), pb = shadow_space(b[i], p);
        const float texels_x = (pb.x - pa.x) * 0.5f * static_cast<float>(params.resolution);
        const float texels_y = (pb.y - pa.y) * 0.5f * static_cast<float>(params.resolution);
        CHECK(std::abs(texels_x - std::round(texels_x)) < 0.01f);
        CHECK(std::abs(texels_y - std::round(texels_y)) < 0.01f);
    }
}

TEST_CASE("shadows: casters outside a cascade are skipped, ones that reach it are kept") {
    const auto cam = camera_at({0, 2, 10}, {0, 1, 0});
    render::ShadowCascadeParams params;
    params.shadow_distance = 80.0f;
    const Vec3 sun = math::normalize(Vec3{-0.4f, -0.8f, -0.45f});
    const auto cascades = render::compute_cascades(cam, 16.0f / 9.0f, sun, params);
    // Right in front of the camera: inside the first (nearest) cascade.
    CHECK(render::sphere_touches_cascade(cascades[0], {0, 0.5f, 6}, 0.5f));
    // 60 m ahead: too far for the first cascade, inside the last one.
    const Vec3 far_point{0, 0.5f, -50};
    CHECK_FALSE(render::sphere_touches_cascade(cascades[0], far_point, 0.5f));
    CHECK(render::sphere_touches_cascade(cascades[3], far_point, 0.5f));
    // Far off to the side: in no cascade at all...
    for (const auto& c : cascades) CHECK_FALSE(render::sphere_touches_cascade(c, {400, 0, 0}, 1.0f));
    // ...unless it is big enough to reach in.
    CHECK(render::sphere_touches_cascade(cascades[3], {400, 0, 0}, 500.0f));
    // A caster high up towards the sun (it shades the ground in front of the camera) still counts.
    CHECK(render::sphere_touches_cascade(cascades[0], Vec3{0, 0.5f, 6} - sun * 40.0f, 0.5f));
}
