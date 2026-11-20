#pragma once
// Animation controllers (Milestone 8C, part 3, ADR-036): `.nooranim` files, and the player that runs one for a model.
//
// A controller has parameters (set by scripts), and layers; each layer has states and transitions between them:
//
//   {
//     "noor_anim": 1,
//     "parameters": { "speed": {"type": "float", "default": 0}, "waving": {"type": "bool"}, "nod": {"type": "trigger"} },
//     "layers": [
//       { "name": "Base", "entry": "Move",
//         "states": [
//           { "name": "Move", "blend": { "x": "speed", "samples": [ {"clip": "Idle", "x": 0}, {"clip": "Walk", "x": 1.4} ] } },
//           { "name": "Nod", "clip": "Nod", "loop": false } ],
//         "transitions": [
//           { "from": "Move", "to": "Nod", "when": [ {"parameter": "nod"} ], "fade": 0.1 },
//           { "from": "Nod", "to": "Move", "exit_time": 1, "fade": 0.25 } ] },
//       { "name": "Arm", "mode": "override", "mask": ["DEF-upper_arm.R"], "entry": "Off", "states": [ {"name": "Off"}, ... ] },
//       { "name": "Lean", "mode": "additive", "weight": "lean", "states": [ {"name": "Lean", "clip": "Lean", "loop": false} ] } ]
//   }
//
// States: one "clip", a "blend" space ("x" alone: 1D; "x" and "y": 2D, samples with "x" and "y"), or neither (an
// empty state: an override layer passes what's below it through, an additive one adds nothing; fading to it fades the
// layer out). "loop" (default true), "speed" (a number, or a parameter's name: multiplies).
// Transitions: "from" a state or "*" (any), "to", "when" (all must hold: {"parameter"} alone is true / non-zero;
// "greater", "less", "equals", "not_equals" compare), "exit_time" (how far through "from" it must be: 1 = once
// through), "fade" (seconds, default 0.2). The first that holds, in file order, fires; triggers it tested are reset.
// A layer mid-fade doesn't start another transition (scripts can still cross-fade it).
// States can name moments ("events": [{"at": 0.5, "name": "step"}], how far through: scripts hear
// on_animation_event(name) each time it's passed; part 4).
// Layers: the first is the base (whole body, full weight). Others: "mode" "override" (default) or "additive", "weight"
// (a number or a parameter's name, 0..1), "mask" (bone names: each with everything below it; none: every bone).
// Additive layers add how their state's pose differs from its first moment.
//
// Timing: a state's progress ("phase": 0 at its start, 1 after one time through) moves by dt / its length; a blend
// space's length is its samples' lengths weighted, and every sample is played at the same phase (a walk and a run
// stay in step). Transitions are checked before time moves on: a fade of F seconds is over after F / dt frames, exactly.

#include "noor/asset/animation.h"
#include "noor/asset/json.h"
#include "noor/foundation/error.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace noor::asset {

inline constexpr std::string_view kAnimControllerExtension = ".nooranim";

struct AnimParameter {
    enum class Type : std::uint8_t { Float, Bool, Trigger } type = Type::Float;
    std::string name;
    float initial = 0.0f;  // bools and triggers: 0 or 1
};

// A number given directly, or a parameter's name (read each frame).
struct AnimValue {
    float value = 1.0f;
    std::string parameter;
};

struct AnimState {
    std::string name;
    std::string clip;  // a single clip; empty with no samples: an empty state
    struct Sample {
        std::string clip;
        std::array<float, 2> at{0, 0};
    };
    std::vector<Sample> samples;  // a blend space
    std::string x, y;             // its parameters (y empty: 1D)
    bool loop = true;
    AnimValue speed;
    struct Event {
        float at = 0.0f;  // how far through the state (0..1), each time through
        std::string name;
    };
    std::vector<Event> events;  // part 4: scripts hear on_animation_event(name)
};

struct AnimCondition {
    enum class Op : std::uint8_t { True, Greater, Less, Equals, NotEquals } op = Op::True;
    std::string parameter;
    float value = 0.0f;
};

struct AnimTransition {
    int from = -1;  // a state index; -1: any state
    int to = 0;
    std::vector<AnimCondition> when;
    std::optional<float> exit_time;
    float fade = 0.2f;
};

struct AnimLayer {
    std::string name;
    enum class Mode : std::uint8_t { Override, Additive } mode = Mode::Override;
    AnimValue weight;
    std::vector<std::string> mask;
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    int entry = 0;
};

struct AnimController {
    std::vector<AnimParameter> parameters;
    std::vector<AnimLayer> layers;
    [[nodiscard]] const AnimParameter* parameter(std::string_view name) const;
};

// Reads a controller; errors name the file, the layer and what's wrong, in plain words.
[[nodiscard]] Result<AnimController> parse_anim_controller(const Json& json, std::string_view label = "controller");
[[nodiscard]] Result<AnimController> load_anim_controller(const std::filesystem::path& path);

// Parameter values by name (floats; bools and triggers 0 or 1).
using AnimParameters = std::unordered_map<std::string, float>;

// Runs a controller for one model. Holds each layer's state, progress and fade: create one per character.
class AnimControllerPlayer {
public:
    AnimControllerPlayer(const AnimController& controller, const ImportedModel& model);

    // Problems found binding the controller to the model (clips it doesn't have, mask bones it doesn't have).
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    struct Ended {
        int layer;
        std::string state;
        std::string event;  // in `events`: the event's name
    };
    // One step: transitions whose conditions hold fire (their triggers are named in `consumed`), then time moves on
    // by dt. Non-looping states that reached their end this step are named in `ended`; states' events passed this
    // step (the current state of each layer, not one fading out) in `events`, in order.
    void step(double dt, const AnimParameters& params, std::vector<std::string>* consumed = nullptr, std::vector<Ended>* ended = nullptr,
              std::vector<Ended>* events = nullptr);

    // Root motion (part 4): `node` (asset::root_motion_node) is the character's root. The base layer's movement of it
    // over each step is measured (root_motion()), and pose() holds it in place horizontally. -1: off.
    void set_root_motion(int node) { root_ = node; }
    [[nodiscard]] math::Vec3 root_motion() const noexcept { return moved_; }

    // Goes to a state now (fade 0) or fading over `fade` seconds; false if the layer has no such state. The base layer
    // if `layer` is empty or unknown.
    bool play(std::string_view state, float fade, std::string_view layer = {});

    // The pose now, with every layer applied (starting from the model's rest pose).
    void pose(const AnimParameters& params, std::span<NodePose> out);

    [[nodiscard]] std::string_view state(std::size_t layer = 0) const;
    [[nodiscard]] double phase(std::size_t layer = 0) const;
    [[nodiscard]] float fade_weight(std::size_t layer = 0) const;  // how far the current fade is (1: none)
    [[nodiscard]] std::size_t layers() const noexcept { return layers_.size(); }

private:
    struct Running {
        int state = 0;
        double phase = 0.0;
        int from = -1;  // fading out of (-1: none)
        double from_phase = 0.0;
        double fade = 0.0, faded = 0.0;
    };
    struct Bound {
        std::vector<const ImportedAnimation*> clips;  // the state's clip, or its samples' (nullptr: missing)
    };
    double length(int layer, int state, const AnimParameters& params) const;
    void sample_state(int layer, int state, double phase, const AnimParameters& params, std::span<NodePose> out);
    float value(const AnimValue& v, const AnimParameters& params) const;
    std::vector<float> sample_weights(const AnimState& s, const AnimParameters& params) const;

    math::Vec3 root_delta_of(int layer, int state, double p0, double p1, const AnimParameters& params) const;

    const AnimController* controller_;
    int root_ = -1;
    math::Vec3 moved_{0, 0, 0};
    const ImportedModel* model_;
    std::vector<NodePose> rest_;
    std::vector<std::vector<Bound>> bound_;           // [layer][state]
    std::vector<std::vector<float>> masks_;           // [layer][node] 0 or 1
    std::vector<Running> layers_;
    std::vector<std::string> warnings_;
    std::vector<NodePose> a_, b_, ref_;               // scratch
    std::vector<std::vector<NodePose>> samples_;      // scratch for blend spaces
};

} // namespace noor::asset
