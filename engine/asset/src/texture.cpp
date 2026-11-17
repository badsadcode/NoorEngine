// Textures (ADR-029): decoding with stb_image, linear-light processing (resize, mipmaps,
// normal renormalisation, DirectX green flip), encoding to the GPU format, and the cache.
#include "noor/asset/texture.h"

#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/foundation/sha256.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <optional>

namespace noor::asset {
namespace stdfs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linear_to_srgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// IEEE 754 binary16 from a float (round to nearest even; overflow -> infinity, NaN kept).
std::uint16_t to_half(float value) {
    const std::uint32_t f = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (f >> 16) & 0x8000u;
    std::uint32_t mantissa = f & 0x007FFFFFu;
    const int exponent = static_cast<int>((f >> 23) & 0xFF) - 127 + 15;
    if (((f >> 23) & 0xFF) == 0xFF) return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        mantissa |= 0x00800000u;
        const int shift = 14 - exponent;
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t rest = mantissa & ((1u << shift) - 1);
        const std::uint32_t halfway = 1u << (shift - 1);
        if (rest > halfway || (rest == halfway && (half & 1u))) ++half;
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
    const std::uint32_t rest = mantissa & 0x1FFFu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u))) ++half;
    return static_cast<std::uint16_t>(half);
}

// Working image: linear float RGBA.
struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgba;
    float* at(std::uint32_t x, std::uint32_t y) { return &rgba[(static_cast<std::size_t>(y) * width + x) * 4]; }
    const float* at(std::uint32_t x, std::uint32_t y) const { return &rgba[(static_cast<std::size_t>(y) * width + x) * 4]; }
};

// Half size (rounding up; an odd last row/column is folded in), averaging in linear light.
Image half_size(const Image& in) {
    Image out;
    out.width = std::max(1u, in.width / 2);
    out.height = std::max(1u, in.height / 2);
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0.0f);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        const std::uint32_t y0 = std::min(y * 2, in.height - 1);
        const std::uint32_t y1 = (y == out.height - 1) ? in.height : std::min(y * 2 + 2, in.height);
        for (std::uint32_t x = 0; x < out.width; ++x) {
            const std::uint32_t x0 = std::min(x * 2, in.width - 1);
            const std::uint32_t x1 = (x == out.width - 1) ? in.width : std::min(x * 2 + 2, in.width);
            float sum[4] = {0, 0, 0, 0};
            std::uint32_t n = 0;
            for (std::uint32_t sy = y0; sy < std::max(y1, y0 + 1); ++sy)
                for (std::uint32_t sx = x0; sx < std::max(x1, x0 + 1); ++sx, ++n)
                    for (int c = 0; c < 4; ++c) sum[c] += in.at(sx, sy)[c];
            for (int c = 0; c < 4; ++c) out.at(x, y)[c] = sum[c] / static_cast<float>(n);
        }
    }
    return out;
}

// Normal maps: back to unit length after averaging (stored 0..1, meaning -1..1).
void renormalize(Image& img) {
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        float n[3] = {img.rgba[i] * 2 - 1, img.rgba[i + 1] * 2 - 1, img.rgba[i + 2] * 2 - 1};
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-6f) {
            n[0] = 0, n[1] = 0, n[2] = 1;
        } else {
            for (float& c : n) c /= len;
        }
        for (int c = 0; c < 3; ++c) img.rgba[i + c] = n[c] * 0.5f + 0.5f;
    }
}

TextureMip encode(const Image& img, TextureFormat format) {
    TextureMip mip{img.width, img.height, {}};
    const std::size_t pixels = static_cast<std::size_t>(img.width) * img.height;
    if (format == TextureFormat::Rgba16Float) {
        mip.bytes.resize(pixels * 8);
        auto* out = reinterpret_cast<std::uint16_t*>(mip.bytes.data());
        for (std::size_t i = 0; i < pixels * 4; ++i) out[i] = to_half(img.rgba[i]);
        return mip;
    }
    mip.bytes.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels * 4; ++i) {
        float v = img.rgba[i];
        if (format == TextureFormat::Rgba8Srgb && (i % 4) != 3) v = linear_to_srgb(v);  // alpha stays linear
        mip.bytes[i] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    }
    return mip;
}

Result<TextureData> process(Image img, const TextureSettings& s, std::string_view name) {
    TextureData out;
    out.source_width = img.width;
    out.source_height = img.height;
    out.format = s.kind == TextureKind::Hdr     ? TextureFormat::Rgba16Float
                 : s.kind == TextureKind::Color ? TextureFormat::Rgba8Srgb
                                                : TextureFormat::Rgba8Unorm;
    if (s.kind == TextureKind::Normal && s.directx_normals) {
        for (std::size_t i = 1; i < img.rgba.size(); i += 4) img.rgba[i] = 1.0f - img.rgba[i];
    }
    const std::uint32_t max_size = std::max(1u, s.max_size);
    while (img.width > max_size || img.height > max_size) {
        img = half_size(img);
        if (s.kind == TextureKind::Normal) renormalize(img);
    }
    if (img.width != out.source_width) {
        NOOR_LOG_INFO("Asset", "Texture {}: {}x{} reduced to {}x{} (max_size {})", name, out.source_width, out.source_height,
                      img.width, img.height, max_size);
    }
    out.mips.push_back(encode(img, out.format));
    if (s.mips) {
        while (img.width > 1 || img.height > 1) {
            img = half_size(img);
            if (s.kind == TextureKind::Normal) renormalize(img);
            out.mips.push_back(encode(img, out.format));
        }
    }
    return out;
}

// Decodes to linear float RGBA, by kind: colour is sRGB-decoded, everything else taken as is.
Result<Image> decode(const stbi_uc* data, int size, const TextureSettings& s, std::string_view name) {
    int w = 0, h = 0, channels = 0;
    Image img;
    if (stbi_is_hdr_from_memory(data, size)) {
        float* f = stbi_loadf_from_memory(data, size, &w, &h, &channels, 4);
        if (!f) return fail(ErrorCode::ParseError, std::format("texture {}: {}", name, stbi_failure_reason()));
        img.rgba.assign(f, f + static_cast<std::size_t>(w) * h * 4);
        stbi_image_free(f);
    } else if (stbi_is_16_bit_from_memory(data, size)) {
        stbi_us* p = stbi_load_16_from_memory(data, size, &w, &h, &channels, 4);
        if (!p) return fail(ErrorCode::ParseError, std::format("texture {}: {}", name, stbi_failure_reason()));
        img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < img.rgba.size(); ++i) img.rgba[i] = static_cast<float>(p[i]) / 65535.0f;
        stbi_image_free(p);
    } else {
        stbi_uc* p = stbi_load_from_memory(data, size, &w, &h, &channels, 4);
        if (!p) return fail(ErrorCode::ParseError, std::format("texture {}: {}", name, stbi_failure_reason()));
        img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < img.rgba.size(); ++i) img.rgba[i] = static_cast<float>(p[i]) / 255.0f;
        stbi_image_free(p);
        if (s.kind == TextureKind::Hdr)
            for (std::size_t i = 0; i < img.rgba.size(); ++i)
                if (i % 4 != 3) img.rgba[i] = srgb_to_linear(img.rgba[i]);  // an 8-bit picture used as light
    }
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768)
        return fail(ErrorCode::ParseError, std::format("texture {}: unsupported size {}x{}", name, w, h));
    img.width = static_cast<std::uint32_t>(w);
    img.height = static_cast<std::uint32_t>(h);
    if (s.kind == TextureKind::Color)
        for (std::size_t i = 0; i < img.rgba.size(); ++i)
            if (i % 4 != 3) img.rgba[i] = srgb_to_linear(img.rgba[i]);
    return img;
}

// ---- cache file: "NTEX", version, format, mip count, then per mip: width, height, size, bytes ----

constexpr std::uint32_t kCacheMagic = 0x5845544Eu;  // "NTEX"

std::string serialize(const TextureData& t) {
    std::string out;
    auto put = [&](std::uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    put(kCacheMagic);
    put(static_cast<std::uint32_t>(kTextureImporterVersion));
    put(static_cast<std::uint32_t>(t.format));
    put(t.source_width);
    put(t.source_height);
    put(static_cast<std::uint32_t>(t.mips.size()));
    for (const auto& m : t.mips) {
        put(m.width);
        put(m.height);
        put(static_cast<std::uint32_t>(m.bytes.size()));
        out.append(reinterpret_cast<const char*>(m.bytes.data()), m.bytes.size());
    }
    return out;
}

std::optional<TextureData> deserialize(const std::string& in) {
    std::size_t pos = 0;
    auto get = [&](std::uint32_t& v) {
        if (pos + 4 > in.size()) return false;
        std::memcpy(&v, in.data() + pos, 4);
        pos += 4;
        return true;
    };
    std::uint32_t magic = 0, version = 0, format = 0, count = 0;
    TextureData t;
    if (!get(magic) || magic != kCacheMagic || !get(version) || version != static_cast<std::uint32_t>(kTextureImporterVersion) ||
        !get(format) || format > 2 || !get(t.source_width) || !get(t.source_height) || !get(count) || count == 0 || count > 20)
        return std::nullopt;
    t.format = static_cast<TextureFormat>(format);
    for (std::uint32_t i = 0; i < count; ++i) {
        TextureMip m;
        std::uint32_t size = 0;
        if (!get(m.width) || !get(m.height) || !get(size) || pos + size > in.size()) return std::nullopt;
        if (static_cast<std::uint64_t>(m.width) * m.height * bytes_per_pixel(t.format) != size) return std::nullopt;
        m.bytes.assign(in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
        t.mips.push_back(std::move(m));
    }
    return t;
}

} // namespace

std::string_view kind_name(TextureKind kind) noexcept {
    switch (kind) {
    case TextureKind::Color: return "color";
    case TextureKind::Data: return "data";
    case TextureKind::Normal: return "normal";
    case TextureKind::Hdr: return "hdr";
    }
    return "color";
}

Json TextureSettings::to_json() const {
    return Json{{"kind", std::string(kind_name(kind))},
                {"normal_convention", directx_normals ? "directx" : "opengl"},
                {"mips", mips},
                {"max_size", max_size}};
}

TextureSettings TextureSettings::from_json(const Json& j) {
    TextureSettings s;
    if (!j.is_object()) return s;
    const std::string kind = j.value("kind", std::string("color"));
    s.kind = kind == "data" ? TextureKind::Data : kind == "normal" ? TextureKind::Normal : kind == "hdr" ? TextureKind::Hdr : TextureKind::Color;
    s.directx_normals = j.value("normal_convention", std::string("opengl")) == "directx";
    s.mips = j.value("mips", true);
    if (j.contains("max_size") && j["max_size"].is_number_unsigned()) s.max_size = std::clamp(j["max_size"].get<std::uint32_t>(), 1u, 16384u);
    return s;
}

bool is_texture_file(const stdfs::path& path) {
    const std::string ext = lower(path.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr";
}

TextureSettings guess_texture_settings(const stdfs::path& path) {
    TextureSettings s;
    const std::string name = lower(path.stem().string());
    const std::string ext = lower(path.extension().string());
    // Words split on _ - . and space, so "brick_normal_dx" -> brick, normal, dx.
    std::vector<std::string> words;
    std::string word;
    for (char c : name + "_") {
        if (c == '_' || c == '-' || c == '.' || c == ' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    auto has = [&](std::initializer_list<std::string_view> any) {
        return std::ranges::any_of(words, [&](const std::string& w) { return std::ranges::find(any, w) != any.end(); });
    };
    if (ext == ".hdr") {
        s.kind = TextureKind::Hdr;
        s.max_size = 8192;
    } else if (has({"normal", "normals", "nrm", "norm", "nor", "normalgl", "normaldx"})) {
        s.kind = TextureKind::Normal;
        s.directx_normals = has({"dx", "directx", "normaldx"});
    } else if (has({"roughness", "rough", "metallic", "metalness", "metal", "ao", "occlusion", "ambientocclusion", "height",
                    "displacement", "disp", "depth", "mask", "orm", "arm", "rma", "specular", "gloss", "glossiness", "bump", "cavity",
                    "curvature", "translucency", "opacity", "alpha", "wind"})) {
        s.kind = TextureKind::Data;
    }
    return s;
}

std::uint32_t bytes_per_pixel(TextureFormat format) noexcept { return format == TextureFormat::Rgba16Float ? 8u : 4u; }

std::uint64_t TextureData::gpu_bytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& m : mips) total += m.bytes.size();
    return total;
}

Result<TextureData> import_texture_memory(std::span<const std::uint8_t> encoded, const TextureSettings& settings, std::string_view name) {
    if (encoded.empty() || encoded.size() > (1u << 30)) return fail(ErrorCode::ParseError, std::format("texture {}: empty or too large", name));
    auto img = decode(encoded.data(), static_cast<int>(encoded.size()), settings, name);
    if (!img) return std::unexpected(img.error());
    return process(std::move(*img), settings, name);
}

Result<TextureData> import_texture(const stdfs::path& file, const TextureSettings& settings) {
    auto data = fs::read_file(file, 1ull << 30);
    if (!data) return std::unexpected(data.error());
    return import_texture_memory({reinterpret_cast<const std::uint8_t*>(data->data()), data->size()}, settings, file.filename().string());
}

TextureSettings texture_settings_for(const stdfs::path& file) {
    const stdfs::path meta = file.string() + ".noormeta";
    if (auto text = fs::read_file(meta, 1 << 20)) {
        if (auto json = parse_json(*text, meta.generic_string()); json && json->contains("settings") && (*json)["settings"].contains("kind"))
            return TextureSettings::from_json((*json)["settings"]);
    }
    return guess_texture_settings(file);
}

Result<TextureData> load_texture(const stdfs::path& file, const stdfs::path& cache_dir) {
    const TextureSettings settings = texture_settings_for(file);
    auto data = fs::read_file(file, 1ull << 30);
    if (!data) return std::unexpected(data.error());
    stdfs::path cached;
    if (!cache_dir.empty()) {
        const std::string key = Sha256::hex(*data) + settings.to_json().dump() + std::to_string(kTextureImporterVersion);
        cached = cache_dir / (Sha256::hex(key).substr(0, 32) + ".ntex");
        if (auto bytes = fs::read_file(cached, 2ull << 30)) {
            if (auto t = deserialize(*bytes)) return std::move(*t);
        }
    }
    auto result = import_texture_memory({reinterpret_cast<const std::uint8_t*>(data->data()), data->size()}, settings,
                                        file.filename().string());
    if (!result) return result;
    if (!cached.empty()) {
        std::error_code ec;
        stdfs::create_directories(cache_dir, ec);
        if (auto w = fs::write_file_atomic(cached, serialize(*result), false); !w)
            NOOR_LOG_WARN("Asset", "Texture cache not written ({}): {}", cached.generic_string(), w.error().describe());
    }
    return result;
}

} // namespace noor::asset
