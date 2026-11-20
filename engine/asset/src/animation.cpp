#include "noor/asset/animation.h"

#include <algorithm>
#include <cmath>

namespace noor::asset {
namespace {

using math::Quat;
using math::Vec3;

Quat normalized(Quat q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return len > 1e-12f ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}

// Key k's value (component c) of a channel with `n` components; cubic splines store in-tangent, value, out-tangent.
float value(const AnimationChannel& ch, std::size_t k, std::size_t c, int which = 1) {
    const std::size_t n = ch.path == AnimationChannel::Path::Rotation ? 4 : 3;
    if (ch.interpolation == AnimationChannel::Interpolation::CubicSpline) return ch.values[(k * 3 + static_cast<std::size_t>(which)) * n + c];
    return ch.values[k * n + c];
}

} // namespace

std::vector<NodePose> rest_pose(const ImportedModel& model) {
    std::vector<NodePose> pose(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) pose[i] = {model.nodes[i].translation, model.nodes[i].rotation, model.nodes[i].scale};
    return pose;
}

Quat slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {  // the shorter way round
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    float wa = 1.0f - t, wb = t;
    if (d < 0.9995f) {  // far enough apart for the angle to be well defined; nearly equal ones blend linearly
        const float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
        const float s = std::sin(angle);
        wa = std::sin((1.0f - t) * angle) / s;
        wb = std::sin(t * angle) / s;
    }
    return normalized({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

void sample(const ImportedAnimation& clip, double seconds, std::span<NodePose> pose) {
    for (const AnimationChannel& ch : clip.channels) {
        if (ch.node < 0 || static_cast<std::size_t>(ch.node) >= pose.size() || ch.times.empty()) continue;
        const std::size_t keys = ch.times.size();
        const std::size_t n = ch.path == AnimationChannel::Path::Rotation ? 4 : 3;
        const float t = static_cast<float>(seconds);
        // The key at or before t (k), the one after (k + 1), and how far between them (u, 0..1).
        std::size_t k = 0;
        float u = 0.0f, dt = 0.0f;
        if (t <= ch.times.front()) {
            k = 0;
        } else if (t >= ch.times.back()) {
            k = keys - 1;
        } else {
            k = static_cast<std::size_t>(std::upper_bound(ch.times.begin(), ch.times.end(), t) - ch.times.begin()) - 1;
            dt = ch.times[k + 1] - ch.times[k];
            u = dt > 0.0f ? (t - ch.times[k]) / dt : 0.0f;
        }
        const bool between = k + 1 < keys && u > 0.0f;
        float out[4] = {0, 0, 0, 1};
        using I = AnimationChannel::Interpolation;
        if (!between || ch.interpolation == I::Step) {
            for (std::size_t c = 0; c < n; ++c) out[c] = value(ch, k, c);
        } else if (ch.interpolation == I::Linear) {
            if (ch.path == AnimationChannel::Path::Rotation) {
                const Quat q = slerp({value(ch, k, 0), value(ch, k, 1), value(ch, k, 2), value(ch, k, 3)},
                                     {value(ch, k + 1, 0), value(ch, k + 1, 1), value(ch, k + 1, 2), value(ch, k + 1, 3)}, u);
                out[0] = q.x, out[1] = q.y, out[2] = q.z, out[3] = q.w;
            } else {
                for (std::size_t c = 0; c < n; ++c) out[c] = value(ch, k, c) + (value(ch, k + 1, c) - value(ch, k, c)) * u;
            }
        } else {  // glTF's cubic Hermite spline: value and out-tangent of k, in-tangent and value of k + 1
            const float u2 = u * u, u3 = u2 * u;
            const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
            for (std::size_t c = 0; c < n; ++c)
                out[c] = h00 * value(ch, k, c, 1) + h10 * dt * value(ch, k, c, 2) + h01 * value(ch, k + 1, c, 1) + h11 * dt * value(ch, k + 1, c, 0);
        }
        NodePose& p = pose[static_cast<std::size_t>(ch.node)];
        switch (ch.path) {
        case AnimationChannel::Path::Translation: p.translation = {out[0], out[1], out[2]}; break;
        case AnimationChannel::Path::Scale: p.scale = {out[0], out[1], out[2]}; break;
        case AnimationChannel::Path::Rotation: p.rotation = normalized({out[0], out[1], out[2], out[3]}); break;
        }
    }
}

const ImportedAnimation* find_animation(const ImportedModel& model, std::string_view name) {
    for (const auto& a : model.animations)
        if (a.name == name) return &a;
    return nullptr;
}

} // namespace noor::asset
