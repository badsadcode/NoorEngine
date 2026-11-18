#pragma once
// Terrain (ADR-031, Milestone 6): tiled heightfields. Runtime-safe (games generate the same terrain
// from the same settings). Sculpting (the editor, noor_terrain_edit) writes a sparse layer of height
// offsets on top of the generated base, so regenerating the base keeps the manual edits.
//
// A `terrain` component:
//   { "tiles": [2, 2], "cell_size": 1,                        tiles of 256 x 256 cells (257 samples)
//     "base": "noise" | "heightmap" | "flat" | "graph",
//     "graph": <.noorgraph id>, "graph_output": "terrain",     graph: a procedural graph's height field
//                                                             (games read it cooked: game::cooked_graph_folder)
//     "seed": 1, "height": 40, "scale": 250, "octaves": 5,    noise: the hills (metres)
//     "heightmap": <texture id>, "min_height": 0, "max_height": 50,   a greyscale PNG (16-bit best)
//     "flat_height": 0,
//     "sculpt": <.noorsculpt id>,                             hand-made offsets (SculptLayer), optional
//     "material": <.noormat id>,                              the ground (layer 0) without "layers"
//     "layers": [{"material": <id>, "tiling": 1}, ...],       up to four materials, blended by...
//     "paint": <.noorpaint id>,                               ...their painted weights (PaintLayer)
//     "collision": true }
// The terrain is centred on its entity (x and z); its transform's rotation and scale are ignored.
// Right-handed, +Y up, metres. Neighbouring tiles share their edge samples, so they meet exactly.
#include "noor/asset/json.h"
#include "noor/asset/material.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"
#include "noor/render/renderer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace noor::terrain {

inline constexpr std::uint32_t kTileCells = 256;
inline constexpr std::uint32_t kTileSamples = kTileCells + 1;

struct Settings {
    std::uint32_t tiles_x = 2, tiles_z = 2;
    float cell_size = 1.0f;
    enum class Base : std::uint8_t { Flat, Noise, Heightmap, Field } base = Base::Noise;
    std::uint64_t seed = 1;
    float height = 40.0f;  // noise: from the lowest to the highest point (metres)
    float scale = 250.0f;  // noise: the size of the largest hills (metres)
    std::uint32_t octaves = 5;
    std::filesystem::path heightmap;  // resolved file
    float min_height = 0.0f, max_height = 50.0f;
    float flat_height = 0.0f;
    std::filesystem::path sculpt;  // resolved .noorsculpt file (empty: none)
    std::filesystem::path paint;   // resolved .noorpaint file (empty: all layer 0)
    // Base::Field ("base": "graph"): the cooked height field (a .noorfield, Milestone 7) and where it
    // came from. The caller fills `base_field` (game::graph_terrain_settings) and adopt_field_shape()
    // takes the terrain's tiles and cell size from the file.
    std::filesystem::path base_field;
    std::optional<Guid> graph;
    std::string graph_output = "terrain";
    [[nodiscard]] Status adopt_field_shape();

    // From a `terrain` component; `resolve` finds the heightmap's and the sculpt layer's files.
    [[nodiscard]] static Settings from_json(const asset::Json& component, const asset::AssetFileResolver& resolve);
    // A text that changes whenever the generated heights would (includes the heightmap's file time).
    [[nodiscard]] std::string key() const;
    [[nodiscard]] float width() const noexcept { return static_cast<float>(tiles_x * kTileCells) * cell_size; }
    [[nodiscard]] float depth() const noexcept { return static_cast<float>(tiles_z * kTileCells) * cell_size; }
    // The same settings without the sculpt and paint layers: the generated base alone.
    [[nodiscard]] Settings base_only() const {
        Settings s = *this;
        s.sculpt.clear();
        s.paint.clear();
        return s;
    }
};

// A rectangle of samples, inclusive, in terrain sample coordinates.
struct SampleRect {
    std::uint32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    [[nodiscard]] std::size_t count() const noexcept { return static_cast<std::size_t>(x1 - x0 + 1) * (z1 - z0 + 1); }
    void include(const SampleRect& o) noexcept {
        x0 = std::min(x0, o.x0);
        z0 = std::min(z0, o.z0);
        x1 = std::max(x1, o.x1);
        z1 = std::max(z1, o.z1);
    }
};

// Per-sample values stored sparsely by tile: only tiles that were touched exist, each 257 x 257 like
// the heights (a sample on a tile edge is kept equal in every tile that shares it). Untouched samples
// read as `fill`. Files: an 8-byte magic, u32 version (1), u32 tiles_x, u32 tiles_z, u32 tile count,
// then per tile u32 tx, u32 tz and 257 x 257 values of 4 bytes (little-endian, row by row).
template <class T>
class TileLayer {
public:
    static_assert(sizeof(T) == 4);
    TileLayer() = default;
    TileLayer(std::uint32_t tiles_x, std::uint32_t tiles_z, T fill) : tiles_x_(tiles_x), tiles_z_(tiles_z), fill_(fill) {}

    [[nodiscard]] std::uint32_t tiles_x() const noexcept { return tiles_x_; }
    [[nodiscard]] std::uint32_t tiles_z() const noexcept { return tiles_z_; }
    // The terrain changed size: values keep their tile; tiles outside are kept but not used.
    void resize(std::uint32_t tiles_x, std::uint32_t tiles_z) noexcept {
        tiles_x_ = tiles_x;
        tiles_z_ = tiles_z;
    }
    [[nodiscard]] T get(std::uint32_t x, std::uint32_t z) const noexcept;
    // Sets a sample (in every tile that shares it). Samples outside the terrain are ignored.
    void set(std::uint32_t x, std::uint32_t z, T value);
    [[nodiscard]] bool empty() const noexcept { return tiles_.empty(); }
    [[nodiscard]] std::size_t tile_count() const noexcept { return tiles_.size(); }
    [[nodiscard]] std::uint64_t hash() const noexcept;

protected:
    [[nodiscard]] Status save_as(const std::filesystem::path& file, std::string_view magic) const;
    [[nodiscard]] static Result<TileLayer> load_as(const std::filesystem::path& file, std::string_view magic, std::string_view what, T fill);

private:
    std::uint32_t tiles_x_ = 0, tiles_z_ = 0;
    T fill_{};
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<T>> tiles_;
};

// Hand-made height offsets (metres) added to the generated base (Milestone 6, part 6): a
// `.noorsculpt` file, magic "NSCULPT1", f32 values.
class SculptLayer : public TileLayer<float> {
public:
    static constexpr std::string_view kExtension = ".noorsculpt";
    SculptLayer() = default;
    SculptLayer(std::uint32_t tiles_x, std::uint32_t tiles_z) : TileLayer(tiles_x, tiles_z, 0.0f) {}
    explicit SculptLayer(TileLayer<float>&& base) : TileLayer(std::move(base)) {}
    [[nodiscard]] static Result<SculptLayer> load(const std::filesystem::path& file);
    [[nodiscard]] Status save(const std::filesystem::path& file) const { return save_as(file, "NSCULPT1"); }
    // Offset at a sample (terrain sample coordinates); 0 where nothing was sculpted.
    [[nodiscard]] float offset(std::uint32_t x, std::uint32_t z) const noexcept { return get(x, z); }
};

// How much of each of a terrain's four material layers covers a sample (Milestone 6, part 7): a
// `.noorpaint` file, magic "NPAINT01", one u32 per sample: byte i (from the lowest) is layer i's
// weight 0..255, and the four add up to 255. Unpainted samples are all layer 0.
class PaintLayer : public TileLayer<std::uint32_t> {
public:
    static constexpr std::string_view kExtension = ".noorpaint";
    static constexpr std::uint32_t kLayers = 4;
    static constexpr std::uint32_t kUnpainted = 0x000000FFu;
    PaintLayer() : TileLayer(0, 0, kUnpainted) {}
    PaintLayer(std::uint32_t tiles_x, std::uint32_t tiles_z) : TileLayer(tiles_x, tiles_z, kUnpainted) {}
    explicit PaintLayer(TileLayer<std::uint32_t>&& base) : TileLayer(std::move(base)) {}
    [[nodiscard]] static Result<PaintLayer> load(const std::filesystem::path& file);
    [[nodiscard]] Status save(const std::filesystem::path& file) const { return save_as(file, "NPAINT01"); }
    [[nodiscard]] static float weight(std::uint32_t packed, std::uint32_t layer) noexcept {
        return static_cast<float>((packed >> (8 * layer)) & 0xFFu) / 255.0f;
    }
};

// Deterministic gradient noise (fractal, in -1..1): only + - * /, floor and integer hashing, so the
// same seed gives the same value on every machine (Thornwake's lesson, its decision D5).
[[nodiscard]] float fractal_noise(double x, double z, std::uint64_t seed, std::uint32_t octaves);

// The whole terrain's height samples: (tiles_x * 256 + 1) by (tiles_z * 256 + 1), row by row
// (z rows of x samples), local to the terrain's corner (sample (0, 0) is at -width/2, -depth/2).
class Heights {
public:
    [[nodiscard]] static Result<Heights> build(const Settings& settings);
    // Built once per settings key and shared (the renderer, physics and the editor all ask).
    [[nodiscard]] static Result<std::shared_ptr<const Heights>> shared(const Settings& settings);
    // The base plus a sculpt layer, everywhere or (recompose) in one rectangle.
    [[nodiscard]] static Heights compose(const Heights& base, const SculptLayer& layer);
    void recompose(const Heights& base, const SculptLayer& layer, const SampleRect& rect);
    // Material weights (PaintLayer packing), everywhere or in one rectangle.
    void repaint(const PaintLayer& paint);
    void repaint(const PaintLayer& paint, const SampleRect& rect);
    // A sample's packed weights (all layer 0 when nothing was painted).
    [[nodiscard]] std::uint32_t weights(std::uint32_t x, std::uint32_t z) const noexcept {
        return w_.empty() ? PaintLayer::kUnpainted : w_[static_cast<std::size_t>(z) * sx_ + x];
    }

    [[nodiscard]] std::uint32_t samples_x() const noexcept { return sx_; }
    [[nodiscard]] std::uint32_t samples_z() const noexcept { return sz_; }
    [[nodiscard]] float cell_size() const noexcept { return cell_; }
    [[nodiscard]] float at(std::uint32_t x, std::uint32_t z) const noexcept { return h_[static_cast<std::size_t>(z) * sx_ + x]; }
    // Height at a point in the terrain's space (centred: x in -width/2..width/2), bilinear; clamped at the edges.
    [[nodiscard]] float height(float x, float z) const noexcept;
    // Surface normal at a sample (central differences; one-sided at the outer edges).
    [[nodiscard]] math::Vec3 normal(std::uint32_t x, std::uint32_t z) const noexcept;
    // One tile's samples (257 x 257), copied out (physics).
    [[nodiscard]] std::vector<float> tile(std::uint32_t tx, std::uint32_t tz) const;
    // A fingerprint of every sample's bits (determinism tests).
    [[nodiscard]] std::uint64_t hash() const noexcept;
    // The corner of sample (0, 0) in the terrain's space.
    [[nodiscard]] math::Vec2 origin() const noexcept { return {-0.5f * cell_ * static_cast<float>(sx_ - 1), -0.5f * cell_ * static_cast<float>(sz_ - 1)}; }
    // Sample coordinates (fractional) of a point in the terrain's space.
    [[nodiscard]] math::Vec2 to_samples(float x, float z) const noexcept {
        const math::Vec2 o = origin();
        return {(x - o.x) / cell_, (z - o.y) / cell_};
    }
    [[nodiscard]] std::uint32_t tiles_x() const noexcept { return (sx_ - 1) / kTileCells; }
    [[nodiscard]] std::uint32_t tiles_z() const noexcept { return (sz_ - 1) / kTileCells; }

private:
    std::uint32_t sx_ = 0, sz_ = 0;
    float cell_ = 1.0f;
    std::vector<float> h_;
    std::vector<std::uint32_t> w_;  // packed material weights per sample; empty: all layer 0
};

// A tile as a render mesh: 257 x 257 vertices (UVs in metres: materials tile with their uv_scale),
// 2 x 256 x 256 triangles, in the terrain's space. The vertex colour carries the weights of material
// layers 1, 2 and 3 (layer 0 gets the rest): terrains are drawn as splats (render::DrawItem::splat).
struct TileMesh {
    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    math::Vec3 lo, hi;  // bounds
};
[[nodiscard]] TileMesh tile_mesh(const Heights& heights, std::uint32_t tx, std::uint32_t tz);

// Where a ray (in the terrain's space) first meets the ground: the distance along `dir` (unit), or
// nothing within max_distance or outside the terrain. Marches half a cell at a time, then bisects.
[[nodiscard]] std::optional<float> raycast(const Heights& heights, math::Vec3 origin, math::Vec3 dir, float max_distance = 5000.0f);

} // namespace noor::terrain
