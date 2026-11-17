#pragma once
// Textures (ADR-029): decoding image files, their import settings, mipmaps and the derived-data
// cache. The renderer gets ready-to-upload mip chains; nothing here touches the GPU.
//
// Settings live in the texture's .noormeta ("settings"), guessed from the file name the first
// time the registry sees the file and editable afterwards:
//   { "kind": "color" | "data" | "normal" | "hdr", "normal_convention": "opengl" | "directx",
//     "mips": true, "max_size": 4096, "compression": "auto" | "none" }
//   color   base colour, emissive: sRGB, stored sRGB (decoded to linear when sampled)
//   data    roughness, metallic, AO, masks, height: linear, stored as is
//   normal  normal maps: linear; "directx" ones have green flipped on import (glTF uses OpenGL's)
//   hdr     .hdr light values (.exr comes with environments, part 5): 16-bit float
// compression "auto" (Milestone 5): colour and data maps become BC7, normal maps BC5 (X and Y; the
// shader rebuilds Z): 1 byte per pixel instead of 4, any size (edge blocks are padded). HDR
// textures stay uncompressed. "none" keeps RGBA8 (pixel-exact, 4x the memory).
#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace noor::asset {

// Bump when decoding or processing changes, so cached results are rebuilt.
inline constexpr int kTextureImporterVersion = 2;  // 2: block compression

enum class TextureKind : std::uint8_t { Color, Data, Normal, Hdr };

struct TextureSettings {
    TextureKind kind = TextureKind::Color;
    bool directx_normals = false;  // flip green on import
    bool mips = true;
    std::uint32_t max_size = 4096;  // larger images are halved until they fit
    bool compress = true;           // "compression": "auto" (BC7/BC5) or "none"

    [[nodiscard]] Json to_json() const;
    [[nodiscard]] static TextureSettings from_json(const Json& settings);
    friend bool operator==(const TextureSettings&, const TextureSettings&) = default;
};

[[nodiscard]] std::string_view kind_name(TextureKind kind) noexcept;

// File types the texture importer reads: .png (8/16-bit), .jpg/.jpeg, .tga, .bmp, .hdr.
[[nodiscard]] bool is_texture_file(const std::filesystem::path& path);

// First guess at the settings from the file name: "*_normal*", "*_nrm*" -> normal (DirectX when the
// name says "dx"/"directx"); "*_rough*", "*_metal*", "*_ao*", "*_height*", "*_mask*", "*_orm*",
// "*_disp*" -> data; .hdr -> hdr; anything else colour.
[[nodiscard]] TextureSettings guess_texture_settings(const std::filesystem::path& path);

enum class TextureFormat : std::uint8_t { Rgba8Srgb, Rgba8Unorm, Rgba16Float, Bc7Srgb, Bc7Unorm, Bc5Unorm };
// Uncompressed formats only (BC formats store 4x4 blocks: see mip_bytes).
[[nodiscard]] std::uint32_t bytes_per_pixel(TextureFormat format) noexcept;
[[nodiscard]] bool is_block_compressed(TextureFormat format) noexcept;
// Bytes of one mip of this size (BC7 and BC5: 16 bytes per 4x4 block, partial blocks rounded up).
[[nodiscard]] std::uint64_t mip_bytes(TextureFormat format, std::uint32_t width, std::uint32_t height) noexcept;
[[nodiscard]] std::string_view format_name(TextureFormat format) noexcept;

struct TextureMip {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> bytes;  // tightly packed rows
};

struct TextureData {
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::vector<TextureMip> mips;  // mips[0] is the full size
    std::uint32_t source_width = 0, source_height = 0;
    [[nodiscard]] std::uint32_t width() const noexcept { return mips.empty() ? 0 : mips[0].width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return mips.empty() ? 0 : mips[0].height; }
    [[nodiscard]] std::uint64_t gpu_bytes() const noexcept;
};

// The format a texture of this size gets with these settings (BC7, BC5 or uncompressed).
[[nodiscard]] TextureFormat compressed_format_for(const TextureSettings& settings, std::uint32_t width, std::uint32_t height) noexcept;
// Encodes RGBA8 mips as `target` (BC7 / BC5; others: returned as is). Slow (seconds for 2048x2048
// on all cores): load_texture caches the result.
[[nodiscard]] TextureData compress_texture(const TextureData& rgba8, TextureFormat target);
// Back to RGBA8 (tests, and GPUs without BC support). BC5 gets Z rebuilt in blue, alpha 255.
[[nodiscard]] TextureData decompress_texture(const TextureData& data);

// Decodes and processes an image file with these settings (no cache).
[[nodiscard]] Result<TextureData> import_texture(const std::filesystem::path& file, const TextureSettings& settings);

// The same from memory (images embedded in GLB files); `name` is for messages.
[[nodiscard]] Result<TextureData> import_texture_memory(std::span<const std::uint8_t> encoded, const TextureSettings& settings,
                                                        std::string_view name);

// Loads a texture for drawing: settings from its .noormeta (guessed if there is none), result
// cached in `cache_dir` keyed by content hash, settings and importer version. Deleting the
// cache is always safe. An empty `cache_dir` skips the cache.
[[nodiscard]] Result<TextureData> load_texture(const std::filesystem::path& file, const std::filesystem::path& cache_dir);

// The same for encoded bytes (an image inside a GLB file), cached by the bytes' hash.
[[nodiscard]] Result<TextureData> load_texture_memory(std::span<const std::uint8_t> encoded, const TextureSettings& settings,
                                                     std::string_view name, const std::filesystem::path& cache_dir);

// The settings a texture file has in its .noormeta, or the guess.
[[nodiscard]] TextureSettings texture_settings_for(const std::filesystem::path& file);

// What a texture becomes with these settings, from the image's header only (no decoding): for the
// editor's import settings.
struct TextureInfo {
    std::uint32_t source_width = 0, source_height = 0;
    std::uint32_t width = 0, height = 0;  // after max_size
    std::uint32_t mips = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::uint64_t gpu_bytes = 0;    // with this format
    std::uint64_t rgba8_bytes = 0;  // the same uncompressed (RGBA8, or RGBA16F for HDR)
};
[[nodiscard]] Result<TextureInfo> texture_info(const std::filesystem::path& file, const TextureSettings& settings);

// Writes new settings into the texture's .noormeta (its id and everything else are kept). Running
// games and the editor reload the texture when they see the file change.
[[nodiscard]] Status set_texture_settings(const std::filesystem::path& file, const TextureSettings& settings);

} // namespace noor::asset
