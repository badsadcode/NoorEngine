#pragma once
// Water's waves and surface for gameplay (Milestone 8D, part 2, ADR-037): one formula, evaluated here on the CPU for
// scripts, buoyancy and swimming, and in water.hlsl's vertex shader for drawing. Gameplay only ever reads this one.
//
// Four Gerstner waves (GPU Gems 1, ch. 1). The main one has the look's height (crest to trough), length and direction;
// three smaller, shorter ones come with it at fixed proportions (kWaveLength, kWaveTurn, kWaveAmplitude below: the
// shader has the same table, keep them in step). Each wave i, with k = 2 pi / length, w = sqrt(g k) (deep water) and
// a phase p = w t worked out in double precision (so it doesn't drift after hours), moves a point of the level surface
// at rest position x (on the plane, metres) to
//   x + Q a d cos(k d.x - p)  (sideways: crests sharpen as `choppiness` grows),   a sin(k d.x - p)  (up)
// with a = half its height, d its direction and Q chosen so the four together reach `choppiness` (at most 0.9: the
// surface never loops over itself). The height *at* a world point is found by stepping back from the sideways
// motion (a fixed-point search, 12 steps: exact to well under a millimetre for choppiness up to 0.9).
//
// It's a pure function of position and game time, with the same sines on every machine (fx::det_sin), so a built game
// agrees with the editor to the bit. The wind (the same ground wind particles feel, game::effect_wind) raises the
// waves by `wind` x the fully developed sea's height for that wind (0.21 U^2 / g); it leaves their lengths and
// directions alone, so a storm rolling in grows the waves without making them jump.

#include "noor/asset/water_asset.h"
#include "noor/foundation/math.h"
#include "noor/render/renderer.h"
#include "noor/runtime/world.h"
#include "noor/scene/scene.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace noor::game {

inline constexpr int kWaveCount = 4;
inline constexpr float kGravity = 9.81f;
// The three smaller waves, as fractions of the main one's length and height, and turned from its direction (radians).
inline constexpr std::array<float, kWaveCount> kWaveLength{1.0f, 0.62f, 0.38f, 0.24f};
inline constexpr std::array<float, kWaveCount> kWaveAmplitude{1.0f, 0.5f, 0.28f, 0.16f};
inline constexpr std::array<float, kWaveCount> kWaveTurn{0.0f, 0.42f, -0.55f, 0.95f};

// One water body's waves: what the look and the wind make of them.
struct Waves {
    float height = 0.0f;      // the main wave, crest to trough (metres), the wind's share included
    float length = 8.0f;      // the main wave's length (metres)
    float direction = 0.0f;   // radians, towards which they travel (0 = -Z, pi/2 = +X)
    float choppiness = 0.5f;  // 0..0.9

    [[nodiscard]] bool flat() const noexcept { return height <= 0.0f; }
    // Each wave's phase at `time` (radians, in [0, 2 pi)).
    [[nodiscard]] std::array<float, kWaveCount> phases(double time) const noexcept;
    // How a point of the surface at rest at `rest` (x, z) moves: {sideways x, up, sideways z}.
    [[nodiscard]] math::Vec3 offset(math::Vec2 rest, const std::array<float, kWaveCount>& phases) const noexcept;
    // The surface's height above the water level at world (x, z), and its normal there.
    [[nodiscard]] float height_at(math::Vec2 xz, const std::array<float, kWaveCount>& phases, math::Vec3* normal = nullptr) const noexcept;
};

// The waves of a look in a wind (m/s, the ground wind's speed).
[[nodiscard]] Waves make_waves(const asset::WaterLook& look, float wind_speed);

// A water body as gameplay sees it.
struct WaterBody {
    Guid entity;
    bool endless = false;              // a sea: everywhere
    math::Vec3 center;                 // its level is center.y
    math::Vec2 half_size{5.0f, 5.0f};  // a box's footprint, along its own axes
    float yaw = 0.0f;
    Waves waves;
    [[nodiscard]] bool covers(math::Vec2 xz) const noexcept;
};

using AssetPath = std::function<std::optional<std::filesystem::path>(const Guid&)>;

// A `water` component's look: its preset, then its asset, then its own fields (as drawing reads it).
[[nodiscard]] asset::WaterLook water_look(const asset::Json& component, const AssetPath& asset_path);

// A `water` component's body: its footprint from "size", "shape" and the entity's world matrix, with these waves.
[[nodiscard]] WaterBody water_body(const asset::Json& component, const math::Mat4& world, const Waves& waves);
// What the renderer draws for it (time and phases left for set_water_time).
[[nodiscard]] render::WaterDraw water_draw(const WaterBody& body, const asset::WaterLook& look);

// The surface at a world point: the highest water body covering it.
struct WaterSurface {
    float height = 0.0f;  // world y
    math::Vec3 normal{0, 1, 0};
    Guid entity;
};
[[nodiscard]] std::optional<WaterSurface> water_surface(const std::vector<WaterBody>& bodies, math::Vec2 xz, double time);

// The renderer's water draws at `time`: their phases (and time, for the ripples) set.
void set_water_time(std::span<render::WaterDraw> draws, double time);

// Water for gameplay: keeps the scene's water bodies (once a frame) and answers the World's water queries
// (runtime::api::water_height, Lua noor.water_height, C# Water.Height) at the world's time.
class WaterSystem final : public runtime::System {
public:
    explicit WaterSystem(AssetPath asset_path) : asset_path_(std::move(asset_path)) {}
    [[nodiscard]] std::string_view name() const override { return "water"; }
    void attached(runtime::World& world) override;
    void update(runtime::World& world, double dt) override;
    [[nodiscard]] const std::vector<WaterBody>& bodies() const noexcept { return bodies_; }
    // Rebuilds the bodies from the scene now (update does it every frame).
    void refresh(runtime::World& world);

private:
    AssetPath asset_path_;
    std::vector<WaterBody> bodies_;
};

} // namespace noor::game
