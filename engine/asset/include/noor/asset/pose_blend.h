#pragma once
// Blending poses (Milestone 8C, part 3, ADR-036): cross-fades, blend spaces, masked and additive layers.
//
// Rotations blend by normalised linear interpolation (nlerp) along the shorter way round: no trigonometry, so the
// result is the same bit for bit in Debug and Release, in the editor and in built games (ADR-036's "deterministic
// maths"). For the small steps between neighbouring poses it's indistinguishable from slerp; a weighted mix of several
// poses adds them in a fixed order, each turned to the first one's side, then normalises.
//
// Additive poses are differences: delta = how `pose` differs from `reference` in each node's own space (rotation
// conj(reference) * pose, translation pose - reference, scale pose / reference); applying one turns, moves and scales
// a base pose by that difference, weighted.

#include "noor/asset/animation.h"

#include <array>
#include <span>
#include <vector>

namespace noor::asset {

// Shortest-path normalised lerp: t = 0 gives a, 1 gives b (or -b: the same turn).
[[nodiscard]] math::Quat nlerp(math::Quat a, math::Quat b, float t);

// out[i] = a[i] blended towards b[i] by t (0 = a, 1 = b). out may be a or b.
void blend(std::span<const NodePose> a, std::span<const NodePose> b, float t, std::span<NodePose> out);
// The same per node with its own amount (masks: weights[i] of 0 leaves out[i] = a[i]).
void blend(std::span<const NodePose> a, std::span<const NodePose> b, std::span<const float> weights, std::span<NodePose> out);

// A weighted mix of several poses (weights summing to 1; zero weights skipped), in the order given.
void mix(std::span<const std::span<const NodePose>> poses, std::span<const float> weights, std::span<NodePose> out);

// Additive layers.
[[nodiscard]] NodePose additive_delta(const NodePose& reference, const NodePose& pose);
void apply_additive(std::span<NodePose> base, std::span<const NodePose> delta, std::span<const float> weights);

// Blend-space weights (summing to 1). 1D: the two samples around x, linearly (clamped at the ends; samples needn't be
// sorted; equal positions share). 2D: gradient-band interpolation (each sample's weight is the smallest of its
// "how far towards each other sample am I still" values, then normalised): exact at every sample, smooth between.
[[nodiscard]] std::vector<float> blend_weights_1d(std::span<const float> at, float x);
[[nodiscard]] std::vector<float> blend_weights_2d(std::span<const std::array<float, 2>> at, std::array<float, 2> p);

} // namespace noor::asset
