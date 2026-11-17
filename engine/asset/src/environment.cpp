#include "noor/asset/environment.h"

#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/foundation/sha256.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>  // the implementation is in texture.cpp
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>

namespace noor::asset {
namespace stdfs = std::filesystem;
using math::Vec2;
using math::Vec3;

namespace {

constexpr float kPi = math::kPi;

std::uint16_t to_half(float value) {
    // Same rounding as texture.cpp; values here are finite and non-negative.
    const std::uint32_t f = std::bit_cast<std::uint32_t>(std::clamp(value, 0.0f, 65000.0f));
    const int exponent = static_cast<int>((f >> 23) & 0xFF) - 127 + 15;
    std::uint32_t mantissa = f & 0x007FFFFFu;
    if (exponent <= 0) {
        if (exponent < -10) return 0;
        mantissa |= 0x00800000u;
        const int shift = 14 - exponent;
        return static_cast<std::uint16_t>((mantissa + (1u << (shift - 1))) >> shift);
    }
    std::uint32_t half = (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
    if ((mantissa & 0x1FFFu) >= 0x1000u) ++half;
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(half, 0x7BFFu));
}

// A pyramid of the equirect image (each level half the size) for filtered lookups.
struct Pyramid {
    std::vector<EquirectImage> levels;
    explicit Pyramid(const EquirectImage& base) {
        levels.push_back(base);
        while (levels.back().width > 8 && levels.back().height > 4) {
            const auto& in = levels.back();
            EquirectImage out{in.width / 2, in.height / 2, {}};
            out.rgb.resize(static_cast<std::size_t>(out.width) * out.height * 3);
            for (std::uint32_t y = 0; y < out.height; ++y)
                for (std::uint32_t x = 0; x < out.width; ++x)
                    for (int c = 0; c < 3; ++c) {
                        float s = 0;
                        for (std::uint32_t dy = 0; dy < 2; ++dy)
                            for (std::uint32_t dx = 0; dx < 2; ++dx)
                                s += in.rgb[((static_cast<std::size_t>(y) * 2 + dy) * in.width + x * 2 + dx) * 3 + c];
                        out.rgb[(static_cast<std::size_t>(y) * out.width + x) * 3 + c] = s * 0.25f;
                    }
            levels.push_back(std::move(out));
        }
    }
    // Bilinear sample at a direction from level `lod` (fractional: blended between levels).
    Vec3 sample(Vec3 dir, float lod) const {
        lod = std::clamp(lod, 0.0f, static_cast<float>(levels.size() - 1));
        const auto l0 = static_cast<std::size_t>(lod);
        const std::size_t l1 = std::min(l0 + 1, levels.size() - 1);
        const float t = lod - static_cast<float>(l0);
        const Vec3 a = bilinear(levels[l0], dir);
        return t > 0.0f ? a * (1.0f - t) + bilinear(levels[l1], dir) * t : a;
    }
    static Vec3 bilinear(const EquirectImage& img, Vec3 dir) {
        const Vec2 uv = equirect_uv(dir);
        const float fx = uv.x * static_cast<float>(img.width) - 0.5f;
        const float fy = std::clamp(uv.y * static_cast<float>(img.height) - 0.5f, 0.0f, static_cast<float>(img.height - 1));
        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
        const auto px = [&](int x, int y) {
            x = ((x % static_cast<int>(img.width)) + static_cast<int>(img.width)) % static_cast<int>(img.width);  // wraps around
            y = std::clamp(y, 0, static_cast<int>(img.height) - 1);
            const float* p = &img.rgb[(static_cast<std::size_t>(y) * img.width + static_cast<std::size_t>(x)) * 3];
            return Vec3{p[0], p[1], p[2]};
        };
        const Vec3 top = px(x0, y0) * (1 - tx) + px(x0 + 1, y0) * tx;
        const Vec3 bottom = px(x0, y0 + 1) * (1 - tx) + px(x0 + 1, y0 + 1) * tx;
        return top * (1 - ty) + bottom * ty;
    }
};

// Hammersley point i of n (low-discrepancy, deterministic).
Vec2 hammersley(std::uint32_t i, std::uint32_t n) {
    std::uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return {static_cast<float>(i) / static_cast<float>(n), static_cast<float>(bits) * 2.3283064365386963e-10f};
}

// Prefilters one direction for roughness `r` (GGX, "split sum": N = V = R), with filtered importance
// sampling: each sample reads a blurrier pyramid level when its probability is low (Křivánek 2008).
Vec3 prefilter(const Pyramid& pyr, Vec3 n, float roughness, std::uint32_t samples) {
    const float alpha = roughness * roughness;
    const Vec3 up = std::abs(n.y) < 0.999f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 tx = math::normalize(math::cross(up, n));
    const Vec3 ty = math::cross(n, tx);
    const auto& base = pyr.levels[0];
    const float texel_solid_angle = 4.0f * kPi / static_cast<float>(base.width * base.height);
    Vec3 sum{0, 0, 0};
    float weight = 0.0f;
    for (std::uint32_t i = 0; i < samples; ++i) {
        const Vec2 xi = hammersley(i, samples);
        const float phi = 2.0f * kPi * xi.x;
        const float cos_theta = std::sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
        const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
        const Vec3 h = tx * (sin_theta * std::cos(phi)) + ty * (sin_theta * std::sin(phi)) + n * cos_theta;
        const Vec3 l = h * (2.0f * math::dot(n, h)) - n;
        const float n_dot_l = math::dot(n, l);
        if (n_dot_l <= 0.0f) continue;
        // pdf over directions for N = V: D(h) / 4 (the n·h terms cancel).
        const float a2 = alpha * alpha;
        const float f = cos_theta * cos_theta * (a2 - 1.0f) + 1.0f;
        const float d = a2 / (kPi * f * f);
        const float pdf = d / 4.0f;
        const float sample_solid_angle = 1.0f / (static_cast<float>(samples) * pdf + 1e-6f);
        const float lod = 0.5f * std::log2(sample_solid_angle / texel_solid_angle) + 1.0f;
        sum = sum + pyr.sample(l, lod) * n_dot_l;
        weight += n_dot_l;
    }
    return weight > 0.0f ? sum * (1.0f / weight) : pyr.sample(n, 0.0f);
}

constexpr std::uint32_t kCacheMagic = 0x564E454Eu;  // "NENV"

std::string serialize(const EnvironmentData& e) {
    std::string out;
    auto put = [&](const void* p, std::size_t n) { out.append(static_cast<const char*>(p), n); };
    const std::uint32_t head[4] = {kCacheMagic, static_cast<std::uint32_t>(kEnvironmentVersion), e.face_size,
                                   static_cast<std::uint32_t>(e.mips.size())};
    put(head, sizeof(head));
    put(e.irradiance_sh.data(), sizeof(e.irradiance_sh));
    for (const auto& mip : e.mips)
        for (const auto& face : mip) put(face.data(), face.size() * 2);
    const std::uint32_t bg[2] = {e.background.width, e.background.height};
    put(bg, sizeof(bg));
    put(e.background.rgb.data(), e.background.rgb.size() * 4);
    return out;
}

std::optional<EnvironmentData> deserialize(const std::string& in) {
    std::size_t pos = 0;
    auto get = [&](void* p, std::size_t n) {
        if (pos + n > in.size()) return false;
        std::memcpy(p, in.data() + pos, n);
        pos += n;
        return true;
    };
    std::uint32_t head[4];
    EnvironmentData e;
    if (!get(head, sizeof(head)) || head[0] != kCacheMagic || head[1] != static_cast<std::uint32_t>(kEnvironmentVersion) ||
        head[2] == 0 || head[2] > 4096 || head[3] == 0 || head[3] > 16)
        return std::nullopt;
    e.face_size = head[2];
    if (!get(e.irradiance_sh.data(), sizeof(e.irradiance_sh))) return std::nullopt;
    e.mips.resize(head[3]);
    for (std::uint32_t m = 0; m < head[3]; ++m) {
        const std::uint32_t s = std::max(1u, e.face_size >> m);
        for (auto& face : e.mips[m]) {
            face.resize(static_cast<std::size_t>(s) * s * 4);
            if (!get(face.data(), face.size() * 2)) return std::nullopt;
        }
    }
    std::uint32_t bg[2];
    if (!get(bg, sizeof(bg)) || bg[0] > 16384 || bg[1] > 8192) return std::nullopt;
    e.background.width = bg[0];
    e.background.height = bg[1];
    e.background.rgb.resize(static_cast<std::size_t>(bg[0]) * bg[1] * 3);
    if (!get(e.background.rgb.data(), e.background.rgb.size() * 4)) return std::nullopt;
    return e;
}

} // namespace

Vec3 equirect_direction(float u, float v) noexcept {
    const float phi = (u - 0.5f) * 2.0f * kPi;  // 0 at u = 0.5 (forward), positive towards +X
    const float theta = v * kPi;                 // 0 up, pi down
    return {std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi)};
}

Vec2 equirect_uv(Vec3 d) noexcept {
    const float phi = std::atan2(d.x, -d.z);
    const float theta = std::acos(std::clamp(d.y / std::max(math::length(d), 1e-12f), -1.0f, 1.0f));
    float u = phi / (2.0f * kPi) + 0.5f;
    u -= std::floor(u);
    return {u, theta / kPi};
}

Vec3 cube_direction(int face, std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept {
    const float sc = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
    const float tc = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
    Vec3 d{0, 0, 0};
    switch (face) {
    case 0: d = {1, -tc, -sc}; break;
    case 1: d = {-1, -tc, sc}; break;
    case 2: d = {sc, 1, tc}; break;
    case 3: d = {sc, -1, -tc}; break;
    case 4: d = {sc, -tc, 1}; break;
    default: d = {-sc, -tc, -1}; break;
    }
    return math::normalize(d);
}

Vec3 evaluate_sh(const std::array<Vec3, 9>& sh, Vec3 n) noexcept {
    const float y[9] = {0.282095f,
                        0.488603f * n.y,
                        0.488603f * n.z,
                        0.488603f * n.x,
                        1.092548f * n.x * n.y,
                        1.092548f * n.y * n.z,
                        0.315392f * (3.0f * n.z * n.z - 1.0f),
                        1.092548f * n.x * n.z,
                        0.546274f * (n.x * n.x - n.y * n.y)};
    Vec3 e{0, 0, 0};
    for (int i = 0; i < 9; ++i) e = e + sh[i] * y[i];
    return e;
}

EnvironmentData make_environment(const EquirectImage& image, std::uint32_t face_size) {
    EnvironmentData out;
    out.background = image;
    if (face_size == 0) face_size = std::clamp(std::bit_floor(std::max(1u, image.width / 4)), 32u, 256u);
    out.face_size = face_size;
    const Pyramid pyr(image);

    // Irradiance SH9: project the radiance (each pixel weighted by its solid angle), then convolve
    // with the clamped cosine (bands scaled by pi, 2pi/3, pi/4).
    std::array<Vec3, 9> radiance{};
    const auto& src = pyr.levels[std::min<std::size_t>(pyr.levels.size() - 1, pyr.levels[0].width > 256 ? 1 : 0)];
    for (std::uint32_t py = 0; py < src.height; ++py) {
        const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(src.height);
        const float solid_angle = (2.0f * kPi / static_cast<float>(src.width)) * (kPi / static_cast<float>(src.height)) * std::sin(v * kPi);
        for (std::uint32_t px = 0; px < src.width; ++px) {
            const Vec3 d = equirect_direction((static_cast<float>(px) + 0.5f) / static_cast<float>(src.width), v);
            const float* p = &src.rgb[(static_cast<std::size_t>(py) * src.width + px) * 3];
            const Vec3 l{p[0] * solid_angle, p[1] * solid_angle, p[2] * solid_angle};
            const float y[9] = {0.282095f,
                                0.488603f * d.y,
                                0.488603f * d.z,
                                0.488603f * d.x,
                                1.092548f * d.x * d.y,
                                1.092548f * d.y * d.z,
                                0.315392f * (3.0f * d.z * d.z - 1.0f),
                                1.092548f * d.x * d.z,
                                0.546274f * (d.x * d.x - d.y * d.y)};
            for (int i = 0; i < 9; ++i) radiance[i] = radiance[i] + l * y[i];
        }
    }
    const float band[9] = {kPi, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, kPi / 4.0f, kPi / 4.0f,
                           kPi / 4.0f, kPi / 4.0f, kPi / 4.0f};
    for (int i = 0; i < 9; ++i) out.irradiance_sh[i] = radiance[i] * band[i];

    // The prefiltered cube: mip 0 straight from the image (filtered to the face's texel size),
    // then GGX convolutions with growing roughness.
    const auto mip_count = static_cast<std::uint32_t>(std::bit_width(face_size));
    const float face_texel_solid_angle = 4.0f * kPi / (6.0f * static_cast<float>(face_size * face_size));
    const float base_texel_solid_angle = 4.0f * kPi / static_cast<float>(image.width * image.height);
    const float lod0 = std::max(0.0f, 0.5f * std::log2(face_texel_solid_angle / base_texel_solid_angle));
    out.mips.resize(mip_count);
    for (std::uint32_t m = 0; m < mip_count; ++m) {
        const std::uint32_t size = std::max(1u, face_size >> m);
        const float roughness = mip_count > 1 ? static_cast<float>(m) / static_cast<float>(mip_count - 1) : 0.0f;
        const std::uint32_t samples = m == 0 ? 1 : (m < 3 ? 48 : 64);
        for (int face = 0; face < 6; ++face) {
            auto& texels = out.mips[m][static_cast<std::size_t>(face)];
            texels.resize(static_cast<std::size_t>(size) * size * 4);
            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = 0; x < size; ++x) {
                    const Vec3 d = cube_direction(face, x, y, size);
                    const Vec3 c = m == 0 ? pyr.sample(d, lod0) : prefilter(pyr, d, roughness, samples);
                    std::uint16_t* t = &texels[(static_cast<std::size_t>(y) * size + x) * 4];
                    t[0] = to_half(c.x), t[1] = to_half(c.y), t[2] = to_half(c.z), t[3] = to_half(1.0f);
                }
        }
    }
    return out;
}

EquirectImage procedural_sky(Vec3 zenith, Vec3 horizon, Vec3 ground, std::uint32_t width, std::uint32_t height) {
    EquirectImage img{width, height, std::vector<float>(static_cast<std::size_t>(width) * height * 3)};
    for (std::uint32_t y = 0; y < height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const float up = std::cos(v * kPi);  // 1 straight up, 0 at the horizon, -1 down
        Vec3 c;
        if (up >= 0.0f) {
            const float t = std::pow(1.0f - up, 4.0f);  // the horizon glow stays low
            c = zenith * (1.0f - t) + horizon * t;
        } else {
            const float t = std::min(1.0f, -up * 8.0f);  // a quick fade from horizon to ground
            c = horizon * (1.0f - t) + ground * t;
        }
        for (std::uint32_t x = 0; x < width; ++x) {
            float* p = &img.rgb[(static_cast<std::size_t>(y) * width + x) * 3];
            p[0] = c.x, p[1] = c.y, p[2] = c.z;
        }
    }
    return img;
}

Result<EnvironmentData> load_environment(const stdfs::path& hdr_file, const stdfs::path& cache_dir) {
    auto data = fs::read_file(hdr_file, 1ull << 30);
    if (!data) return std::unexpected(data.error());
    stdfs::path cached;
    if (!cache_dir.empty()) {
        cached = cache_dir / (Sha256::hex(Sha256::hex(*data) + "env" + std::to_string(kEnvironmentVersion)).substr(0, 32) + ".nenv");
        if (auto bytes = fs::read_file(cached, 2ull << 30))
            if (auto e = deserialize(*bytes)) return std::move(*e);
    }
    int w = 0, h = 0, channels = 0;
    float* pixels = stbi_loadf_from_memory(reinterpret_cast<const stbi_uc*>(data->data()), static_cast<int>(data->size()), &w, &h, &channels, 3);
    if (!pixels) return fail(ErrorCode::ParseError, std::format("environment {}: {}", hdr_file.filename().string(), stbi_failure_reason()));
    if (w < 8 || h < 4 || w > 16384 || h > 8192) {
        stbi_image_free(pixels);
        return fail(ErrorCode::ParseError, std::format("environment {}: unsupported size {}x{}", hdr_file.filename().string(), w, h));
    }
    EquirectImage image{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), {}};
    image.rgb.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 3);
    stbi_image_free(pixels);
    for (float& v : image.rgb) v = std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;  // broken files must not poison the filter
    const auto start = std::chrono::steady_clock::now();
    auto env = make_environment(image);
    NOOR_LOG_INFO("Asset", "Environment {}: {}x{} -> {} cube faces, {} mips, prefiltered in {:.0f} ms", hdr_file.filename().string(), w, h,
                  env.face_size, env.mips.size(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    if (!cached.empty()) {
        std::error_code ec;
        stdfs::create_directories(cache_dir, ec);
        if (auto s = fs::write_file_atomic(cached, serialize(env), false); !s)
            NOOR_LOG_WARN("Asset", "Environment cache not written: {}", s.error().describe());
    }
    return env;
}

} // namespace noor::asset
