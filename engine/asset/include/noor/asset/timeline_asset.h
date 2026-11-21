#pragma once
// Timelines (gameplay foundations, part 5; Godot's AnimationPlayer animations): keyframed tracks that animate any
// property of an entity and its children, and do things at moments. A `.noortimeline` file:
//
//   {"noor_timeline": 1, "name": "DoorOpen", "length": 2.0, "loop": false,
//    "tracks": [
//      {"target": "Hinge", "property": "euler",
//       "keys": [{"t": 0, "v": [0, 0, 0]}, {"t": 1.2, "v": [0, 95, 0], "ease": "back_out"}]},
//      {"target": "", "property": "light.intensity", "keys": [{"t": 0, "v": 0}, {"t": 0.5, "v": 800}]},
//      {"target": "", "call": true, "keys": [{"t": 0.1, "function": "creak", "args": []}]},
//      {"target": "", "emit": true, "keys": [{"t": 2.0, "message": "opened", "data": {}}]}]}
//
// `target` is a path from the entity playing it ("" itself, "Hinge" a child, "Arm/Hand" deeper). Value tracks' property
// is anything a tween can move (runtime/tween.h). A key's "ease" shapes the way *into* it from the key before (linear if
// absent). Call keys call a function of the target's script; emit keys send a message from the target.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace noor::asset {

inline constexpr int kTimelineFormat = 1;

struct TimelineKey {
    double t = 0.0;
    Json value;            // value tracks
    std::string ease;      // value tracks: the way into this key ("" = linear)
    std::string function;  // call tracks
    std::string message;   // emit tracks
    Json args = Json::array(), data;
};

struct TimelineTrack {
    enum class Kind : std::uint8_t { Value, Call, Emit } kind = Kind::Value;
    std::string target;    // path from the player's entity
    std::string property;  // value tracks
    std::vector<TimelineKey> keys;  // by time
};

struct Timeline {
    std::string name;
    double length = 1.0;
    bool loop = false;
    std::vector<TimelineTrack> tracks;
};

[[nodiscard]] Result<Timeline> parse_timeline(const Json& file);
[[nodiscard]] Json timeline_file_json(const Timeline& t);
[[nodiscard]] Result<Timeline> load_timeline_file(const std::filesystem::path& path);

// Where a value track is at time t: the two keys around it and how far between (0..1, before easing), or one key
// (before the first, after the last, or exactly a key: `b == a`, f = 0). Nothing if the track has no keys.
struct KeySpan {
    const TimelineKey* a = nullptr;
    const TimelineKey* b = nullptr;
    double f = 0.0;
};
[[nodiscard]] std::optional<KeySpan> key_span(const TimelineTrack& track, double t);

// Puts a track's keys in time order (stable: keys at the same time keep their order).
void sort_keys(TimelineTrack& track);

} // namespace noor::asset
