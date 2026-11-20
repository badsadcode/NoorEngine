#pragma once
// Linear blend skinning on the CPU (Milestone 8C, ADR-036): the reference the GPU path is checked against, and what
// tools use where they need posed positions (bounds, picking). The vertex shader does exactly this:
//
//   palette[j] = world(joint j) x inverse_bind[j]       (a skinned mesh's own node transform is ignored, as glTF says)
//   skinned(p) = sum over the vertex's four joints of weight x palette[joint] x p

#include "noor/asset/gltf_import.h"

#include <span>
#include <vector>

namespace noor::asset {

// One palette for a skin, from its joints' world matrices (in the skin's joint order).
[[nodiscard]] std::vector<math::Mat4> skin_palette(const ImportedSkin& skin, std::span<const math::Mat4> joint_worlds);

// A point and a direction (normal, tangent: not translated, renormalised) moved by their skin weights.
[[nodiscard]] math::Vec3 skin_point(const SkinWeights& w, std::span<const math::Mat4> palette, math::Vec3 p);
[[nodiscard]] math::Vec3 skin_direction(const SkinWeights& w, std::span<const math::Mat4> palette, math::Vec3 d);

} // namespace noor::asset
