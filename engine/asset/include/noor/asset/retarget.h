#pragma once
// Retargeting (Milestone 8C, part 5, ADR-036): a clip made for one humanoid played on another, whatever its bones are
// called, however long its limbs are and whatever pose it was built in (T or A).
//
// Both rigs get a **humanoid bone map**: which of their nodes is the hips, the left upper arm and so on (22 bones;
// fingers aren't mapped yet). The map is detected from the names: presets for **Rigify** (`DEF-spine.001`...),
// **Mixamo** (`mixamorig:LeftArm`, any `prefix:`), the **Unreal mannequin** (`upperarm_l`) and Blender's **metarig**
// (`upper_arm.L`); failing those, common names ("hips", "upper_arm.L", "LeftForeArm", "calf_r"...). A model's
// .noormeta can correct it: "settings": {"bone_map": {"LeftHand": "Hand_L", "LeftToes": null}}.
//
// The transfer works on directions, relative to each rig's rest pose:
//   - each mapped bone of the target is first turned so it points where the source's bone points at rest (the
//     shortest turn from one to the other: an A-pose arm is lifted to the source's T-pose; twist is the target's own);
//   - then every frame it gets the same turn, in the model's space, as the source bone has made from its rest pose.
// So at any moment a target bone points where the source bone points. Bones with nothing to point at (head, hands,
// toes) take their parent's correction. The hips also move: by the source's movement from rest, scaled by the ratio
// of leg lengths (hips height above the feet), so a shorter character takes shorter steps and root motion follows.
//
// `bake` turns a source clip into an ordinary clip of the target (keys at every source key and at least 60 a second,
// linear), so everything else (blending, controllers, root motion, events) works on it unchanged. Only bones the clip
// moves (or whose parents it turns) are baked; the rest of the target keeps its own pose.

#include "noor/asset/animation.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noor::asset {

enum class HumanBone : std::uint8_t {
    Hips, Spine, Chest, UpperChest, Neck, Head,
    LeftShoulder, LeftUpperArm, LeftLowerArm, LeftHand,
    RightShoulder, RightUpperArm, RightLowerArm, RightHand,
    LeftUpperLeg, LeftLowerLeg, LeftFoot, LeftToes,
    RightUpperLeg, RightLowerLeg, RightFoot, RightToes,
    Count
};
inline constexpr std::size_t kHumanBones = static_cast<std::size_t>(HumanBone::Count);

[[nodiscard]] std::string_view human_bone_name(HumanBone bone);              // "LeftUpperArm"
[[nodiscard]] std::optional<HumanBone> parse_human_bone(std::string_view name);
// The 15 a body needs to be retargeted: hips, spine, head, and both arms (upper, lower, hand) and legs (upper, lower,
// foot). Chest, upper chest, neck, shoulders and toes are optional.
[[nodiscard]] bool human_bone_required(HumanBone bone);

struct BoneMap {
    std::array<int, kHumanBones> nodes{};  // node index per human bone; -1: not mapped
    std::string preset;                    // "Rigify", "Mixamo", "Unreal mannequin", "Blender metarig", "names", "none"
                                           // (" (edited)" after it when the .noormeta changed it)
    std::vector<std::string> warnings;     // in plain words: missing bones, odd hierarchy, unknown names

    BoneMap() { nodes.fill(-1); }
    [[nodiscard]] int node(HumanBone b) const { return nodes[static_cast<std::size_t>(b)]; }
    [[nodiscard]] bool usable() const;  // every required bone mapped
};

// Detects the map from the node names (presets first, then common names).
[[nodiscard]] BoneMap detect_bone_map(const ImportedModel& model);
// Detects, then applies corrections: {"<HumanBone>": "<node name>" or null}.
[[nodiscard]] BoneMap bone_map(const ImportedModel& model, const Json& corrections);
// The map for a model file: its .noormeta's "settings"."bone_map" corrections, if any.
[[nodiscard]] BoneMap bone_map_for(const ImportedModel& model, const std::filesystem::path& file);

// Every node's transform in the model's space for a pose (parents applied): for tests, tools and drawing skeletons.
[[nodiscard]] std::vector<math::Mat4> posed_world(const ImportedModel& model, std::span<const NodePose> pose);

class Retargeter {
public:
    Retargeter(const ImportedModel& source, const BoneMap& source_map, const ImportedModel& target, const BoneMap& target_map);

    [[nodiscard]] bool usable() const noexcept { return usable_; }
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }
    [[nodiscard]] float hips_scale() const noexcept { return hips_scale_; }

    // The target pose for a whole source pose (every source node). Mapped target bones are set (only those `only` marks,
    // when given: the others keep what `target_pose` has); everything else in `target_pose` is left as it is.
    void transfer(std::span<const NodePose> source_pose, std::span<NodePose> target_pose, const std::vector<bool>* only = nullptr) const;

    // The clip as the target's own (same name and length). `rate`: the fewest keys a second.
    [[nodiscard]] ImportedAnimation bake(const ImportedAnimation& clip, double rate = 60.0) const;

private:
    struct Xf {
        math::Vec3 t{0, 0, 0};
        math::Quat r{};
        math::Vec3 s{1, 1, 1};
    };
    static void globals(const ImportedModel& model, std::span<const NodePose> pose, std::vector<Xf>& out);

    const ImportedModel* source_;
    const ImportedModel* target_;
    std::vector<int> source_of_;           // per target node: the source node it follows, or -1
    std::vector<math::Quat> correction_;   // per target node: the turn that points it where the source's points at rest
    std::vector<Xf> source_rest_, target_rest_;
    std::vector<NodePose> source_rest_pose_, target_rest_pose_;
    int source_hips_ = -1, target_hips_ = -1;
    float hips_scale_ = 1.0f;
    bool usable_ = false;
    std::vector<std::string> warnings_;
};

} // namespace noor::asset
