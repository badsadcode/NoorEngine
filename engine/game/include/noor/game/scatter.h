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
#include "noor/terrain/terrain.h"

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

// Where each instance is in the world: the entity's transform applied, standing on `ground` (a
// terrain's heights, its centre at `ground_at`) when given. The same for drawing and for physics.
struct PlacedInstance {
    Guid asset;
    math::Mat4 world;
};
[[nodiscard]] std::vector<PlacedInstance> place_scatter(const std::vector<ScatterItem>& items, const math::Mat4& entity_world,
                                                        const terrain::Heights* ground, math::Vec3 ground_at);

// The same, kept between frames (per scatter entity) and rebuilt only when the component, the entity's
// matrix, the cooked file or the ground changes: a meadow of grass is tens of thousands of instances,
// too many to place again every frame. The cache holds the ground it placed on, which also keeps
// terrain::Heights::shared from rebuilding the terrain every frame. Thread-safe. `warning` as for
// scatter_items.
// The instances grouped by model, ready to draw (shared, never copied per frame).
struct ScatterPlacement {
    struct Group {
        Guid asset;
        std::shared_ptr<const std::vector<math::Mat4>> worlds;
    };
    std::vector<Group> groups;  // in order of first use
    std::size_t count = 0;
};
[[nodiscard]] std::shared_ptr<const ScatterPlacement> place_scatter_cached(const Guid& entity, const asset::Json& component,
                                                                                     const math::Mat4& entity_world,
                                                                                     const std::filesystem::path& project_root,
                                                                                     std::shared_ptr<const terrain::Heights> ground, math::Vec3 ground_at,
                                                                                     std::string* warning = nullptr);

// The graphs a scene needs cooked: terrains on a graph base and scatter components.
[[nodiscard]] std::vector<Guid> graphs_used(const scene::Scene& scene);

// A new id for a hand-placed instance (random; can't collide with generated ones in practice).
[[nodiscard]] std::uint64_t new_instance_id();

} // namespace noor::game
