#pragma once
// The UI spike's tiny CPU rasterizer (gameplay foundations, part 6; ADR-041): triangles with a colour each corner and an
// optional texture, blended "over" into an sRGB picture, clipped to a scissor. Both spike UIs (ours and RmlUi) draw
// through it, so their pictures can be compared side by side without either touching the Vulkan renderer.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ui_spike {

struct Vert {
    float x, y;        // pixels
    float u, v;        // texture coordinates (0..1)
    float r, g, b, a;  // 0..1; `premultiplied` says how
};

struct Texture {
    int width = 0, height = 0;
    int channels = 4;  // 1: an SDF (ours: 0.5 is the edge) or a coverage mask; 4: RGBA8 (premultiplied for RmlUi)
    bool sdf = false;
    std::vector<std::uint8_t> pixels;
    float sample(float u, float v, int c) const {
        const float x = std::clamp(u * width - 0.5f, 0.0f, static_cast<float>(width - 1));
        const float y = std::clamp(v * height - 0.5f, 0.0f, static_cast<float>(height - 1));
        const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
        const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
        const float fx = x - x0, fy = y - y0;
        const auto p = [&](int px, int py) { return pixels[(static_cast<std::size_t>(py) * width + px) * channels + c] / 255.0f; };
        return (p(x0, y0) * (1 - fx) + p(x1, y0) * fx) * (1 - fy) + (p(x0, y1) * (1 - fx) + p(x1, y1) * fx) * fy;
    }
};

struct Image {
    int width, height;
    std::vector<float> rgb;  // linear, 0..1
    Image(int w, int h) : width(w), height(h), rgb(static_cast<std::size_t>(w) * h * 3, 0.0f) {}

    static float to_linear(float s) { return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f); }
    static float to_srgb(float l) {
        l = std::clamp(l, 0.0f, 1.0f);
        return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    }

    // A triangle. `linear`: the colours are linear (ours) or sRGB (RmlUi's); `premultiplied`: colour already times alpha.
    // SDF textures: edge at 0.5, softened over `sdf_width` (in SDF units).
    void triangle(const Vert& a, const Vert& b, const Vert& c, const Texture* tex, bool linear, bool premultiplied,
                  std::array<int, 4> scissor, float sdf_width = 0.08f) {
        const float minx = std::max<float>(static_cast<float>(scissor[0]), std::floor(std::min({a.x, b.x, c.x})));
        const float maxx = std::min<float>(static_cast<float>(scissor[2]), std::ceil(std::max({a.x, b.x, c.x})));
        const float miny = std::max<float>(static_cast<float>(scissor[1]), std::floor(std::min({a.y, b.y, c.y})));
        const float maxy = std::min<float>(static_cast<float>(scissor[3]), std::ceil(std::max({a.y, b.y, c.y})));
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-9f) return;
        // 4x4 samples a pixel: smooth edges for both (RmlUi relies on the GPU's; ours too).
        for (int y = static_cast<int>(miny); y < static_cast<int>(maxy); ++y) {
            for (int x = static_cast<int>(minx); x < static_cast<int>(maxx); ++x) {
                float cover = 0, cr = 0, cg = 0, cb = 0, ca = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                        const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                        const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                        const float w2 = 1 - w0 - w1;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        const float u = a.u * w0 + b.u * w1 + c.u * w2, v = a.v * w0 + b.v * w1 + c.v * w2;
                        float r = a.r * w0 + b.r * w1 + c.r * w2, g = a.g * w0 + b.g * w1 + c.g * w2;
                        float bl = a.b * w0 + b.b * w1 + c.b * w2, al = a.a * w0 + b.a * w1 + c.a * w2;
                        if (tex && tex->sdf) {
                            const float d = tex->sample(u, v, 0);
                            const float t = std::clamp((d - (0.5f - sdf_width)) / (2 * sdf_width), 0.0f, 1.0f);
                            al *= t * t * (3 - 2 * t);
                        } else if (tex && tex->channels == 1) {
                            const float m = tex->sample(u, v, 0);
                            al *= m;
                            if (premultiplied) r *= m, g *= m, bl *= m;
                        } else if (tex) {
                            r *= tex->sample(u, v, 0), g *= tex->sample(u, v, 1), bl *= tex->sample(u, v, 2);
                            al *= tex->sample(u, v, 3);
                        }
                        if (!linear) {  // sRGB colours: blend in linear light, as the GPU does with an sRGB target
                            const float k = premultiplied && al > 0 ? al : 1.0f;
                            r = to_linear(r / k) * k, g = to_linear(g / k) * k, bl = to_linear(bl / k) * k;
                        }
                        if (!premultiplied) r *= al, g *= al, bl *= al;
                        cr += r, cg += g, cb += bl, ca += al, cover += 1;
                    }
                if (cover == 0) continue;
                const float n = 16.0f;
                float* p = &rgb[(static_cast<std::size_t>(y) * width + x) * 3];
                const float alpha = ca / n;
                p[0] = cr / n + p[0] * (1 - alpha);
                p[1] = cg / n + p[1] * (1 - alpha);
                p[2] = cb / n + p[2] * (1 - alpha);
            }
        }
    }

    std::vector<std::uint8_t> srgb8() const {
        std::vector<std::uint8_t> out(rgb.size());
        for (std::size_t i = 0; i < rgb.size(); ++i) out[i] = static_cast<std::uint8_t>(std::lround(to_srgb(rgb[i]) * 255.0f));
        return out;
    }
};

} // namespace ui_spike
