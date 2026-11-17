#include "noor/render/ibl.h"

#include <algorithm>
#include <cmath>

namespace noor::render {

std::vector<float> brdf_lut(std::uint32_t size, std::uint32_t samples) {
    constexpr float kPi = 3.14159265358979f;
    std::vector<float> out(static_cast<std::size_t>(size) * size * 2);
    for (std::uint32_t row = 0; row < size; ++row) {
        const float roughness = (static_cast<float>(row) + 0.5f) / static_cast<float>(size);
        const float alpha = roughness * roughness;
        for (std::uint32_t col = 0; col < size; ++col) {
            const float n_dot_v = (static_cast<float>(col) + 0.5f) / static_cast<float>(size);
            const float vx = std::sqrt(1.0f - n_dot_v * n_dot_v), vz = n_dot_v;  // v in the xz plane, n = +z
            float a = 0.0f, b = 0.0f;
            for (std::uint32_t i = 0; i < samples; ++i) {
                // Hammersley point and a GGX-distributed half vector.
                std::uint32_t bits = i;
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                const float x1 = static_cast<float>(i) / static_cast<float>(samples);
                const float x2 = static_cast<float>(bits) * 2.3283064365386963e-10f;
                const float phi = 2.0f * kPi * x1;
                const float cos_t = std::sqrt((1.0f - x2) / (1.0f + (alpha * alpha - 1.0f) * x2));
                const float sin_t = std::sqrt(std::max(0.0f, 1.0f - cos_t * cos_t));
                const float hx = sin_t * std::cos(phi), hy = sin_t * std::sin(phi), hz = cos_t;
                const float v_dot_h = vx * hx + vz * hz;
                const float lz = 2.0f * v_dot_h * hz - vz;  // l = reflect(-v, h); only its z (n·l) is needed
                (void)hy;
                const float n_dot_l = std::clamp(lz, 0.0f, 1.0f);
                const float n_dot_h = std::clamp(hz, 0.0f, 1.0f);
                if (n_dot_l <= 0.0f) continue;
                // Height-correlated Smith visibility, as in the shader (mesh.hlsl).
                const float a2 = alpha * alpha;
                const float gv = n_dot_l * std::sqrt(n_dot_v * n_dot_v * (1.0f - a2) + a2);
                const float gl = n_dot_v * std::sqrt(n_dot_l * n_dot_l * (1.0f - a2) + a2);
                const float vis = 0.5f / std::max(gv + gl, 1e-5f);
                const float g_vis = vis * 4.0f * n_dot_l * std::max(v_dot_h, 0.0f) / std::max(n_dot_h, 1e-5f);
                const float fc = std::pow(1.0f - std::max(v_dot_h, 0.0f), 5.0f);
                a += (1.0f - fc) * g_vis;
                b += fc * g_vis;
            }
            out[(static_cast<std::size_t>(row) * size + col) * 2] = a / static_cast<float>(samples);
            out[(static_cast<std::size_t>(row) * size + col) * 2 + 1] = b / static_cast<float>(samples);
        }
    }
    return out;
}

} // namespace noor::render
