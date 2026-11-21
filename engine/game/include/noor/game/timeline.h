#pragma once
// Playing timelines (gameplay foundations, part 5): the `animation_player` component and its system.
//
//   "animation_player": {"timelines": {"open": "<.noortimeline id>", "close": "<id>"}, "autoplay": "open",
//                        "playing": "open", "time": 0.4, "speed": 1}
//
// "playing", "time" and "speed" are its state, written as it plays (scripts start and stop it: runtime::api::
// play_timeline). Value tracks are applied every frame, call and emit keys fire once as play passes them, and when a
// timeline that doesn't loop ends, the entity sends the message "timeline_end" ({"name"}). On the game's clock: it pauses
// with the game. asset/timeline_asset.h describes the file.

#include "noor/asset/timeline_asset.h"
#include "noor/runtime/world.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

namespace noor::game {

// Applies a timeline's value tracks at time t to the entity playing it and its children (used by the system, and by the
// editor's preview). Targets that aren't there are skipped.
void apply_timeline(runtime::World& world, const Guid& player, const asset::Timeline& timeline, double t);
// The entity a track's target path names, from `player` ("" itself, "Arm/Hand" by names of children).
[[nodiscard]] std::optional<Guid> timeline_target(runtime::World& world, const Guid& player, std::string_view path);

class TimelineSystem final : public runtime::System {
public:
    using Resolver = std::function<std::optional<std::filesystem::path>(const Guid&)>;
    explicit TimelineSystem(Resolver resolve) : resolve_(std::move(resolve)) {}
    [[nodiscard]] std::string_view name() const override { return "timelines"; }
    void entity_started(runtime::World& world, scene::Entity& entity) override;
    void update(runtime::World& world, double dt) override;

    // A timeline by asset id (loaded once; nothing if it can't be read, reported once).
    [[nodiscard]] const asset::Timeline* timeline(const Guid& id);

private:
    Resolver resolve_;
    std::unordered_map<Guid, std::optional<asset::Timeline>> cache_;
    void fire(runtime::World& world, const Guid& player, const asset::Timeline& t, double from, double to);
};

} // namespace noor::game
