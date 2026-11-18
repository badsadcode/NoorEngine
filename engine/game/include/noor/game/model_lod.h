#pragma once
// Levels of detail for placed models (Milestone 7, part 6, ADR-032).
//
// A model with a Noor Interchange manifest can have lower levels in `<name>.lod<n>.glb` files,
// each used up to a camera distance (`max_distance`, metres, for a 60-degree vertical field of
// view). A placed model (an entity with a `model` component and its node entities below it) is
// drawn as a whole at one level: level 0 is its own entities (as authored, so edits to its nodes
// show up close); a lower level replaces all of them with that level file's meshes, placed by the
// model's root. The switch is per model, by its distance from the camera, scaled for the camera's
// field of view and the model's size.

#include "noor/foundation/math.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace noor::game {

struct LodLevel {
    std::filesystem::path file;          // level 0: the model itself
    std::optional<double> max_distance;  // metres at a 60-degree field of view; none: no limit
    bool billboard = false;
};

struct ModelLods {
    std::vector<LodLevel> levels;  // in order; empty or one level: nothing to switch
    [[nodiscard]] bool switches() const noexcept { return levels.size() > 1; }
};

// The levels a model's manifest lists whose files exist. Nothing (no switching) without a valid
// manifest, or with a manifest whose levels have no distances: a model never silently disappears
// or loses detail because of a bad manifest.
[[nodiscard]] ModelLods read_model_lods(const std::filesystem::path& model_file);

// Which level to draw for a model whose root is `distance` metres from the camera. `fov_y` is the
// camera's vertical field of view (radians): a narrower view (zoomed in) keeps detail further away.
// `scale` is the model's size relative to its file (a tree scaled 2x keeps detail twice as far).
[[nodiscard]] int choose_lod(const ModelLods& lods, float distance, float fov_y, float scale = 1.0f);

} // namespace noor::game
