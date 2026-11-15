#pragma once
// Internal to noor_editor_api: an editor document (ADR-025). Owns an open project, its asset
// registry, one open scene with its undo history, and publishes render snapshots for viewports.

#include "noor/asset/file_watcher.h"
#include "noor/asset/json.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"
#include "noor/runtime/world.h"
#include "noor/scene/history.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noor::editor {

// What a viewport needs to draw the document's scene, immutable once published.
struct RenderSnapshot {
    struct Mesh {
        std::string asset_path;  // absolute path of the GLB
        int mesh_index = -1;
        math::Mat4 world;
        Guid entity;             // expanded entity id
        std::uint64_t asset_version = 0;  // bumps when the file changes on disk: reload it
    };
    // Every entity of the expanded scene, for picking, highlighting and gizmos.
    struct Entity {
        Guid id;
        std::optional<Guid> parent;
        math::Mat4 world;
        math::Quat world_rotation;  // product of the rotations up the parent chain
        scene::Transform local;
        bool editable = true;  // false for entities that come from a prefab
    };
    std::uint64_t revision = 0;
    std::vector<Mesh> meshes;
    std::vector<Entity> entities;
    struct Marker {
        math::Mat4 world;
        Guid entity;
    };
    std::vector<Marker> markers;
    // Simple shapes from `primitive` components (sizes baked into `world`, unit shapes below).
    struct Primitive {
        enum class Shape : std::uint8_t { Box, Sphere, Capsule } shape;
        math::Mat4 world;  // box: +-0.5 cube scaled to size; sphere: radius 0.5 scaled; capsule: unscaled
        float radius = 0.5f, height = 2.0f;  // capsule only
        math::Vec3 color;  // linear
        Guid entity;
    };
    std::vector<Primitive> primitives;
};

// Shared between a document and any viewports showing it; outlives either side.
class SnapshotChannel {
public:
    void publish(std::shared_ptr<const RenderSnapshot> snapshot) {
        std::scoped_lock lock(mutex_);
        current_ = std::move(snapshot);
    }
    [[nodiscard]] std::shared_ptr<const RenderSnapshot> get() const {
        std::scoped_lock lock(mutex_);
        return current_;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const RenderSnapshot> current_;
};

class Document {
public:
    [[nodiscard]] static Result<std::unique_ptr<Document>> open(const std::filesystem::path& project);
    [[nodiscard]] static Result<std::unique_ptr<Document>> create(const std::filesystem::path& folder, std::string_view name);

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] asset::Json project_info() const;
    [[nodiscard]] Result<asset::Json> assets(bool rescan);
    [[nodiscard]] asset::Json scenes() const;

    [[nodiscard]] Status open_scene(std::string_view relative);
    [[nodiscard]] Status new_scene(std::string_view relative, std::string_view name);
    [[nodiscard]] Status save_scene();
    [[nodiscard]] asset::Json scene_snapshot() const;

    // Applies one command (ADR-025 command JSON). Every change goes through the undo history.
    [[nodiscard]] Status execute(const asset::Json& command);
    bool undo();
    bool redo();

    // Play mode (ADR-026): a runtime copy of the expanded scene that behaviours update. Edits are
    // refused while playing; Stop throws the runtime copy away.
    [[nodiscard]] Status play();
    void stop();
    [[nodiscard]] bool playing() const noexcept { return world_ != nullptr; }
    void tick(double seconds, const runtime::InputState& input = {});

    // Checks watched asset files (models, prefabs) for changes on disk. Changed files get a new
    // version in the render snapshot (viewports reload them) and prefabs are re-read.
    // Returns the project-relative paths that changed.
    [[nodiscard]] asset::Json poll_files();

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::shared_ptr<SnapshotChannel> channel() const noexcept { return channel_; }

private:
    explicit Document(asset::Project project);
    [[nodiscard]] Status require_scene() const;
    [[nodiscard]] Status add_asset(const asset::Json& command);
    [[nodiscard]] Status create(const asset::Json& command);
    void changed();  // bumps the revision and republishes the render snapshot
    void publish(const scene::Scene& expanded, const std::unordered_set<Guid>& authored);
    void watch_assets();
    [[nodiscard]] Status require_editable() const;

    asset::Project project_;
    asset::AssetRegistry registry_;
    mutable scene::PrefabLibrary prefabs_;  // a cache of prefab files: filling it isn't a change
    std::optional<scene::Scene> scene_;
    std::filesystem::path scene_file_;
    scene::History history_;
    std::uint64_t revision_ = 0;
    std::unique_ptr<runtime::World> world_;  // play mode (ADR-026/027)
    asset::FileWatcher watcher_;
    std::unordered_map<std::string, std::uint64_t> asset_versions_;  // absolute generic path -> version
    std::shared_ptr<SnapshotChannel> channel_ = std::make_shared<SnapshotChannel>();
};

} // namespace noor::editor
