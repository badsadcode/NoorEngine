#pragma once
// Textures (ADR-029): decoding image files, their import settings, mipmaps and the derived-data
// cache. The renderer gets ready-to-upload mip chains; nothing here touches the GPU.
//
// Settings live in the texture's .noormeta ("settings"), guessed from the file name the first
// time the registry sees the file and editable afterwards:
//   { "kind": "color" | "data" | "normal" | "hdr", "normal_convention": "opengl" | "directx",
//     "mips": true, "max_size": 4096 }
//   color   base colour, emissive: sRGB, stored sRGB (decoded to linear when sampled)
//   data    roughness, metallic, AO, masks, height: linear, stored as is
//   normal  normal maps: linear; "directx" ones have green flipped on import (glTF uses OpenGL's)
//   hdr     .hdr light values (.exr comes with environments, part 5): 16-bit float
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
inline constexpr int kTextureImporterVersion = 1;

enum class TextureKind : std::uint8_t { Color, Data, Normal, Hdr };

struct TextureSettings {
    TextureKind kind = TextureKind::Color;
    bool directx_normals = false;  // flip green on import
    bool mips = true;
    std::uint32_t max_size = 4096;  // larger images are halved until they fit

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

enum class TextureFormat : std::uint8_t { Rgba8Srgb, Rgba8Unorm, Rgba16Float };
[[nodiscard]] std::uint32_t bytes_per_pixel(TextureFormat format) noexcept;

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

} // namespace noor::asset
