// Block compression for textures (ADR-029, Milestone 5): BC7 for colour and data maps, BC5 for
// normal maps (two channels; the shader rebuilds Z). Encoded on the CPU with bc7enc / rgbcx
// (Richard Geldreich, MIT or public domain), in parallel over rows of 4x4 blocks, and cached with
// the rest of the import (texture.cpp), so the cost is paid once per texture and setting.
#include "noor/asset/texture.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <bc7decomp.h>
#include <bc7enc.h>
#include <rgbcx.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <execution>
#include <mutex>
#include <numeric>

namespace noor::asset {
namespace {

constexpr std::uint32_t kBlockBytes = 16;  // BC7 and BC5: 16 bytes per 4x4 block

void init_encoders() {
    static std::once_flag once;
    std::call_once(once, [] {
        bc7enc_compress_block_init();
        rgbcx::init();
    });
}

std::uint32_t blocks(std::uint32_t pixels) { return (pixels + 3) / 4; }

// The 4x4 RGBA8 pixels of block (bx, by); past the edge of a small mip, the edge pixel repeats.
void gather(const TextureMip& m, std::uint32_t bx, std::uint32_t by, std::uint8_t out[64]) {
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 4; ++x) {
            const std::uint32_t sx = std::min(bx * 4 + x, m.width - 1), sy = std::min(by * 4 + y, m.height - 1);
            std::memcpy(out + (y * 4 + x) * 4, m.bytes.data() + (static_cast<std::size_t>(sy) * m.width + sx) * 4, 4);
        }
}

TextureMip compress_mip(const TextureMip& m, TextureFormat target, const bc7enc_compress_block_params& params) {
    TextureMip out{m.width, m.height, {}};
    const std::uint32_t bw = blocks(m.width), bh = blocks(m.height);
    out.bytes.resize(static_cast<std::size_t>(bw) * bh * kBlockBytes);
    std::vector<std::uint32_t> rows(bh);
    std::iota(rows.begin(), rows.end(), 0u);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](std::uint32_t by) {
        std::uint8_t pixels[64];
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            gather(m, bx, by, pixels);
            std::uint8_t* dst = out.bytes.data() + (static_cast<std::size_t>(by) * bw + bx) * kBlockBytes;
            if (target == TextureFormat::Bc5Unorm) rgbcx::encode_bc5_hq(dst, pixels, 0, 1, 4);
            else bc7enc_compress_block(dst, pixels, &params);
        }
    });
    return out;
}

TextureMip decompress_mip(const TextureMip& m, TextureFormat format) {
    TextureMip out{m.width, m.height, std::vector<std::uint8_t>(static_cast<std::size_t>(m.width) * m.height * 4)};
    const std::uint32_t bw = blocks(m.width), bh = blocks(m.height);
    for (std::uint32_t by = 0; by < bh; ++by)
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const std::uint8_t* src = m.bytes.data() + (static_cast<std::size_t>(by) * bw + bx) * kBlockBytes;
            std::uint8_t pixels[64]{};
            if (format == TextureFormat::Bc5Unorm) {
                rgbcx::unpack_bc5(src, pixels, 0, 1, 4);
                for (int i = 0; i < 16; ++i) {
                    // Rebuild Z from X and Y (unit length), like the shader does.
                    const float x = pixels[i * 4] / 127.5f - 1.0f, y = pixels[i * 4 + 1] / 127.5f - 1.0f;
                    const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
                    pixels[i * 4 + 2] = static_cast<std::uint8_t>(std::lround((z * 0.5f + 0.5f) * 255.0f));
                    pixels[i * 4 + 3] = 255;
                }
            } else {
                bc7decomp::unpack_bc7(src, reinterpret_cast<bc7decomp::color_rgba*>(pixels));
            }
            for (std::uint32_t y = 0; y < 4; ++y)
                for (std::uint32_t x = 0; x < 4; ++x) {
                    const std::uint32_t px = bx * 4 + x, py = by * 4 + y;
                    if (px < m.width && py < m.height)
                        std::memcpy(out.bytes.data() + (static_cast<std::size_t>(py) * m.width + px) * 4, pixels + (y * 4 + x) * 4, 4);
                }
        }
    return out;
}

} // namespace

bool is_block_compressed(TextureFormat format) noexcept {
    return format == TextureFormat::Bc7Srgb || format == TextureFormat::Bc7Unorm || format == TextureFormat::Bc5Unorm;
}

std::uint64_t mip_bytes(TextureFormat format, std::uint32_t width, std::uint32_t height) noexcept {
    if (is_block_compressed(format)) return static_cast<std::uint64_t>(blocks(width)) * blocks(height) * kBlockBytes;
    return static_cast<std::uint64_t>(width) * height * bytes_per_pixel(format);
}

TextureFormat compressed_format_for(const TextureSettings& s, std::uint32_t width, std::uint32_t height) noexcept {
    // Any size works: Vulkan doesn't need BC images to be multiples of 4 (edge blocks are padded).
    (void)width;
    (void)height;
    if (!s.compress || s.kind == TextureKind::Hdr) {
        return s.kind == TextureKind::Hdr ? TextureFormat::Rgba16Float : s.kind == TextureKind::Color ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
    }
    return s.kind == TextureKind::Color ? TextureFormat::Bc7Srgb : s.kind == TextureKind::Normal ? TextureFormat::Bc5Unorm : TextureFormat::Bc7Unorm;
}

TextureData compress_texture(const TextureData& rgba8, TextureFormat target) {
    if (!is_block_compressed(target) || is_block_compressed(rgba8.format) || rgba8.format == TextureFormat::Rgba16Float) return rgba8;
    init_encoders();
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    // Colour: errors weighted as the eye sees them; data and normals: every channel counts the same.
    if (target == TextureFormat::Bc7Unorm) bc7enc_compress_block_params_init_linear_weights(&params);
    TextureData out;
    out.format = target;
    out.source_width = rgba8.source_width;
    out.source_height = rgba8.source_height;
    for (const auto& m : rgba8.mips) out.mips.push_back(compress_mip(m, target, params));
    return out;
}

TextureData decompress_texture(const TextureData& data) {
    if (!is_block_compressed(data.format)) return data;
    TextureData out;
    out.format = data.format == TextureFormat::Bc7Srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
    out.source_width = data.source_width;
    out.source_height = data.source_height;
    for (const auto& m : data.mips) out.mips.push_back(decompress_mip(m, data.format));
    return out;
}

} // namespace noor::asset
