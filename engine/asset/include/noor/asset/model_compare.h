#pragma once
// Comparing two imports of the same model (Milestone 8C, part 7): an FBX and the glTF exported from the same scene,
// or one file read by two importers. "The same" is what shows: the skeleton (nodes by name, parents), where every
// joint is at rest and at every moment of every clip, and the drawn shape (skinned meshes posed, the others placed),
// at rest and at a few moments of each clip. Not compared: bone orientations (exporters roll bones differently and
// the joints still land in the same places), vertex order and counts (exporters split vertices differently).
//
// Used by the FBX tests and `noor_cli compare-models`.

#include "noor/asset/gltf_import.h"
#include "noor/asset/json.h"

#include <string>
#include <vector>

namespace noor::asset {

struct CompareOptions {
    double sample_rate = 30.0;     // joint positions compared this often through each clip
    int shape_samples = 3;         // shapes compared at this many moments of each clip (and at rest)
    std::size_t max_points = 60000;  // points per shape compared (a larger mesh is sampled evenly)
    bool ignore_leaf_ends = true;  // Blender's FBX "leaf bones" (names ending _end) aren't part of the skeleton
};

struct ModelComparison {
    std::size_t nodes_a = 0, nodes_b = 0, matched = 0;
    std::vector<std::string> only_a, only_b;   // node names in one and not the other
    std::vector<std::string> parent_differs;   // matched nodes whose nearest matched ancestors differ
    std::uint64_t triangles_a = 0, triangles_b = 0;
    std::size_t skins_a = 0, skins_b = 0, joints_a = 0, joints_b = 0;
    float rest_joints = 0.0f;  // metres: the farthest a matched node is from its twin at rest
    std::string rest_joints_worst;
    float rest_shape = 0.0f;   // metres: how far the drawn shapes are apart at rest (both ways)
    struct Clip {
        std::string name;
        float duration_a = 0.0f, duration_b = 0.0f;
        double offset_b = 0.0;  // b's clip compared this many seconds in (aligned at the ends: b keeps its take's start)
        int samples = 0;
        float joints = 0.0f;   // metres, the worst over every sample
        std::string joints_worst;
        double joints_worst_time = 0.0;
        float shape = 0.0f;    // metres, the worst of the shape samples
    };
    std::vector<Clip> clips;
    std::vector<std::string> clips_only_a, clips_only_b;

    // The worst joint and shape distances over rest and every clip.
    [[nodiscard]] float worst_joints() const;
    [[nodiscard]] float worst_shape() const;
    [[nodiscard]] Json to_json() const;
};

[[nodiscard]] ModelComparison compare_models(const ImportedModel& a, const ImportedModel& b, const CompareOptions& options = {});

} // namespace noor::asset
