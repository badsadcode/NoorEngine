#pragma once
// Scene documents (ADR-004): entities with stable GUIDs, a parent hierarchy, transforms and
// typed components, saved as deterministic JSON (ADR-005).

#include "noor/asset/gltf_import.h"
#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noor::scene {

// Format history: 1 = initial; 2 = entities gain "layer" and "tags" (spec 9.1).
inline constexpr int kSceneFormat = 2;

struct Transform {
    math::Vec3 position{0, 0, 0};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};
    [[nodiscard]] math::Mat4 matrix() const { return math::compose(position, rotation, scale); }
};

// Component data lives in JSON keyed by type name ("mesh", "marker", "collider", ...).
// Unknown types are preserved verbatim on load/save.
struct Entity {
    Guid id;
    std::string name;
    std::optional<Guid> parent;
    std::string layer = "default";
    std::vector<std::string> tags;
    Transform transform;
    asset::Json components = asset::Json::object();

    [[nodiscard]] bool has(std::string_view type) const { return components.contains(std::string(type)); }
};

class Scene {
public:
    Guid id = Guid::generate();
    std::string name = "Untitled";
    std::vector<Entity> entities;  // parents before children

    // Upgrades applied while loading (e.g. "1 -> 2: ..."). Not saved. Saving writes the current
    // format and keeps the previous file as <file>.bak.
    std::vector<std::string> migrations_applied;

    [[nodiscard]] static Result<Scene> load(const std::filesystem::path& path);
    [[nodiscard]] static Result<Scene> from_text(std::string_view text, std::string_view source);
    [[nodiscard]] Status save(const std::filesystem::path& path) const;
    [[nodiscard]] std::string to_text() const;

    [[nodiscard]] const Entity* find(const Guid& id) const;
    // World matrix of entity `index` (parents applied). Missing parents are treated as roots.
    // Every entity's world matrix at once, in entity order: each parent's is computed once and reused
    // (world_matrix() looks parents up one by one, which is quadratic over a whole scene).
    [[nodiscard]] std::vector<math::Mat4> world_matrices() const;
    [[nodiscard]] math::Mat4 world_matrix(std::size_t index) const;
};

// Adds one entity per node of an imported model under a new root entity named `root_name`.
// Mesh nodes get {"mesh": {"asset", "mesh_index"}}; Noor Interchange markers get "marker";
// collision nodes get "collider" and never "mesh". Returns the root entity's id.
Guid instantiate_model(Scene& scene, const Guid& asset, const asset::ImportedModel& model, std::string_view root_name,
                       const Transform& root_transform = {});

// Problems found by validate(): asset references that don't resolve, broken parents, duplicate ids.
struct ValidationIssue {
    std::string entity;  // name (id)
    std::string message;
};

// Checks structure, and asset references against `asset_exists` (a predicate on asset GUIDs).
template <class AssetExists>
[[nodiscard]] std::vector<ValidationIssue> validate(const Scene& scene, AssetExists&& asset_exists);

// ---- implementation of the template --------------------------------------------------------
std::vector<ValidationIssue> validate_structure(const Scene& scene);
std::vector<std::pair<std::size_t, Guid>> asset_references(const Scene& scene);

template <class AssetExists>
std::vector<ValidationIssue> validate(const Scene& scene, AssetExists&& asset_exists) {
    auto issues = validate_structure(scene);
    for (const auto& [index, guid] : asset_references(scene)) {
        if (!asset_exists(guid)) {
            const Entity& e = scene.entities[index];
            issues.push_back({e.name + " (" + e.id.to_string() + ")",
                              "references missing asset " + guid.to_string()});
        }
    }
    return issues;
}

} // namespace noor::scene
