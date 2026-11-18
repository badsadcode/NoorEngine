#pragma once
// Terrain (ADR-031, Milestone 6): tiled heightfields. Runtime-safe (games generate the same terrain
// from the same settings); editing tools come later and write layers on top.
//
// A `terrain` component:
//   { "tiles": [2, 2], "cell_size": 1,                        tiles of 256 x 256 cells (257 samples)
//     "base": "noise" | "heightmap" | "flat",
//     "seed": 1, "height": 40, "scale": 250, "octaves": 5,    noise: the hills (metres)
//     "heightmap": <texture id>, "min_height": 0, "max_height": 50,   a greyscale PNG (16-bit best)
//     "flat_height": 0,
//     "material": <.noormat id>, "collision": true }
// The terrain is centred on its entity (x and z); its transform's rotation and scale are ignored.
// Right-handed, +Y up, metres. Neighbouring tiles share their edge samples, so they meet exactly.
#include "noor/asset/json.h"
#include "noor/asset/material.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"
#include "noor/render/renderer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace noor::terrain {

inline constexpr std::uint32_t kTileCells = 256;
inline constexpr std::uint32_t kTileSamples = kTileCells + 1;

struct Settings {
    std::uint32_t tiles_x = 2, tiles_z = 2;
    float cell_size = 1.0f;
    enum class Base : std::uint8_t { Flat, Noise, Heightmap } base = Base::Noise;
    std::uint64_t seed = 1;
    float height = 40.0f;  // noise: from the lowest to the highest point (metres)
    float scale = 250.0f;  // noise: the size of the largest hills (metres)
    std::uint32_t octaves = 5;
    std::filesystem::path heightmap;  // resolved file
    float min_height = 0.0f, max_height = 50.0f;
    float flat_height = 0.0f;

    // From a `terrain` component; `resolve` finds the heightmap's file.
    [[nodiscard]] static Settings from_json(const asset::Json& component, const asset::AssetFileResolver& resolve);
    // A text that changes whenever the generated heights would (includes the heightmap's file time).
    [[nodiscard]] std::string key() const;
    [[nodiscard]] float width() const noexcept { return static_cast<float>(tiles_x * kTileCells) * cell_size; }
    [[nodiscard]] float depth() const noexcept { return static_cast<float>(tiles_z * kTileCells) * cell_size; }
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

private:
    std::uint32_t sx_ = 0, sz_ = 0;
    float cell_ = 1.0f;
    std::vector<float> h_;
};

// A tile as a render mesh: 257 x 257 vertices (UVs in metres: materials tile with their uv_scale),
// 2 x 256 x 256 triangles, in the terrain's space.
struct TileMesh {
    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    math::Vec3 lo, hi;  // bounds
};
[[nodiscard]] TileMesh tile_mesh(const Heights& heights, std::uint32_t tx, std::uint32_t tz);

} // namespace noor::terrain
