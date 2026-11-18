#include "noor/terrain/terrain.h"

#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>  // the implementation lives in noor_asset (texture.cpp)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <map>
#include <mutex>

namespace noor::terrain {
namespace stdfs = std::filesystem;
using asset::Json;

namespace {

float number(const Json& j, const char* key, float fallback) { return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback; }

// SplitMix64: a well-mixed integer hash (integer maths only: the same on every machine).
std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// One of 8 gradient directions for lattice point (ix, iz), as exact constants.
void gradient(std::int64_t ix, std::int64_t iz, std::uint64_t seed, double& gx, double& gz) {
    static constexpr double kD = 0.70710678118654752;
    static constexpr double kG[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {kD, kD}, {-kD, kD}, {kD, -kD}, {-kD, -kD}};
    const std::uint64_t h = mix(seed ^ mix(static_cast<std::uint64_t>(ix) * 0x632BE59BD9B4E019ull ^ static_cast<std::uint64_t>(iz)));
    const auto& g = kG[h & 7];
    gx = g[0];
    gz = g[1];
}

double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }  // quintic, a polynomial

// 2D gradient noise in about -1..1.
double gradient_noise(double x, double z, std::uint64_t seed) {
    const double fx = std::floor(x), fz = std::floor(z);
    const auto ix = static_cast<std::int64_t>(fx), iz = static_cast<std::int64_t>(fz);
    const double dx = x - fx, dz = z - fz;
    double v[4];
    for (int c = 0; c < 4; ++c) {
        const int ox = c & 1, oz = c >> 1;
        double gx, gz;
        gradient(ix + ox, iz + oz, seed, gx, gz);
        v[c] = gx * (dx - ox) + gz * (dz - oz);
    }
    const double u = fade(dx), w = fade(dz);
    const double a = v[0] + (v[1] - v[0]) * u, b = v[2] + (v[3] - v[2]) * u;
    return (a + (b - a) * w) * 1.4142135623730951;  // the 2D range is +-sqrt(0.5): stretch to about +-1
}

} // namespace

float fractal_noise(double x, double z, std::uint64_t seed, std::uint32_t octaves) {
    double sum = 0.0, amplitude = 1.0, total = 0.0, frequency = 1.0;
    for (std::uint32_t o = 0; o < std::max(1u, octaves); ++o) {
        sum += gradient_noise(x * frequency, z * frequency, mix(seed + o)) * amplitude;
        total += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return static_cast<float>(std::clamp(sum / total, -1.0, 1.0));
}

Settings Settings::from_json(const Json& c, const asset::AssetFileResolver& resolve) {
    Settings s;
    if (!c.is_object()) return s;
    if (c.contains("tiles") && c["tiles"].is_array() && c["tiles"].size() == 2 && c["tiles"][0].is_number() && c["tiles"][1].is_number()) {
        s.tiles_x = std::clamp(c["tiles"][0].get<std::uint32_t>(), 1u, 16u);
        s.tiles_z = std::clamp(c["tiles"][1].get<std::uint32_t>(), 1u, 16u);
    }
    s.cell_size = std::clamp(number(c, "cell_size", 1.0f), 0.05f, 64.0f);
    const std::string base = c.value("base", std::string("noise"));
    s.base = base == "flat" ? Base::Flat : base == "heightmap" ? Base::Heightmap : Base::Noise;
    if (c.contains("seed") && c["seed"].is_number_unsigned()) s.seed = c["seed"].get<std::uint64_t>();
    else if (c.contains("seed") && c["seed"].is_number_integer()) s.seed = static_cast<std::uint64_t>(c["seed"].get<std::int64_t>());
    s.height = std::clamp(number(c, "height", 40.0f), 0.0f, 10000.0f);
    s.scale = std::clamp(number(c, "scale", 250.0f), 1.0f, 100000.0f);
    s.octaves = static_cast<std::uint32_t>(std::clamp(number(c, "octaves", 5.0f), 1.0f, 12.0f));
    s.min_height = number(c, "min_height", 0.0f);
    s.max_height = number(c, "max_height", 50.0f);
    s.flat_height = number(c, "flat_height", 0.0f);
    if (const auto id = asset::guid_field(c, "heightmap"); id && resolve)
        if (auto file = resolve(*id)) s.heightmap = *file;
    return s;
}

std::string Settings::key() const {
    std::error_code ec;
    const auto time = heightmap.empty() ? stdfs::file_time_type{} : stdfs::last_write_time(heightmap, ec);
    return std::format("t1 {} {} {} {} {} {} {} {} {} {} {} {} {} {}", tiles_x, tiles_z, cell_size, static_cast<int>(base), seed, height, scale,
                       octaves, heightmap.generic_string(), time.time_since_epoch().count(), min_height, max_height, flat_height, 0);
}

Result<Heights> Heights::build(const Settings& s) {
    Heights out;
    out.sx_ = s.tiles_x * kTileCells + 1;
    out.sz_ = s.tiles_z * kTileCells + 1;
    out.cell_ = s.cell_size;
    out.h_.assign(static_cast<std::size_t>(out.sx_) * out.sz_, s.flat_height);
    if (s.base == Settings::Base::Noise) {
        // Noise in metres-from-the-corner / scale; heights from 0 to `height`.
        for (std::uint32_t z = 0; z < out.sz_; ++z)
            for (std::uint32_t x = 0; x < out.sx_; ++x) {
                const double px = static_cast<double>(x) * s.cell_size / s.scale, pz = static_cast<double>(z) * s.cell_size / s.scale;
                out.h_[static_cast<std::size_t>(z) * out.sx_ + x] = (fractal_noise(px, pz, s.seed, s.octaves) * 0.5f + 0.5f) * s.height;
            }
    } else if (s.base == Settings::Base::Heightmap) {
        if (s.heightmap.empty()) return fail(ErrorCode::NotFound, "terrain: the heightmap asset isn't set or can't be found");
        auto bytes = fs::read_file(s.heightmap, 256ull << 20);
        if (!bytes) return std::unexpected(bytes.error());
        int w = 0, h = 0, channels = 0;
        stbi_us* p = stbi_load_16_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()), static_cast<int>(bytes->size()), &w, &h, &channels, 1);
        if (!p) return fail(ErrorCode::ParseError, std::format("terrain heightmap {}: {}", s.heightmap.filename().string(), stbi_failure_reason()));
        // The picture covers the whole terrain, corner to corner: bilinear resampling.
        for (std::uint32_t z = 0; z < out.sz_; ++z)
            for (std::uint32_t x = 0; x < out.sx_; ++x) {
                const double u = static_cast<double>(x) / (out.sx_ - 1) * (w - 1), v = static_cast<double>(z) / (out.sz_ - 1) * (h - 1);
                const int x0 = static_cast<int>(std::floor(u)), z0 = static_cast<int>(std::floor(v));
                const int x1 = std::min(x0 + 1, w - 1), z1 = std::min(z0 + 1, h - 1);
                const double fx = u - x0, fz = v - z0;
                const auto px = [&](int a, int b) { return p[static_cast<std::size_t>(b) * w + a] / 65535.0; };
                const double top = px(x0, z0) + (px(x1, z0) - px(x0, z0)) * fx, bottom = px(x0, z1) + (px(x1, z1) - px(x0, z1)) * fx;
                out.h_[static_cast<std::size_t>(z) * out.sx_ + x] = static_cast<float>(s.min_height + (top + (bottom - top) * fz) * (s.max_height - s.min_height));
            }
        stbi_image_free(p);
    }
    return out;
}

Result<std::shared_ptr<const Heights>> Heights::shared(const Settings& s) {
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<const Heights>> cache;
    const std::string key = s.key();
    {
        std::scoped_lock lock(mutex);
        if (auto it = cache.find(key); it != cache.end())
            if (auto live = it->second.lock()) return live;
    }
    auto built = build(s);
    if (!built) return std::unexpected(built.error());
    auto ptr = std::make_shared<const Heights>(std::move(*built));
    std::scoped_lock lock(mutex);
    cache[key] = ptr;
    return ptr;
}

float Heights::height(float x, float z) const noexcept {
    const math::Vec2 o = origin();
    const float u = std::clamp((x - o.x) / cell_, 0.0f, static_cast<float>(sx_ - 1));
    const float v = std::clamp((z - o.y) / cell_, 0.0f, static_cast<float>(sz_ - 1));
    const auto x0 = std::min(static_cast<std::uint32_t>(u), sx_ - 2), z0 = std::min(static_cast<std::uint32_t>(v), sz_ - 2);
    const float fx = u - static_cast<float>(x0), fz = v - static_cast<float>(z0);
    // The same split as the mesh and Jolt's height fields: two triangles per cell, the diagonal from
    // (x0, z0) to (x0+1, z0+1).
    const float h00 = at(x0, z0), h10 = at(x0 + 1, z0), h01 = at(x0, z0 + 1), h11 = at(x0 + 1, z0 + 1);
    if (fz >= fx) return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
    return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
}

math::Vec3 Heights::normal(std::uint32_t x, std::uint32_t z) const noexcept {
    const std::uint32_t xl = x > 0 ? x - 1 : x, xr = x + 1 < sx_ ? x + 1 : x;
    const std::uint32_t zl = z > 0 ? z - 1 : z, zr = z + 1 < sz_ ? z + 1 : z;
    const float dx = (at(xr, z) - at(xl, z)) / (static_cast<float>(xr - xl) * cell_);
    const float dz = (at(x, zr) - at(x, zl)) / (static_cast<float>(zr - zl) * cell_);
    return math::normalize(math::Vec3{-dx, 1.0f, -dz});
}

std::vector<float> Heights::tile(std::uint32_t tx, std::uint32_t tz) const {
    std::vector<float> out(static_cast<std::size_t>(kTileSamples) * kTileSamples);
    for (std::uint32_t z = 0; z < kTileSamples; ++z)
        for (std::uint32_t x = 0; x < kTileSamples; ++x) out[static_cast<std::size_t>(z) * kTileSamples + x] = at(tx * kTileCells + x, tz * kTileCells + z);
    return out;
}

std::uint64_t Heights::hash() const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (const float v : h_) {
        h ^= std::bit_cast<std::uint32_t>(v);
        h *= 1099511628211ull;
    }
    return h;
}

TileMesh tile_mesh(const Heights& heights, std::uint32_t tx, std::uint32_t tz) {
    TileMesh m;
    m.lo = {1e30f, 1e30f, 1e30f};
    m.hi = {-1e30f, -1e30f, -1e30f};
    m.vertices.reserve(static_cast<std::size_t>(kTileSamples) * kTileSamples);
    const math::Vec2 o = heights.origin();
    const float cell = heights.cell_size();
    for (std::uint32_t z = 0; z < kTileSamples; ++z)
        for (std::uint32_t x = 0; x < kTileSamples; ++x) {
            const std::uint32_t gx = tx * kTileCells + x, gz = tz * kTileCells + z;
            const math::Vec3 p{o.x + static_cast<float>(gx) * cell, heights.at(gx, gz), o.y + static_cast<float>(gz) * cell};
            const math::Vec3 n = heights.normal(gx, gz);
            // Tangent along +x (the direction of +u), on the surface; bitangent along +z (+v).
            math::Vec3 t = math::normalize(math::Vec3{1, 0, 0} - n * n.x);
            const float w = math::dot(math::cross(n, t), math::Vec3{0, 0, 1}) < 0.0f ? -1.0f : 1.0f;
            m.vertices.push_back({p, n, {1, 1, 1}, {p.x, p.z}, {t.x, t.y, t.z, w}});
            m.lo = {std::min(m.lo.x, p.x), std::min(m.lo.y, p.y), std::min(m.lo.z, p.z)};
            m.hi = {std::max(m.hi.x, p.x), std::max(m.hi.y, p.y), std::max(m.hi.z, p.z)};
        }
    m.indices.reserve(static_cast<std::size_t>(kTileCells) * kTileCells * 6);
    for (std::uint32_t z = 0; z < kTileCells; ++z)
        for (std::uint32_t x = 0; x < kTileCells; ++x) {
            const std::uint32_t a = z * kTileSamples + x, b = a + 1, c = a + kTileSamples, d = c + 1;
            // Counter-clockwise seen from above; the diagonal runs from a to d, like Jolt's height fields
            // (and Heights::height), so what you see is what you walk on.
            m.indices.insert(m.indices.end(), {a, c, d, a, d, b});
        }
    return m;
}

} // namespace noor::terrain
