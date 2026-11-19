#pragma once
// Sprite animation (Milestone 8B, part 6, ADR-035): which frame a sprite shows, and the runtime side of playing
// animations from scripts. No GPU here: games' drawing (sprites.h), the editor and tests all ask the same functions.
//
// A sprite's animation clock comes from its component:
//   - played by a script (runtime::api::sprite_play): "started" (game seconds) and "speed"; stopped: "paused_at"
//     (seconds into the animation where it froze);
//   - otherwise the scene's clock: seconds x "speed" + "time_offset" (a looping idle that needs no script).
// 8-direction sheets (asset::SpriteSheet::directions) pick their column from the angle between the entity's forward
// (its -Z, like every NoorEngine entity) and the way to the camera: 0 faces the viewer, turning to the viewer's left.

#include "noor/asset/sprite_asset.h"
#include "noor/runtime/world.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace noor::game {

// The direction (0..count-1) an entity at `world` shows to a viewer at `eye`. 0 when count <= 1.
[[nodiscard]] int sprite_direction(const math::Mat4& world, math::Vec3 eye, int count);

// Seconds into its animation a sprite is at `seconds` of game (or scene) time.
[[nodiscard]] double sprite_time(const asset::Json& component, double seconds);

// The frame a sprite shows: its animation at its time and direction, or its still "frame". Clamped to the sheet.
[[nodiscard]] int sprite_frame(const asset::SpriteSheet& sheet, const asset::Json& component, const math::Mat4& world, math::Vec3 eye,
                               double seconds);

// The runtime half of script-played animations: posts an AnimationEnd event (runtime::Event, with the animation's
// name) once when a non-looping animation a script played has shown its last frame for its full time. Sheets are read
// once per file (they're small) through `resolve`.
class SpriteAnimationSystem final : public runtime::System {
public:
    using Resolver = std::function<std::optional<std::filesystem::path>(const Guid&)>;
    explicit SpriteAnimationSystem(Resolver resolve) : resolve_(std::move(resolve)) {}
    [[nodiscard]] std::string_view name() const override { return "Sprites"; }
    void update(runtime::World& world, double dt) override;
    void entity_destroyed(runtime::World& world, const scene::Entity& entity) override;

    // How many AnimationEnd events it has posted (reports, tests).
    [[nodiscard]] std::size_t ended() const noexcept { return ended_; }

private:
    const asset::SpriteSheet* sheet(const Guid& id);
    Resolver resolve_;
    std::unordered_map<Guid, std::optional<asset::SpriteSheet>> sheets_;  // by sheet asset id; nullopt: failed
    std::unordered_map<Guid, double> reported_;                            // entity -> the "started" whose end was posted
    std::size_t ended_ = 0;
};

} // namespace noor::game
