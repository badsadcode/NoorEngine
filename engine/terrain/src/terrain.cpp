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
    if (const auto id = asset::guid_field(c, "sculpt"); id && resolve)
        if (auto file = resolve(*id)) s.sculpt = *file;
    if (const auto id = asset::guid_field(c, "paint"); id && resolve)
        if (auto file = resolve(*id)) s.paint = *file;
    return s;
}

std::string Settings::key() const {
    std::error_code ec;
    const auto time = heightmap.empty() ? stdfs::file_time_type{} : stdfs::last_write_time(heightmap, ec);
    const auto sculpt_time = sculpt.empty() ? stdfs::file_time_type{} : stdfs::last_write_time(sculpt, ec);
    const auto paint_time = paint.empty() ? stdfs::file_time_type{} : stdfs::last_write_time(paint, ec);
    return std::format("t1 {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}", tiles_x, tiles_z, cell_size, static_cast<int>(base), seed, height,
                       scale, octaves, heightmap.generic_string(), time.time_since_epoch().count(), min_height, max_height, flat_height, 0,
                       sculpt.generic_string(), sculpt_time.time_since_epoch().count(), paint.generic_string(), paint_time.time_since_epoch().count());
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
    if (!s.sculpt.empty()) {
        auto layer = SculptLayer::load(s.sculpt);
        if (!layer) return std::unexpected(layer.error());
        layer->resize(s.tiles_x, s.tiles_z);  // a terrain that changed size keeps the tiles it still has
        out = compose(out, *layer);
    }
    if (!s.paint.empty()) {
        auto paint = PaintLayer::load(s.paint);
        if (!paint) return std::unexpected(paint.error());
        paint->resize(s.tiles_x, s.tiles_z);
        out.repaint(*paint);
    }
    return out;
}

Heights Heights::compose(const Heights& base, const SculptLayer& layer) {
    Heights out = base;
    if (!layer.empty()) out.recompose(base, layer, {0, 0, base.sx_ - 1, base.sz_ - 1});
    return out;
}

void Heights::recompose(const Heights& base, const SculptLayer& layer, const SampleRect& r) {
    for (std::uint32_t z = r.z0; z <= std::min(r.z1, sz_ - 1); ++z)
        for (std::uint32_t x = r.x0; x <= std::min(r.x1, sx_ - 1); ++x)
            h_[static_cast<std::size_t>(z) * sx_ + x] = base.at(x, z) + layer.offset(x, z);
}

// ---- Tile layers (sculpt offsets, paint weights) ----

template <class T>
T TileLayer<T>::get(std::uint32_t x, std::uint32_t z) const noexcept {
    // Every tile holding a sample has the same value: read the one on its low side (inside the terrain).
    if (tiles_x_ == 0 || tiles_z_ == 0) return fill_;
    const std::uint32_t tx = std::min(x / kTileCells, tiles_x_ - 1), tz = std::min(z / kTileCells, tiles_z_ - 1);
    const auto it = tiles_.find({tx, tz});
    if (it == tiles_.end()) return fill_;
    const std::uint32_t lx = x - tx * kTileCells, lz = z - tz * kTileCells;
    if (lx >= kTileSamples || lz >= kTileSamples) return fill_;
    return it->second[static_cast<std::size_t>(lz) * kTileSamples + lx];
}

template <class T>
void TileLayer<T>::set(std::uint32_t x, std::uint32_t z, T value) {
    if (tiles_x_ == 0 || tiles_z_ == 0 || x > tiles_x_ * kTileCells || z > tiles_z_ * kTileCells) return;
    // A sample on a tile edge (or corner) is in up to four tiles: keep them all equal.
    const std::uint32_t tx_hi = std::min(x / kTileCells, tiles_x_ - 1), tz_hi = std::min(z / kTileCells, tiles_z_ - 1);
    const std::uint32_t tx_lo = x % kTileCells == 0 && x > 0 ? x / kTileCells - 1 : tx_hi;
    const std::uint32_t tz_lo = z % kTileCells == 0 && z > 0 ? z / kTileCells - 1 : tz_hi;
    for (std::uint32_t tz = tz_lo; tz <= tz_hi; ++tz)
        for (std::uint32_t tx = tx_lo; tx <= tx_hi; ++tx) {
            auto it = tiles_.find({tx, tz});
            if (it == tiles_.end()) {
                if (value == fill_) continue;  // nothing to store
                it = tiles_.emplace(std::pair{tx, tz}, std::vector<T>(static_cast<std::size_t>(kTileSamples) * kTileSamples, fill_)).first;
            }
            it->second[static_cast<std::size_t>(z - tz * kTileCells) * kTileSamples + (x - tx * kTileCells)] = value;
        }
}

template <class T>
std::uint64_t TileLayer<T>::hash() const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    auto add = [&](std::uint32_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    for (const auto& [key, values] : tiles_) {
        add(key.first);
        add(key.second);
        for (const T v : values) add(std::bit_cast<std::uint32_t>(v));
    }
    return h;
}

namespace {
void put_u32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
std::uint32_t get_u32(const std::string& in, std::size_t at) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(in[at + i])) << (8 * i);
    return v;
}
} // namespace

template <class T>
Status TileLayer<T>::save_as(const stdfs::path& file, std::string_view magic) const {
    std::string out(magic);
    put_u32(out, 1);
    put_u32(out, tiles_x_);
    put_u32(out, tiles_z_);
    put_u32(out, static_cast<std::uint32_t>(tiles_.size()));
    for (const auto& [key, values] : tiles_) {
        put_u32(out, key.first);
        put_u32(out, key.second);
        for (const T v : values) put_u32(out, std::bit_cast<std::uint32_t>(v));
    }
    return fs::write_file_atomic(file, out, false);
}

template <class T>
Result<TileLayer<T>> TileLayer<T>::load_as(const stdfs::path& file, std::string_view magic, std::string_view what, T fill) {
    auto bytes = fs::read_file(file, 1ull << 30);
    if (!bytes) return std::unexpected(bytes.error());
    const std::string& in = *bytes;
    const auto bad = [&](std::string_view why) {
        return fail(ErrorCode::ParseError, std::format("{} {}: {}", what, file.filename().string(), why));
    };
    if (in.size() < 24 || in.compare(0, 8, magic.data(), 8) != 0) return bad(std::format("not a {} file", what));
    if (get_u32(in, 8) != 1) return bad(std::format("version {} is newer than this engine reads", get_u32(in, 8)));
    TileLayer layer(get_u32(in, 12), get_u32(in, 16), fill);
    const std::size_t count = get_u32(in, 20);
    constexpr std::size_t kTileBytes = 8 + static_cast<std::size_t>(kTileSamples) * kTileSamples * 4;
    if (layer.tiles_x_ == 0 || layer.tiles_z_ == 0 || layer.tiles_x_ > 64 || layer.tiles_z_ > 64 || in.size() != 24 + count * kTileBytes)
        return bad("damaged (its size doesn't match its header)");
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 24 + i * kTileBytes;
        std::vector<T> values(static_cast<std::size_t>(kTileSamples) * kTileSamples);
        for (std::size_t k = 0; k < values.size(); ++k) values[k] = std::bit_cast<T>(get_u32(in, at + 8 + k * 4));
        layer.tiles_[{get_u32(in, at), get_u32(in, at + 4)}] = std::move(values);
    }
    return layer;
}

template class TileLayer<float>;
template class TileLayer<std::uint32_t>;

Result<SculptLayer> SculptLayer::load(const stdfs::path& file) {
    auto layer = load_as(file, "NSCULPT1", "sculpt layer", 0.0f);
    if (!layer) return std::unexpected(layer.error());
    return SculptLayer(std::move(*layer));
}

Result<PaintLayer> PaintLayer::load(const stdfs::path& file) {
    auto layer = load_as(file, "NPAINT01", "paint layer", kUnpainted);
    if (!layer) return std::unexpected(layer.error());
    return PaintLayer(std::move(*layer));
}

void Heights::repaint(const PaintLayer& paint) {
    if (paint.empty()) {
        w_.clear();
        return;
    }
    repaint(paint, {0, 0, sx_ - 1, sz_ - 1});
}

void Heights::repaint(const PaintLayer& paint, const SampleRect& r) {
    if (w_.empty()) w_.assign(static_cast<std::size_t>(sx_) * sz_, PaintLayer::kUnpainted);
    for (std::uint32_t z = r.z0; z <= std::min(r.z1, sz_ - 1); ++z)
        for (std::uint32_t x = r.x0; x <= std::min(r.x1, sx_ - 1); ++x) w_[static_cast<std::size_t>(z) * sx_ + x] = paint.get(x, z);
}

std::optional<float> raycast(const Heights& h, math::Vec3 origin, math::Vec3 dir, float max_distance) {
    // Clip the ray to the terrain's rectangle (x and z), then march.
    const math::Vec2 o = h.origin();
    float t_in = 0.0f, t_out = max_distance;
    auto slab = [&](float p, float d, float lo, float hi) {
        if (std::fabs(d) < 1e-9f) return p >= lo && p <= hi;
        float a = (lo - p) / d, b = (hi - p) / d;
        if (a > b) std::swap(a, b);
        t_in = std::max(t_in, a);
        t_out = std::min(t_out, b);
        return t_in <= t_out;
    };
    if (!slab(origin.x, dir.x, o.x, -o.x) || !slab(origin.z, dir.z, o.y, -o.y)) return std::nullopt;
    auto above = [&](float t) {
        const math::Vec3 p = origin + dir * t;
        return p.y - h.height(p.x, p.z);
    };
    const float step = h.cell_size() * 0.5f;
    float prev_t = t_in;
    if (above(t_in) < 0.0f) return std::nullopt;  // starts under the ground
    for (int i = 1;; ++i) {
        const float t = std::min(t_in + step * static_cast<float>(i), t_out);
        if (above(t) <= 0.0f) {
            float a = prev_t, b = t;
            for (int k = 0; k < 20; ++k) {
                const float m = 0.5f * (a + b);
                (above(m) > 0.0f ? a : b) = m;
            }
            return 0.5f * (a + b);
        }
        if (t >= t_out) return std::nullopt;
        prev_t = t;
    }
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
            const std::uint32_t packed = heights.weights(gx, gz);
            const math::Vec3 weights{PaintLayer::weight(packed, 1), PaintLayer::weight(packed, 2), PaintLayer::weight(packed, 3)};
            m.vertices.push_back({p, n, weights, {p.x, p.z}, {t.x, t.y, t.z, w}});
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
