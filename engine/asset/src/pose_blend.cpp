#include "noor/asset/pose_blend.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace noor::asset {
namespace {

using math::Quat;
using math::Vec3;

float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

Quat normalized(Quat q) {
    const float len = std::sqrt(dot(q, q));  // sqrt is exact (IEEE): the same everywhere
    return len > 1e-12f ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}

Quat mul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Vec3 lerp(Vec3 a, Vec3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

NodePose blend_one(const NodePose& a, const NodePose& b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return {lerp(a.translation, b.translation, t), asset::nlerp(a.rotation, b.rotation, t), lerp(a.scale, b.scale, t)};
}

} // namespace

Quat nlerp(Quat a, Quat b, float t) {
    if (dot(a, b) < 0.0f) b = {-b.x, -b.y, -b.z, -b.w};
    const float s = 1.0f - t;
    return normalized({a.x * s + b.x * t, a.y * s + b.y * t, a.z * s + b.z * t, a.w * s + b.w * t});
}

void blend(std::span<const NodePose> a, std::span<const NodePose> b, float t, std::span<NodePose> out) {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) out[i] = blend_one(a[i], b[i], t);
}

void blend(std::span<const NodePose> a, std::span<const NodePose> b, std::span<const float> weights, std::span<NodePose> out) {
    const std::size_t n = std::min({a.size(), b.size(), out.size(), weights.size()});
    for (std::size_t i = 0; i < n; ++i) out[i] = blend_one(a[i], b[i], weights[i]);
}

void mix(std::span<const std::span<const NodePose>> poses, std::span<const float> weights, std::span<NodePose> out) {
    const std::size_t count = std::min(poses.size(), weights.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        Vec3 t{0, 0, 0}, s{0, 0, 0};
        Quat r{0, 0, 0, 0}, first{};
        bool any = false;
        float total = 0.0f;
        for (std::size_t k = 0; k < count; ++k) {
            const float w = weights[k];
            if (w <= 0.0f || i >= poses[k].size()) continue;
            const NodePose& p = poses[k][i];
            Quat q = p.rotation;
            if (!any) first = q;
            else if (dot(first, q) < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};
            any = true;
            total += w;
            t = {t.x + p.translation.x * w, t.y + p.translation.y * w, t.z + p.translation.z * w};
            s = {s.x + p.scale.x * w, s.y + p.scale.y * w, s.z + p.scale.z * w};
            r = {r.x + q.x * w, r.y + q.y * w, r.z + q.z * w, r.w + q.w * w};
        }
        if (!any) continue;  // no pose has weight: out keeps what it had
        const float inv = 1.0f / total;
        out[i] = {{t.x * inv, t.y * inv, t.z * inv}, normalized(r), {s.x * inv, s.y * inv, s.z * inv}};
    }
}

NodePose additive_delta(const NodePose& reference, const NodePose& pose) {
    const Quat inv{-reference.rotation.x, -reference.rotation.y, -reference.rotation.z, reference.rotation.w};
    auto div = [](float a, float b) { return std::abs(b) > 1e-12f ? a / b : 1.0f; };
    return {{pose.translation.x - reference.translation.x, pose.translation.y - reference.translation.y, pose.translation.z - reference.translation.z},
            normalized(mul(inv, pose.rotation)),
            {div(pose.scale.x, reference.scale.x), div(pose.scale.y, reference.scale.y), div(pose.scale.z, reference.scale.z)}};
}

void apply_additive(std::span<NodePose> base, std::span<const NodePose> delta, std::span<const float> weights) {
    const std::size_t n = std::min({base.size(), delta.size(), weights.size()});
    for (std::size_t i = 0; i < n; ++i) {
        const float w = weights[i];
        if (w <= 0.0f) continue;
        const NodePose& d = delta[i];
        NodePose& b = base[i];
        b.translation = {b.translation.x + d.translation.x * w, b.translation.y + d.translation.y * w, b.translation.z + d.translation.z * w};
        b.rotation = normalized(mul(b.rotation, w >= 1.0f ? d.rotation : asset::nlerp(Quat{}, d.rotation, w)));
        const Vec3 s = lerp({1, 1, 1}, d.scale, w);
        b.scale = {b.scale.x * s.x, b.scale.y * s.y, b.scale.z * s.z};
    }
}

std::vector<float> blend_weights_1d(std::span<const float> at, float x) {
    std::vector<float> w(at.size(), 0.0f);
    if (at.empty()) return w;
    // The nearest sample at or below x and the nearest above it (by position, then by index: deterministic).
    int lo = -1, hi = -1;
    for (int i = 0; i < static_cast<int>(at.size()); ++i) {
        const float a = at[static_cast<std::size_t>(i)];
        if (a <= x && (lo < 0 || a > at[static_cast<std::size_t>(lo)])) lo = i;
        if (a > x && (hi < 0 || a < at[static_cast<std::size_t>(hi)])) hi = i;
    }
    if (lo < 0) {
        w[static_cast<std::size_t>(hi)] = 1.0f;  // below every sample
    } else if (hi < 0) {
        w[static_cast<std::size_t>(lo)] = 1.0f;  // above every sample
    } else {
        const float a = at[static_cast<std::size_t>(lo)], b = at[static_cast<std::size_t>(hi)];
        const float t = (x - a) / (b - a);
        w[static_cast<std::size_t>(lo)] = 1.0f - t;
        w[static_cast<std::size_t>(hi)] = t;
    }
    return w;
}

std::vector<float> blend_weights_2d(std::span<const std::array<float, 2>> at, std::array<float, 2> p) {
    std::vector<float> w(at.size(), 0.0f);
    if (at.empty()) return w;
    if (at.size() == 1) {
        w[0] = 1.0f;
        return w;
    }
    float total = 0.0f;
    for (std::size_t i = 0; i < at.size(); ++i) {
        float h = 1.0f;
        const float px = p[0] - at[i][0], py = p[1] - at[i][1];
        for (std::size_t j = 0; j < at.size(); ++j) {
            if (j == i) continue;
            const float ex = at[j][0] - at[i][0], ey = at[j][1] - at[i][1];
            const float len2 = ex * ex + ey * ey;
            if (len2 <= 1e-12f) continue;  // two samples in the same place
            h = std::min(h, std::clamp(1.0f - (px * ex + py * ey) / len2, 0.0f, 1.0f));
        }
        w[i] = h;
        total += h;
    }
    if (total <= 0.0f) {  // outside them all in a way nothing covers: the nearest sample
        std::size_t best = 0;
        float d = INFINITY;
        for (std::size_t i = 0; i < at.size(); ++i) {
            const float dx = p[0] - at[i][0], dy = p[1] - at[i][1];
            if (dx * dx + dy * dy < d) d = dx * dx + dy * dy, best = i;
        }
        w[best] = 1.0f;
        return w;
    }
    for (float& v : w) v /= total;
    return w;
}

} // namespace noor::asset
