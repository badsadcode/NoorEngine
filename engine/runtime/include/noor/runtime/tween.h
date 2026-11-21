#pragma once
// Tweens (gameplay foundations, part 4): a value moved smoothly from where it is to where it should be over some seconds,
// with an easing. The World owns them (World::tweens()), so Lua and C# share one implementation; scripts reach them
// through runtime::api::tween. They step every frame right after the systems' update, on the game's clock (so they
// pause with the game and follow the time scale) or the real one.
//
// What can be tweened, on an entity:
//   "position", "euler" (degrees, each angle on its own: 0 to 360 is a full turn), "rotation" (the shortest turn to the
//   given Euler angles), "scale" (local), "world_position";
//   "<component>.<field>[.<field>...]": a number, an array of numbers, or a "#RRGGBB[AA]" colour (blended per channel)
//   in the entity's component data ("light.intensity", "sprite.color", "camera.height").
// A new tween of the same entity and property replaces the one already running. A tween whose entity goes stops.

#include "noor/asset/json.h"
#include "noor/foundation/guid.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace noor::runtime {

class World;

// The easings, t from 0 to 1: linear; quad, cubic, sine, back, elastic, bounce, each _in, _out or _in_out ("ease_in",
// "ease_out", "ease_in_out" are quad's). nullopt for an unknown name.
[[nodiscard]] std::optional<double> ease(std::string_view name, double t);

// The property code tweens and timelines share (gameplay foundations, parts 4 and 5): a property's value now (nothing if
// the entity hasn't got it), whether two values can be blended, and writing the blend of two values at k (0..1; past 1
// for overshooting easings).
[[nodiscard]] std::optional<asset::Json> read_property(World& w, const Guid& e, std::string_view property);
[[nodiscard]] bool blendable(std::string_view property, const asset::Json& a, const asset::Json& b);
void write_property(World& w, const Guid& e, std::string_view property, const asset::Json& from, const asset::Json& to, double k);

struct TweenOptions {
    double delay = 0.0;   // seconds before it starts (its starting value is read then)
    int loops = 1;        // how many times it plays; -1: forever
    bool yoyo = false;    // every other play goes back
    bool real = false;    // the real clock: runs while paused
};

class Tweens {
public:
    // Starts one; an error message if the entity, property, value or easing won't do.
    [[nodiscard]] std::variant<std::uint64_t, std::string> start(World& w, const Guid& entity, std::string property, asset::Json to,
                                                                double seconds, std::string ease, TweenOptions options);
    void cancel(std::uint64_t id) { tweens_.erase(id); }
    [[nodiscard]] bool active(std::uint64_t id) const { return tweens_.contains(id); }
    [[nodiscard]] std::size_t count() const noexcept { return tweens_.size(); }
    // Moves them all on (game dt and real dt), in the order they started.
    void step(World& w, double dt, double real_dt);

private:
    struct Tween {
        Guid entity;
        std::string property;
        asset::Json from, to;  // from: read when it starts (after the delay)
        double seconds = 0.0, elapsed = 0.0, delay = 0.0;
        std::string ease;
        int loops_left = 1;
        bool yoyo = false, real = false, started = false, backwards = false;
    };
    std::map<std::uint64_t, Tween> tweens_;
    std::uint64_t next_ = 1;
};

} // namespace noor::runtime
