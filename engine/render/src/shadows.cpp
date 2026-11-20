#include "noor/render/shadows.h"

#include "noor/render/renderer.h"

#include <algorithm>
#include <cmath>

namespace noor::render {

std::array<ShadowCascade, kShadowCascades> compute_cascades(const Camera& camera, float aspect, math::Vec3 sun_direction,
                                                            const ShadowCascadeParams& params) {
    using math::Vec3;
    std::array<ShadowCascade, kShadowCascades> out{};
    const math::Mat4 world_from_view = math::inverse(camera.view);
    const Vec3 eye = world_from_view.transform_point({0, 0, 0});
    const Vec3 forward = math::normalize(math::transform_vector(world_from_view, {0, 0, -1}));
    const float tan_half = std::tan(camera.fov_y * 0.5f);
    const float near_plane = std::max(camera.near_plane, 0.01f);
    const float far_plane = std::max(params.shadow_distance, near_plane * 2.0f);

    // The sun's view: a fixed rotation (independent of the camera, so snapping works).
    const Vec3 dir = math::length(sun_direction) > 1e-6f ? math::normalize(sun_direction) : Vec3{0, -1, 0};
    const Vec3 helper = std::abs(dir.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const math::Mat4 light_view = math::look_at({0, 0, 0}, dir, helper);

    float previous = near_plane;
    for (std::uint32_t i = 0; i < kShadowCascades; ++i) {
        // Practical split scheme: a blend of uniform and logarithmic slices.
        const float t = static_cast<float>(i + 1) / kShadowCascades;
        const float uniform = near_plane + (far_plane - near_plane) * t;
        const float logarithmic = near_plane * std::pow(far_plane / near_plane, t);
        const float split = i + 1 == kShadowCascades ? far_plane : params.split_lambda * logarithmic + (1.0f - params.split_lambda) * uniform;

        // The slice's bounding sphere: its centre lies on the view axis, so the radius depends only
        // on the slice's depths, fov and aspect, never on where the camera looks.
        // A parallel view (9A part 1): every slice is the same box, the view's height and width all the way.
        const float h0 = camera.orthographic() ? 0.5f * camera.ortho_height : previous * tan_half, w0 = h0 * aspect;
        const float h1 = camera.orthographic() ? 0.5f * camera.ortho_height : split * tan_half, w1 = h1 * aspect;
        const float d0 = previous, d1 = split;
        const float r0 = w0 * w0 + h0 * h0, r1 = w1 * w1 + h1 * h1;  // squared corner offsets from the axis
        // Centre at distance c minimising the max distance to both rings: (d1² + r1 - d0² - r0) / (2(d1 - d0)), clamped.
        float c = (d1 * d1 + r1 - d0 * d0 - r0) / (2.0f * (d1 - d0));
        c = std::clamp(c, d0, d1);
        float radius = std::sqrt(std::max((c - d0) * (c - d0) + r0, (d1 - c) * (d1 - c) + r1));
        radius = std::ceil(radius * 16.0f) / 16.0f;  // a fixed, round size
        const Vec3 center = eye + forward * c;

        // Snap the centre to whole shadow-map texels, in the sun's view.
        const float texel = 2.0f * radius / static_cast<float>(std::max(1u, params.resolution));
        Vec3 ls = light_view.transform_point(center);
        ls.x = std::floor(ls.x / texel) * texel;
        ls.y = std::floor(ls.y / texel) * texel;
        // Depth along the sun (view space looks down -z): from caster_reach behind the slice to its far side.
        const float depth_center = -ls.z;
        const float dmin = depth_center - radius - params.caster_reach;
        const float dmax = depth_center + radius;
        math::Mat4 proj;
        proj.m[0] = {1.0f / radius, 0, 0, 0};
        proj.m[1] = {0, 1.0f / radius, 0, 0};
        proj.m[2] = {0, 0, -1.0f / (dmax - dmin), 0};
        proj.m[3] = {-ls.x / radius, -ls.y / radius, -dmin / (dmax - dmin), 1};
        out[i] = {proj * light_view, split, texel};
        previous = split;
    }
    return out;
}

bool sphere_touches_cascade(const ShadowCascade& cascade, math::Vec3 center, float radius) noexcept {
    const math::Mat4& m = cascade.clip_from_world;
    // Clip x and y of the centre, and the radius in clip units (an orthographic projection scales
    // every direction of a row the same way).
    const float x = m.m[0].x * center.x + m.m[1].x * center.y + m.m[2].x * center.z + m.m[3].x;
    const float y = m.m[0].y * center.x + m.m[1].y * center.y + m.m[2].y * center.z + m.m[3].y;
    const float rx = radius * math::length(math::Vec3{m.m[0].x, m.m[1].x, m.m[2].x});
    const float ry = radius * math::length(math::Vec3{m.m[0].y, m.m[1].y, m.m[2].y});
    return std::abs(x) <= 1.0f + rx && std::abs(y) <= 1.0f + ry;
}

} // namespace noor::render
