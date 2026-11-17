#pragma once
// CSG models in scenes (ADR-016): an entity with a `csg_model` component whose children carry
// `brush` components (see docs/level/CSG.md for the JSON). Reading them, and cooking a model into
// the format games load (noor/game/cooked_csg.h). Editor and tools only.
#include "noor/csg/csg.h"
#include "noor/game/cooked_csg.h"
#include "noor/scene/scene.h"

#include <optional>
#include <vector>

namespace noor::csg {

struct SceneModel {
    std::vector<Brush> brushes;                      // in order
    std::vector<Guid> brush_ids;                     // the entity of each brush
    std::vector<std::optional<Guid>> slot_materials;  // per material slot (slot 0: none, the default)
};

// The brushes of the model at `model_index` of an (expanded) scene, from their components and local transforms.
[[nodiscard]] SceneModel brushes_from_scene(const scene::Scene& scene, std::size_t model_index);

// Builds the model and packs it for games: one submesh per material slot, the key of its brushes.
// `model` (optional) receives the full result (triangle sources and all).
[[nodiscard]] Result<game::CookedCsg> cook_model(const scene::Scene& scene, std::size_t model_index, Model* model = nullptr,
                                                 std::vector<std::string>* warnings = nullptr);

} // namespace noor::csg
