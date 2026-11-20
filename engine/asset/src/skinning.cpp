#include "noor/asset/skinning.h"

#include <algorithm>

namespace noor::asset {

std::vector<math::Mat4> skin_palette(const ImportedSkin& skin, std::span<const math::Mat4> joint_worlds) {
    std::vector<math::Mat4> palette(skin.joints.size(), math::Mat4::identity());
    const std::size_t n = std::min({palette.size(), skin.inverse_bind.size(), joint_worlds.size()});
    for (std::size_t j = 0; j < n; ++j) palette[j] = joint_worlds[j] * skin.inverse_bind[j];
    return palette;
}

namespace {

// The weighted sum of the four joints' matrices, row by row as the shader does (fixed order: the same bits).
math::Mat4 blended(const SkinWeights& w, std::span<const math::Mat4> palette) {
    math::Mat4 m;
    for (auto& c : m.m) c = {0, 0, 0, 0};
    const float* wv = &w.weights.x;
    for (int k = 0; k < 4; ++k) {
        const std::size_t j = w.joints[static_cast<std::size_t>(k)];
        if (wv[k] == 0.0f || j >= palette.size()) continue;
        for (int c = 0; c < 4; ++c) {
            const math::Vec4& src = palette[j].m[c];
            m.m[c] = {m.m[c].x + src.x * wv[k], m.m[c].y + src.y * wv[k], m.m[c].z + src.z * wv[k], m.m[c].w + src.w * wv[k]};
        }
    }
    return m;
}

} // namespace

math::Vec3 skin_point(const SkinWeights& w, std::span<const math::Mat4> palette, math::Vec3 p) {
    const math::Mat4 m = blended(w, palette);
    return {m.m[0].x * p.x + m.m[1].x * p.y + m.m[2].x * p.z + m.m[3].x, m.m[0].y * p.x + m.m[1].y * p.y + m.m[2].y * p.z + m.m[3].y,
            m.m[0].z * p.x + m.m[1].z * p.y + m.m[2].z * p.z + m.m[3].z};
}

math::Vec3 skin_direction(const SkinWeights& w, std::span<const math::Mat4> palette, math::Vec3 d) {
    const math::Mat4 m = blended(w, palette);
    const math::Vec3 r{m.m[0].x * d.x + m.m[1].x * d.y + m.m[2].x * d.z, m.m[0].y * d.x + m.m[1].y * d.y + m.m[2].y * d.z,
                       m.m[0].z * d.x + m.m[1].z * d.y + m.m[2].z * d.z};
    const float len = math::length(r);
    return len > 1e-12f ? r * (1.0f / len) : d;
}

} // namespace noor::asset
