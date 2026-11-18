#pragma once
// Scattered instances in a scene (Milestone 7, part 7): an entity with a `scatter` component shows a
// procedural graph's instance set output (a forest, a rock field) with a hand-made layer on top that
// regenerating the graph never touches:
//
//   "scatter": { "graph": <.noorgraph id>, "output": "trees",
//                "ground": <entity id of a terrain> | null,    stand on it (its sculpting included)
//                "removed": ["9f3c0a1b2c3d4e5f", ...],         generated instances taken out, by id
//                "added": [{"id": "a1...", "asset": <model id>, "position": [x, y, z], "yaw": 30, "scale": 1}] }
//
// Positions are in the entity's space. Removed ids refer to generated instances: with another seed
// those trees don't exist, so they stay gone (and come back as removed if the seed goes back). Added
// instances are the user's and always stay. Games read the cooked output (.noor/cooked/graphs/<id>/
// <output>.instances.json), never the graph.
#include "noor/asset/instance_set.h"
#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <memory>
#include <set>
#include <vector>

namespace noor::game {

struct ScatterItem {
    Guid asset;
    std::uint64_t id = 0;
    bool added = false;    // hand-placed (in the component), not generated
    math::Vec3 position{};  // in the entity's space
    math::Quat rotation{};
    float scale = 1.0f;
};

// An instance set file, read once and kept until the file changes (size or time). Thread-safe.
[[nodiscard]] Result<std::shared_ptr<const asset::InstanceSet>> cached_instance_set(const std::filesystem::path& file);

// Where a scatter component's generated instances are (the cooked graph output).
[[nodiscard]] std::filesystem::path scatter_file(const asset::Json& component, const std::filesystem::path& project_root);

// What a scatter component shows: its generated instances minus the removed ones, plus the added ones.
// A missing output (not cooked yet) gives just the added ones and the reason in `warning`.
[[nodiscard]] std::vector<ScatterItem> scatter_items(const asset::Json& component, const std::filesystem::path& project_root,
                                                     std::string* warning = nullptr);

// The graphs a scene needs cooked: terrains on a graph base and scatter components.
[[nodiscard]] std::vector<Guid> graphs_used(const scene::Scene& scene);

// A new id for a hand-placed instance (random; can't collide with generated ones in practice).
[[nodiscard]] std::uint64_t new_instance_id();

} // namespace noor::game
