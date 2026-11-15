#pragma once
// Internal to noor_editor_api: an editor document (ADR-025). Owns an open project, its asset
// registry, one open scene with its undo history, and publishes render snapshots for viewports.

#include "noor/asset/json.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"
#include "noor/scene/history.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace noor::editor {

// What a viewport needs to draw the document's scene, immutable once published.
struct RenderSnapshot {
    struct Mesh {
        std::string asset_path;  // absolute path of the GLB
        int mesh_index = -1;
        math::Mat4 world;
        Guid entity;             // expanded entity id
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

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::shared_ptr<SnapshotChannel> channel() const noexcept { return channel_; }

private:
    explicit Document(asset::Project project);
    [[nodiscard]] Status require_scene() const;
    [[nodiscard]] Status add_asset(const asset::Json& command);
    void changed();  // bumps the revision and republishes the render snapshot

    asset::Project project_;
    asset::AssetRegistry registry_;
    mutable scene::PrefabLibrary prefabs_;  // a cache of prefab files: filling it isn't a change
    std::optional<scene::Scene> scene_;
    std::filesystem::path scene_file_;
    scene::History history_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<SnapshotChannel> channel_ = std::make_shared<SnapshotChannel>();
};

} // namespace noor::editor
