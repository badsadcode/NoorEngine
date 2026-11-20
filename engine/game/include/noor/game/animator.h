#pragma once
// Skeletal animation playback (Milestone 8C, part 2, ADR-036): a model's "animator" component plays one of its clips,
// and AnimatorSystem poses the model's bone entities from it every frame.
//
// The component sits on a model's root (next to "model") and keeps the same fields as a sprite's:
//   "animation"    the clip's name (from the glTF file)
//   "loop"         true (default): wraps round; false: holds the last pose, and a script-played clip sends
//                  on_animation_end(name) once it gets there
//   "speed"        1 = as authored
//   "time_offset"  seconds; for a clip that plays on the scene's clock (no script), so a crowd isn't in step
//   "started"      set by scripts (runtime::api::play_animation): the game time it was played at
//   "paused_at"    set by scripts (stop_animation, speed 0): seconds into the clip where it froze
// Without "started" a clip plays on the scene's clock (seconds x speed + time_offset): a looping idle needs no script.
// Cross-fades (part 3, runtime::api::cross_fade): "fade" (seconds) and "from" (the clip it's leaving, with its own
// clock fields); the two are blended by how far into the fade it is, still a pure function of time.
//
// Or it runs a controller (part 3, asset/anim_controller.h): {"controller": "<.nooranim id>", "parameters": {...},
// "speed": 1}. Scripts set the parameters (set_animation_parameter); the system steps the controller in update (so a
// state's end reaches scripts that frame) and writes the base layer's state into "state" when it changes. A controller
// carries state from frame to frame: replays are identical because steps are (the same dt, the same parameters).
//
// Part 4: "root_motion": true. The clip's own movement of the character's root bone (asset::root_motion_node: Ward's
// hips) moves the character instead of the bone: the bone is held in place horizontally, and what it would have moved
// this frame goes to the nearest character controller at or above the model (as its velocity, so walls stop it and
// slopes and steps work), or else to the model's own position. Up and down stays in the pose; turning is the script's.
// "events": [{"clip": "Walk", "at": 0.5, "name": "step"}] names moments of plain clips (controllers name them on
// their states); scripts hear on_animation_event(name) each time one is passed.
//
// Playback is a pure function of time (asset::sample): the same game time gives the same pose, in the editor, in a
// built game and on any machine. Clips move the nodes they name; the model's other nodes are left alone. Which entity
// each node became comes from the root's "model": {"nodes": [...]} (written by scene::instantiate_model); a model
// placed before 8C part 2 lacks it, so its nodes are matched by name below the root (said once).

#include "noor/asset/anim_controller.h"
#include "noor/asset/animation.h"
#include "noor/runtime/world.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::game {

// Seconds into its clip an animator is at, at `seconds` of game (or scene) time: wrapped for a looping clip, held
// at its ends otherwise.
[[nodiscard]] double animator_time(const asset::Json& component, double seconds, double duration);

class AnimatorSystem final : public runtime::System {
public:
    using Resolver = std::function<std::optional<std::filesystem::path>(const Guid&)>;
    explicit AnimatorSystem(Resolver resolve) : resolve_(std::move(resolve)) {}
    [[nodiscard]] std::string_view name() const override { return "Animation"; }
    void attached(runtime::World& world) override;                // the "animator.play" command
    void update(runtime::World& world, double dt) override;       // controllers step; end events
    void late_update(runtime::World& world, double dt) override;  // poses the bones (after scripts and physics)
    void entity_destroyed(runtime::World& world, const scene::Entity& entity) override;

    struct Stats {
        std::size_t animators = 0;     // animators posed last frame
        std::size_t nodes = 0;         // bone entities they moved
        std::size_t ended = 0;         // AnimationEnd events posted so far
        std::uint64_t fingerprint = 0; // of every pose written last frame (equal runs, equal fingerprints)
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    struct Model {
        asset::ImportedModel model;
        std::vector<int> animated;  // nodes any of its clips move, in node order
        std::vector<asset::NodePose> rest;
        int root = -1;  // asset::root_motion_node
    };
    struct Running {  // a controller running for one character
        Guid controller, model;
        std::unique_ptr<asset::AnimControllerPlayer> player;
    };
    const Model* model(const Guid& id);
    const asset::AnimController* controller(const Guid& id);
    asset::AnimControllerPlayer* running(const scene::Entity& root);
    asset::AnimParameters parameters(const asset::Json& component, const asset::AnimController& controller) const;
    const std::vector<Guid>& bind(runtime::World& world, const scene::Entity& root);
    void say_once(const std::string& message);
    void move_character(runtime::World& world, scene::Entity& model_root, math::Vec3 moved, double dt);

    Resolver resolve_;
    std::unordered_map<Guid, std::optional<Model>> models_;  // by asset id; nullopt: failed to load
    std::unordered_map<Guid, std::optional<asset::AnimController>> controllers_;  // by asset id
    std::unordered_map<Guid, Running> running_;               // root entity -> its controller
    std::unordered_map<Guid, std::vector<Guid>> nodes_;      // root entity -> the entity of each node (nil: none)
    std::unordered_map<Guid, double> reported_;              // root entity -> the "started" whose end was posted
    std::vector<asset::NodePose> pose_, from_;               // scratch
    std::unordered_map<std::string, bool> said_;
    Stats stats_;
};

} // namespace noor::game
