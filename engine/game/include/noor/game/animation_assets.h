#pragma once
// The animated models and controllers a scene uses, loaded once (Milestone 8C, ADR-036). Shared by AnimatorSystem
// (games, Play) and the editor's Animation panel preview (part 6), so both pose a character from exactly the same
// data: the model's clips (or another model's, retargeted with "retarget_from"), its rest pose, its root-motion bone
// and the names controllers' masks may use for its bones.

#include "noor/asset/anim_controller.h"
#include "noor/asset/animation.h"
#include "noor/asset/retarget.h"
#include "noor/foundation/guid.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::game {

class AnimationAssets {
public:
    using Resolver = std::function<std::optional<std::filesystem::path>(const Guid&)>;
    using Warn = std::function<void(const std::string&)>;  // problems found loading (said once by the caller)
    explicit AnimationAssets(Resolver resolve, Warn warn = {}) : resolve_(std::move(resolve)), warn_(std::move(warn)) {}

    struct Model {
        asset::ImportedModel model;  // with "retarget_from", the other model's clips retargeted in place of its own
        std::vector<int> animated;   // nodes any of its clips move, in node order
        std::vector<asset::NodePose> rest;
        int root = -1;               // asset::root_motion_node
        math::Mat4 root_space{};     // from the root bone's parent's space into the model's (an armature's 0.01 scale)
        std::unordered_map<std::string, int> aliases;  // other names controllers' masks may use: humanoid bones
                                                       // ("RightUpperArm"), the clips' model's bones when retargeted
    };

    // The model `id` (an asset id) with `clips`' clips retargeted onto it (nil: its own); nullptr if it can't load.
    const Model* model(const Guid& id, const Guid& clips = {});
    // The model a character's animator plays: its "model" asset, with its animator's "retarget_from" clips if any.
    const Model* model_of(const scene::Entity& root);
    const asset::AnimController* controller(const Guid& id);

    // Forget everything loaded (files changed on disk).
    void clear() {
        models_.clear();
        controllers_.clear();
    }

private:
    void warn(const std::string& message) const {
        if (warn_) warn_(message);
    }
    Resolver resolve_;
    Warn warn_;
    std::unordered_map<std::string, std::optional<Model>> models_;  // by asset id (and clips' id); nullopt: failed
    std::unordered_map<Guid, std::optional<asset::AnimController>> controllers_;  // by asset id
};

// Which entity each of a placed model's nodes became: from the root's "model": {"nodes": [...]} (scene::
// instantiate_model writes it); for a model placed before 8C part 2, matched by name below the root (then
// `*by_name` is set). Nil where a node has no entity.
[[nodiscard]] std::vector<Guid> model_node_entities(const scene::Scene& scene, const scene::Entity& root, const asset::ImportedModel& model,
                                                    bool* by_name = nullptr);

} // namespace noor::game
