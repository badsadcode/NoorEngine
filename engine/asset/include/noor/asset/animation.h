#pragma once
// Animation clips (Milestone 8C, part 2, ADR-036): sampling a glTF animation at a time, as glTF defines it.
//
// A clip has channels; each moves one node's translation, rotation or scale through keys (times in seconds):
//   STEP:        the key's value until the next key;
//   LINEAR:      straight between keys (rotations: spherical, along the shorter way round);
//   CUBICSPLINE: glTF's Hermite curve through the keys with their in- and out-tangents (rotations then normalised).
// Before the first key a channel holds its first value, after the last its last. Nodes no channel moves keep their
// rest pose. The same time always gives the same pose (no state): that's what makes playback deterministic.

#include "noor/asset/gltf_import.h"

#include <span>

namespace noor::asset {

struct NodePose {
    math::Vec3 translation{0, 0, 0};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};
};

// The model's rest pose: each node's own transform.
[[nodiscard]] std::vector<NodePose> rest_pose(const ImportedModel& model);

// Sets the nodes `clip` moves to where they are at `seconds` (clamped to the clip); the others are left as they are.
void sample(const ImportedAnimation& clip, double seconds, std::span<NodePose> pose);

// Spherical interpolation along the shorter way round (unit quaternions in, a unit quaternion out).
[[nodiscard]] math::Quat slerp(math::Quat a, math::Quat b, float t);

// A clip by name; nullptr if the model has none of that name.
[[nodiscard]] const ImportedAnimation* find_animation(const ImportedModel& model, std::string_view name);

// ---- Root motion and events (part 4) ----
// The node whose movement is the character's: the topmost node (fewest ancestors; then the first) that any clip moves
// the translation of (a Rigify or Mixamo hips bone, a "root" bone). -1 if no clip moves any translation.
[[nodiscard]] int root_motion_node(const ImportedModel& model);

// How far `node` moves (its translation) while the clip's clock goes from t0 to t1 seconds (t0 <= t1, not wrapped).
// Looping: each time the clip wraps round, a whole cycle's distance is added (last key minus first), so a walk that
// moves 1.4 m a cycle covers 1.4 m every cycle exactly. Not looping: clamped to the clip.
[[nodiscard]] math::Vec3 root_delta(const ImportedAnimation& clip, int node, const NodePose& rest, double t0, double t1, bool loop);

// How many times the moment `at` seconds into a clip of `duration` seconds is passed when its clock goes from t0 to t1
// (after t0, up to and including t1): every cycle if it loops, at most once if not.
[[nodiscard]] int crossings(double t0, double t1, double at, double duration, bool loop);

} // namespace noor::asset
