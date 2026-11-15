#pragma once
// Prefabs (ADR-024): reusable entity trees. A scene stores an instance as one placement entity
// with a "prefab" component (asset GUID, removed entities, overrides); expand_prefabs() builds the
// runtime view with the prefab's entities under it, using derived (stable) entity ids.

#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::scene {

inline constexpr int kPrefabFormat = 1;
inline constexpr std::string_view kPrefabExtension = ".noorprefab";

struct Prefab {
    Guid id = Guid::generate();
    std::string name;
    std::vector<Entity> entities;  // root first (no parent), parents before children

    [[nodiscard]] static Result<Prefab> load(const std::filesystem::path& path);
    [[nodiscard]] static Result<Prefab> from_text(std::string_view text, std::string_view source);
    [[nodiscard]] std::string to_text() const;
    [[nodiscard]] Status save(const std::filesystem::path& path) const;
};

// Looks up a prefab by GUID; nullptr when unknown.
using PrefabResolver = std::function<const Prefab*(const Guid&)>;

struct ExpandIssue {
    std::string entity;  // name (id) of the instance or entity concerned
    std::string message;
    bool error = true;  // false = warning (e.g. an override whose target left the prefab)
};

struct ExpandedScene {
    Scene scene;  // authored entities plus every expanded prefab entity
    std::vector<ExpandIssue> issues;
    std::size_t instances = 0;  // instances expanded, nested ones included
};

// The runtime view of a scene. Never saved: the authored scene stays small and diffable.
[[nodiscard]] ExpandedScene expand_prefabs(const Scene& scene, const PrefabResolver& resolve);

// A new instance entity for `prefab` (not yet added to a scene).
[[nodiscard]] Entity make_instance(const Guid& prefab, std::string name, const Transform& placement = {},
                                   std::optional<Guid> parent = std::nullopt);

// Turns the subtree under `root` into a prefab with fresh ids (its root at the prefab's origin).
// With `replace_with_instance`, the subtree in `scene` becomes an instance entity that keeps the old
// root's id, name, parent, transform and position in the scene, so nothing that referenced the root breaks.
[[nodiscard]] Result<Prefab> make_prefab(Scene& scene, const Guid& root, std::string name, bool replace_with_instance);

// Loads prefab files through the asset registry on first use and keeps them in memory.
class PrefabLibrary {
public:
    PrefabLibrary(const asset::Project& project, const asset::AssetRegistry& registry)
        : project_(&project), registry_(&registry) {}

    [[nodiscard]] const Prefab* get(const Guid& id);
    [[nodiscard]] PrefabResolver resolver() {
        return [this](const Guid& id) { return get(id); };
    }
    // Files that failed to load (reported once each).
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }
    void clear() {
        cache_.clear();
        errors_.clear();
    }

private:
    const asset::Project* project_;
    const asset::AssetRegistry* registry_;
    std::unordered_map<Guid, std::optional<Prefab>> cache_;  // nullopt = failed to load
    std::vector<std::string> errors_;
};

// Expanded id of a prefab entity inside an instance: Guid::derive(instance, prefab-local id).
[[nodiscard]] inline Guid expanded_id(const Guid& instance, const Guid& prefab_local) {
    return Guid::derive(instance, prefab_local);
}

} // namespace noor::scene
