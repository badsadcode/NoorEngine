#pragma once
// Internal to noor_editor_api: an editor document (ADR-025). Owns an open project, its asset
// registry, one open scene with its undo history, and publishes render snapshots for viewports.

#include "noor/asset/file_watcher.h"
#include "noor/asset/json.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"
#include "noor/game/game.h"
#include "noor/runtime/world.h"
#include "noor/scene/history.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"
#include "noor/procgraph/procgraph.h"
#include "noor/terrain/terrain.h"
#include "noor/terrain_edit/brush.h"

#include <atomic>
#include <thread>

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

// A CSG model built from its brushes (ADR-016), in the model's space; shared between snapshots
// while the brushes don't change.
struct CsgMesh {
    std::uint64_t key = 0;  // what it was built from (brushes, transforms, materials)
    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    // One group per brush that has triangles in the result: its runs (one per material slot).
    struct Part {
        std::uint32_t first_index = 0, index_count = 0, slot = 0;
    };
    struct Group {
        Guid brush;
        std::vector<Part> parts;
    };
    std::vector<Group> groups;
    std::vector<std::filesystem::path> materials;  // per slot; empty: the default material
    std::vector<std::optional<Guid>> material_ids;  // per slot (what cooked files and exports name)
    // Subtract and intersect brushes as see-through shapes, so they can be seen and clicked.
    struct Ghost {
        Guid brush;
        bool intersect = false;
        std::vector<math::Vec3> positions;  // triangle list
    };
    std::vector<Ghost> ghosts;
    // Which material each brush face ended up with, and how many triangles it has in the result.
    struct FaceUse {
        Guid brush;
        std::uint32_t face = 0, slot = 0, triangles = 0;
    };
    std::vector<FaceUse> faces;
    std::vector<std::uint32_t> triangle_face;  // per result triangle: its brush's logical face
    std::size_t brush_count = 0;
    std::size_t triangles = 0;
    double volume = 0.0;
    double build_ms = 0.0;
    std::vector<std::string> warnings;
};

// A terrain's hand-made layers while the editor has it open (Milestone 6, parts 6 and 7): its sculpt
// offsets and its paint weights, one edit per terrain entity. The layers here are the truth until
// saved. A layer's asset is remembered even while the component doesn't name it (an undone first
// stroke), so redo finds the same data.
struct TerrainEdit {
    Guid entity;
    std::optional<Guid> sculpt_asset, paint_asset;
    std::filesystem::path sculpt_file, paint_file;
    terrain::SculptLayer layer;
    terrain::PaintLayer paint;
    bool sculpt_active = false, paint_active = false;  // the component names the asset right now
    bool sculpt_dirty = false, paint_dirty = false;    // changed since saved
    std::string base_key;                            // settings of the generated base it sits on
    std::shared_ptr<const terrain::Heights> base;
    std::shared_ptr<terrain::Heights> current;       // base + layers (only the document writes it)
    std::shared_ptr<const terrain::Heights> published;  // a frozen copy of `current` for viewports
    std::vector<std::uint64_t> tile_versions;        // per tile, row by row: changes with its heights or weights
    std::uint64_t revision = 1, published_revision = 0;
    // Set a layer's values in a rectangle (row by row) and update the heights/weights there.
    void write(const terrain::SampleRect& rect, const std::vector<float>& offsets);
    void write(const terrain::SampleRect& rect, const std::vector<std::uint32_t>& weights);
    // The layers changed in `rect`: recompose and bump the tiles it (and its normals) touch.
    void touched(const terrain::SampleRect& rect);
    void rebuild();  // everything, from the base and the active layers
    [[nodiscard]] std::shared_ptr<const terrain::Heights> snapshot();
};

// What a viewport needs to draw the document's scene, immutable once published.
struct RenderSnapshot {
    struct Mesh {
        std::string asset_path;  // absolute path of the GLB
        int mesh_index = -1;
        math::Mat4 world;
        Guid entity;             // expanded entity id
        std::uint64_t asset_version = 0;  // bumps when the file changes on disk: reload it
        Guid model_root;         // the placed model it belongs to (levels of detail switch it as a whole), nil if none
    };
    // Placed models (Milestone 7, part 6): levels of detail are chosen per model and frame.
    struct Model {
        Guid entity;
        std::string asset_path;
        math::Mat4 world;
        std::uint64_t asset_version = 0;
    };
    std::vector<Model> models;
    // Scattered instances (Milestone 7, part 7), one entry per scatter entity and model file.
    struct Scattered {
        Guid entity;
        std::string asset_path;
        std::uint64_t asset_version = 0;
        std::shared_ptr<const std::vector<math::Mat4>> worlds;  // shared with the placement cache
        float draw_distance = 0.0f;  // 0: no limit
        bool cast_shadows = true;
    };
    std::vector<Scattered> scattered;
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
        std::optional<math::Vec3> color;  // lights: their colour
    };
    std::vector<Marker> markers;
    using Primitive = game::SceneView::Primitive;  // simple shapes from `primitive` components
    std::vector<Primitive> primitives;
    using Sprite = game::SceneView::Sprite;  // `sprite` components (8B part 5): billboards turned to the viewport's camera
    std::vector<Sprite> sprites;
    using GameCamera = game::SceneView::Camera;    // while playing: the first entity with a `camera`
    std::optional<GameCamera> camera;
    render::Lighting lighting;  // the scene's sun and exposure (game::SceneView), also while editing
    game::SceneView::Environment environment;
    std::filesystem::path texture_cache;  // <project>/.noor/cache/textures (decoded textures)
    bool show_grid = true;            // false: the material preview (no ground grid)
    std::uint64_t reload_token = 0;   // changed: reload materials and textures now
    // Every asset's file by id (materials find their textures and parents with it).
    std::shared_ptr<const std::unordered_map<Guid, std::filesystem::path>> asset_files;
    // CSG models (entities with `csg_model` and `brush` children).
    struct Csg {
        Guid model;
        math::Mat4 world;
        std::shared_ptr<const CsgMesh> mesh;
    };
    std::vector<Csg> csg;
    using Terrain = game::SceneView::Terrain;  // generated in the viewport from their settings
    std::vector<Terrain> terrains;
    // While playing: what scripts drew on the HUD this frame (noor.ui / Ui).
    std::vector<runtime::UiCommand> ui;
    // Effect particles (Milestone 8B, part 4): the emitters the viewport previews, settings resolved (asset and
    // overrides), with the scene's wind; and how the Effects panel wants them previewed.
    struct Emitter {
        Guid entity;
        asset::Json settings;
        math::Mat4 world;
    };
    std::vector<Emitter> emitters;
    math::Vec3 wind{0, 0, 0};
    std::optional<Guid> preview_solo;    // only this emitter is shown
    std::vector<Guid> preview_muted;     // these aren't
    std::uint64_t preview_restart = 0;   // changed: every preview emitter starts again from nothing
    bool preview_paused = false;
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
    ~Document();

    [[nodiscard]] asset::Json project_info() const;
    [[nodiscard]] Result<asset::Json> assets(bool rescan);
    // A .noormat from the texture set a texture belongs to (asset::find_texture_set), beside it.
    [[nodiscard]] Result<asset::Json> make_material(std::string_view texture_path);
    // Environment assets (Milestone 8A, part 6): {"op": "list" | "apply" | "duplicate" | "reset" | "read" | "save", ...}
    // (noor_doc_environment in noor_editor_api.h).
    [[nodiscard]] Result<asset::Json> environment(const asset::Json& command);
    // Effect assets (Milestone 8B, part 4): {"op": "list" | "read" | "save" | "duplicate" | "reset" | "apply" |
    // "preview", ...} (noor_doc_effect in noor_editor_api.h).
    [[nodiscard]] Result<asset::Json> effect(const asset::Json& command);
    // Sprite sheets (8B part 5): list, read (sliced), create from a texture, save, apply (noor_doc_sprite).
    [[nodiscard]] Result<asset::Json> sprite(const asset::Json& command);
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
    // refused while playing; Stop throws the runtime copy away. tick() may run on a viewport's
    // render thread (noor_viewport_set_play) while the UI thread reads the scene: play, stop, tick,
    // scene_snapshot and take_mouse_capture_request take play_mutex_.
    [[nodiscard]] Status play();
    void stop();
    [[nodiscard]] bool playing() const noexcept { return world_ != nullptr; }
    void tick(double seconds, const runtime::InputState& input = {});
    // The managed C# script host (noor_rt_host) for play mode; null: C# scripts are skipped.
    void set_script_host(const void* host) noexcept { script_host_ = host; }
    // Play sounds through the speakers (default) or mix silently (tests).
    void set_audio_device(bool enabled) noexcept { audio_device_ = enabled; }
    // A script asked to capture (true) or release (false) the mouse this frame.
    [[nodiscard]] std::optional<bool> take_mouse_capture_request() {
        std::scoped_lock lock(play_mutex_);
        return world_ ? world_->take_mouse_capture_request() : std::nullopt;
    }

    // Checks watched asset files (models, prefabs) for changes on disk. Changed files get a new
    // version in the render snapshot (viewports reload them) and prefabs are re-read.
    // Returns the project-relative paths that changed.
    [[nodiscard]] asset::Json poll_files();

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::shared_ptr<SnapshotChannel> channel() const noexcept { return channel_; }
    // The material editor's preview: one shape wearing a material, under an environment.
    [[nodiscard]] std::shared_ptr<SnapshotChannel> preview_channel() const noexcept { return preview_channel_; }
    [[nodiscard]] Status set_preview(const asset::Json& settings);
    // A texture's import settings and what they make of it; and changing them (Milestone 5).
    [[nodiscard]] Result<asset::Json> texture(std::string_view texture_path);
    [[nodiscard]] Status set_texture_settings(std::string_view texture_path, const asset::Json& settings);
    // The CSG models of the open scene: [{"model", "brushes", "triangles", "volume", "build_ms", "warnings"}].
    [[nodiscard]] asset::Json csg_info() const;
    // Writes a CSG model as a Noor Interchange GLB + manifest (Milestone 6, part 4).
    [[nodiscard]] Result<asset::Json> export_csg(const Guid& model, const std::filesystem::path& glb);
    // Sculpting or painting a terrain (Milestone 6, parts 6-7): {"terrain": id, "phase": "begin" |
    // "dab" | "end", "tool": "raise" | "lower" | "smooth" | "flatten" | "paint", "material": id (paint),
    // "radius", "strength", "falloff", "dabs": [{"point": [x, y, z] (world), "seconds"}]}. A stroke
    // (begin .. end) is one undo step.
    [[nodiscard]] Result<asset::Json> sculpt(const asset::Json& command);
    // The ground height of a terrain entity at world (x, z), with its sculpting (saved or not).
    [[nodiscard]] Result<float> terrain_height(const Guid& terrain, float x, float z);
    // Running a procedural graph in the background (Milestone 7, part 2), one at a time: the same
    // procgraph::generate as `noor_cli generate`, so the same bytes. `out` empty: <project>/Generated/<stem>.
    [[nodiscard]] Status generate_start(std::string_view graph, const std::filesystem::path& out);
    [[nodiscard]] asset::Json generate_status();
    void generate_cancel();
    // A preview of one node output of the last run: fields as an 8-bit greyscale picture (base64), numbers as a value.
    [[nodiscard]] Result<asset::Json> generate_preview(std::string_view node, std::string_view pin, int size);
    // Everything known about one asset (Milestone 7, part 5): its file, and for a model with a Noor
    // Interchange manifest, the manifest (generator, recipe, seed, family, wind...) and each level of
    // detail opened and compared with it. Cached until the files change.
    [[nodiscard]] Result<asset::Json> asset_details(const Guid& asset);
    // Hand-editing scattered instances (Milestone 7, part 7): {"scatter": entity id, "phase": "begin" |
    // "dab" | "end", "tool": "plant" | "remove", "radius", "asset" (plant: the model; default: the set's
    // first), "dabs": [{"point": [x, y, z] (world)}]}. A stroke (begin .. end) is one undo step; the edits
    // go into the component's "removed" and "added" lists, which regenerating the graph never touches.
    [[nodiscard]] Result<asset::Json> scatter_edit(const asset::Json& command);

private:
    struct ScatterStroke {
        Guid entity;
        bool plant = true;
        float radius = 4.0f;
        std::optional<Guid> asset;
        std::optional<math::Vec3> last;  // where the last tree was planted (one per `radius` along a drag)
        std::size_t planted = 0, removed = 0;
    };
    std::optional<ScatterStroke> scatter_stroke_;
    struct DetailsCache {
        std::string key;  // the files' sizes and times: a change makes it stale
        asset::Json json;
    };
    std::unordered_map<Guid, DetailsCache> details_;
    struct GenerateJob {
        std::thread thread;
        std::atomic<bool> cancel{false};
        std::mutex mutex;
        asset::Json status;
        std::shared_ptr<const procgraph::Evaluation> evaluation;  // the last finished run (previews)
    };
    std::unique_ptr<GenerateJob> job_;
    std::unique_ptr<procgraph::Cache> graph_cache_;  // <project>/.noor/cache/procgraph, shared by runs
    // Terrains' graphs cooked for the scene (Milestone 7, part 3): by graph id, the graph file's time when
    // last cooked. Cooked again when the file changes, and on every save.
    std::unordered_map<Guid, std::filesystem::file_time_type> graphs_cooked_;
    void cook_graphs(const scene::Scene& expanded, bool force);
    // Terrains with hand-made layers, by entity (loaded on first use, saved with the scene).
    std::unordered_map<Guid, std::shared_ptr<TerrainEdit>> terrain_edits_;
    // The live edit of a terrain entity with these layer assets and settings (rebuilt on a new base).
    std::shared_ptr<TerrainEdit> terrain_edit(const Guid& entity, const std::optional<Guid>& sculpt, const std::optional<Guid>& paint,
                                              const terrain::Settings& settings);
    struct Stroke {
        Guid entity;
        std::shared_ptr<TerrainEdit> edit;
        terrain_edit::Brush brush;
        bool painting = false;
        std::uint32_t layer = 0;       // painting: the material layer
        math::Vec3 position;           // the terrain's centre in the world
        terrain::SculptLayer before;   // the layer when the stroke began (for undo)
        terrain::PaintLayer before_paint;
        std::optional<terrain::SampleRect> changed;
        bool first_dab = true;
        math::Vec2 last;  // the previous dab (terrain space): fast pointer moves are filled in from it
    };
    std::optional<Stroke> stroke_;
    void finish_stroke();  // an unfinished stroke becomes its undo step (before other edits)
    void save_terrain_layers();
    void apply_dabs(const asset::Json& dabs);
    // A new layer file for a terrain (Assets/Terrain/<name><extension>), registered; its asset id.
    Result<Guid> new_layer_file(const std::string& name, std::string_view extension, const terrain::Settings& settings, bool paint);
    // CSG models built for snapshots, by model entity (rebuilt only when their brushes change).
    std::unordered_map<Guid, std::shared_ptr<const CsgMesh>> csg_cache_;
    std::vector<std::pair<Guid, std::shared_ptr<const CsgMesh>>> csg_last_;  // models in the last published snapshot
    std::shared_ptr<const CsgMesh> build_csg(const scene::Scene& expanded, std::size_t model);
    // Writes the cooked file of every CSG model in the open scene whose file is missing or stale
    // (on save and on Play: what games and Play load; noor/game/cooked_csg.h).
    void cook_csg();
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
    // The Effects panel's preview controls (8B part 4), published in every snapshot.
    std::optional<Guid> preview_solo_;
    std::vector<Guid> preview_muted_;
    std::uint64_t preview_restart_ = 0;
    bool preview_paused_ = false;
    const void* script_host_ = nullptr;      // noor_rt_host*
    mutable std::recursive_mutex play_mutex_;  // world_ (see play())
    bool audio_device_ = true;
    asset::FileWatcher watcher_;
    std::unordered_map<std::string, std::uint64_t> asset_versions_;  // absolute generic path -> version
    std::shared_ptr<SnapshotChannel> channel_ = std::make_shared<SnapshotChannel>();
    std::shared_ptr<SnapshotChannel> preview_channel_ = std::make_shared<SnapshotChannel>();
    std::shared_ptr<const std::unordered_map<Guid, std::filesystem::path>> asset_files() const;
};

} // namespace noor::editor
