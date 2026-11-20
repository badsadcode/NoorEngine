// Editor viewport: native child window + dedicated render thread.
//
// The UI thread owns the child window (Win32 rule). The render thread owns all Vulkan
// objects; it creates the renderer, draws until asked to stop, then destroys it. While
// the UI thread waits for the render thread to finish, it keeps pumping messages,
// because presentation may need the window's thread to respond.
//
// With a document attached (ADR-025) the viewport draws the document's scene: it picks up
// each published RenderSnapshot, loads the GLBs it references (on the render thread) and
// draws every mesh with its world matrix. Camera: hold the right button to look and fly (WASD,
// Q/E, Shift), Alt + left-drag orbits, middle-drag pans, the wheel zooms. Left click picks (reported as an event), and a gizmo on the selected entity can
// be dragged: the drag is previewed here and reported as an event on release. The viewport
// never edits the document; the editor turns events into undoable commands.
// Without a document it shows the spike's input demo (hue follows the pointer).

#include "noor_editor_api.h"

#include "api_internal.h"
#include "document.h"
#include "viewport_tools.h"
#include "noor/asset/gltf_import.h"
#include "noor/asset/json.h"
#include "noor/foundation/log.h"
#include "noor/game/geometry.h"
#include "noor/game/hud.h"
#include "noor/game/model_lod.h"
#include "noor/game/particle_draws.h"
#include "noor/game/sprites.h"
#include "noor/asset/skinning.h"
#include "noor/asset/model_import.h"
#include "noor/game/particle_ground.h"
#include "noor/game/scene_renderer.h"
#include "noor/game/texture_library.h"
#include "noor/game/material_library.h"
#include "noor/game/environment_library.h"
#include "noor/terrain/terrain.h"
#include "noor/platform/native_child_window.h"
#include "noor/render/renderer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace std::chrono_literals;
using noor::Guid;
using noor::math::Mat4;
using noor::math::Quat;
using noor::math::Vec3;
using noor::math::Vec4;
namespace ed = noor::editor;

struct noor_viewport {
    std::unique_ptr<noor::platform::NativeChildWindow> window;
    std::thread render_thread;
    std::atomic<bool> stop{false};
    bool enable_validation = true;
    std::chrono::steady_clock::time_point created_at;

    std::mutex stats_mutex;
    noor_viewport_stats stats{};

    // Set from the UI thread, read by the render thread.
    std::mutex doc_mutex;
    std::shared_ptr<ed::SnapshotChannel> channel;
    std::optional<std::vector<Guid>> frame_request;  // empty vector = frame everything
    std::vector<Guid> selection;
    std::optional<Guid> gizmo_entity;
    ed::GizmoMode gizmo_mode = ed::GizmoMode::None;
    std::uint64_t selection_version = 0;
    struct Options {
        bool snap = false;
        float move_step = 1.0f, rotate_step = 15.0f, scale_step = 0.1f;
        float fly_speed = 8.0f, look_sensitivity = 1.0f, gizmo_scale = 1.0f;
        std::uint32_t msaa = 4;
    } options;
    std::string render_stats = "{}";  // JSON (noor_viewport_render_stats), under stats_mutex
    std::optional<Guid> face_brush;  // the outlined brush face (noor_viewport_set_face_highlight), under doc_mutex
    std::uint32_t face_index = 0;
    std::uint64_t face_version = 0;
    std::uint64_t options_version = 0;
    // The sculpt brush (noor_viewport_set_brush), under doc_mutex: the terrain it works on, its size.
    std::optional<Guid> brush_terrain;
    float brush_radius = 8.0f;

    // The camera of the last drawn frame (for noor_viewport_world_to_screen).
    std::mutex camera_mutex;
    std::optional<ed::CameraBasis> camera;
    float camera_width = 0, camera_height = 0;

    // Game input bookkeeping (whoever calls noor_viewport_game_input: see api_internal.h).
    std::int32_t last_game_mouse_x = 0, last_game_mouse_y = 0;
    bool game_mouse_valid = false;

    // Play mode ticked here, once per frame before drawing (noor_viewport_set_play). play_mutex is
    // held for each tick, so set_play(nullptr) returns only once no tick is running.
    std::mutex play_mutex;
    noor_doc* play_doc = nullptr;
    std::chrono::steady_clock::time_point last_play_tick{};
    bool play_ticked = false;

    // Written by the render thread, drained by the UI thread.
    std::mutex event_mutex;
    noor::asset::Json events = noor::asset::Json::array();
};

namespace {

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

Vec3 srgb(std::uint32_t rgb) {
    return {srgb_to_linear(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f),
            srgb_to_linear(static_cast<float>((rgb >> 8) & 0xFF) / 255.0f),
            srgb_to_linear(static_cast<float>(rgb & 0xFF) / 255.0f)};
}

Vec4 tint(std::uint32_t rgb, float amount) {
    const Vec3 c = srgb(rgb);
    return {c.x, c.y, c.z, amount};
}

// HSV (h in degrees) to sRGB.
std::array<float, 3> hsv_to_srgb(float h, float s, float v) {
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    return {r + m, g + m, b + m};
}

// Spike input demo (no document): Noor purple at rest; hovering shifts the hue with the
// pointer's x position, and holding the left button brightens it.
std::array<float, 4> demo_clear_color(const noor::platform::PointerState& p, noor::platform::PixelSize size) {
    std::array<float, 3> c{0x6B / 255.0f, 0x3F / 255.0f, 0xD9 / 255.0f};
    if (p.inside && size.width > 0) {
        const float t = std::clamp(static_cast<float>(p.x) / static_cast<float>(size.width), 0.0f, 1.0f);
        c = hsv_to_srgb(t * 359.0f, 0.71f, p.left_down ? 1.0f : 0.85f);
    }
    return {srgb_to_linear(c[0]), srgb_to_linear(c[1]), srgb_to_linear(c[2]), 1.0f};
}

using noor::game::Geometry;
using noor::game::make_box;
using noor::game::make_round;

// Gizmo arrow along +Y from 0 to 1: a thin shaft and a cone tip.
Geometry make_arrow() {
    constexpr int kSides = 16;
    Geometry g;
    auto ring = [&](float y, float r, float ny) {
        const auto base = static_cast<std::uint32_t>(g.first.size());
        for (int i = 0; i < kSides; ++i) {
            const float a = 2.0f * noor::math::kPi * static_cast<float>(i) / kSides;
            const Vec3 n = noor::math::normalize(Vec3{std::cos(a), ny, std::sin(a)});
            g.first.push_back({{std::cos(a) * r, y, std::sin(a) * r}, n, {1, 1, 1}});
        }
        return base;
    };
    auto connect = [&](std::uint32_t a, std::uint32_t b) {
        for (std::uint32_t i = 0; i < kSides; ++i) {
            const std::uint32_t j = (i + 1) % kSides;
            for (const std::uint32_t k : {a + i, b + i, b + j, a + i, b + j, a + j}) g.second.push_back(k);
        }
    };
    const auto s0 = ring(0.0f, 0.018f, 0.0f), s1 = ring(0.78f, 0.018f, 0.0f);
    connect(s0, s1);
    const auto c0 = ring(0.78f, 0.065f, 0.35f);
    const auto tip = static_cast<std::uint32_t>(g.first.size());
    g.first.push_back({{0, 1, 0}, {0, 1, 0}, {1, 1, 1}});
    for (std::uint32_t i = 0; i < kSides; ++i) {
        for (const std::uint32_t k : {c0 + i, tip, c0 + (i + 1) % kSides}) g.second.push_back(k);
    }
    return g;
}

// Scale handle along +Y: a thin shaft with a cube at the tip.
Geometry make_scale_handle() {
    Geometry g = make_box({1, 1, 1}, {-0.012f, 0.0f, -0.012f}, {0.012f, 0.9f, 0.012f});
    const Geometry tip = make_box({1, 1, 1}, {-0.05f, 0.9f, -0.05f}, {0.05f, 1.0f, 0.05f});
    const auto base = static_cast<std::uint32_t>(g.first.size());
    g.first.insert(g.first.end(), tip.first.begin(), tip.first.end());
    for (const auto i : tip.second) g.second.push_back(base + i);
    return g;
}

// Gizmo ring around the Y axis.
Geometry make_ring(float radius, float tube) {
    constexpr int kMajor = 72, kMinor = 8;
    Geometry g;
    for (int i = 0; i < kMajor; ++i) {
        const float a = 2.0f * noor::math::kPi * static_cast<float>(i) / kMajor;
        const Vec3 dir{std::cos(a), 0, std::sin(a)};
        for (int j = 0; j < kMinor; ++j) {
            const float b = 2.0f * noor::math::kPi * static_cast<float>(j) / kMinor;
            const Vec3 n = dir * std::cos(b) + Vec3{0, std::sin(b), 0};
            g.first.push_back({dir * radius + n * tube, n, {1, 1, 1}});
        }
    }
    for (int i = 0; i < kMajor; ++i) {
        for (int j = 0; j < kMinor; ++j) {
            const auto a = static_cast<std::uint32_t>(i * kMinor + j);
            const auto b = static_cast<std::uint32_t>(((i + 1) % kMajor) * kMinor + j);
            const auto c = static_cast<std::uint32_t>(((i + 1) % kMajor) * kMinor + (j + 1) % kMinor);
            const auto d = static_cast<std::uint32_t>(i * kMinor + (j + 1) % kMinor);
            for (const std::uint32_t k : {a, c, b, a, d, c}) g.second.push_back(k);
        }
    }
    return g;
}

// Maps the gizmo's +Y-built parts onto an axis.
Mat4 axis_frame(ed::Handle h) {
    const Vec3 a = ed::handle_axis(h);
    if (a.x > 0.5f) return noor::math::rotation({0, 0, 1}, -noor::math::kPi * 0.5f);
    if (a.z > 0.5f) return noor::math::rotation({1, 0, 0}, noor::math::kPi * 0.5f);
    return Mat4::identity();
}

std::uint32_t handle_color(ed::Handle h) {
    switch (h) {
    case ed::Handle::MoveX:
    case ed::Handle::RotateX:
    case ed::Handle::ScaleX: return 0xF06A6A;
    case ed::Handle::MoveZ:
    case ed::Handle::RotateZ:
    case ed::Handle::ScaleZ: return 0x6AA8F0;
    case ed::Handle::MoveXZ: return 0xFBBF24;
    case ed::Handle::ScaleAll: return 0xE7E3F2;
    default: return 0x7BD88F;
    }
}

struct LoadedMesh {
    noor::render::MeshHandle handle;
    Vec3 lo, hi;  // local bounds
    std::vector<Vec3> positions;  // CPU copy for picking
    std::vector<std::uint32_t> indices;
    // Models: one draw per glTF primitive with its material (empty: the whole mesh, one material).
    struct Part {
        std::uint32_t first_index = 0, index_count = 0;
        noor::render::MaterialHandle material;
    };
    std::vector<Part> parts;
    std::vector<std::uint32_t> triangle_face;  // CSG brush groups: the logical face of each triangle in `indices`
    std::vector<noor::asset::SkinWeights> skin;  // skinned meshes (8C): per vertex, for posing `positions` (picking, framing)
};

void set_state(noor_viewport* vp, noor_viewport_state state) {
    std::scoped_lock lock(vp->stats_mutex);
    vp->stats.state = state;
}

void push_event(noor_viewport* vp, noor::asset::Json event) {
    std::scoped_lock lock(vp->event_mutex);
    vp->events.push_back(std::move(event));
}

noor::asset::Json transform_json(const noor::scene::Transform& t) {
    return {{"position", {t.position.x, t.position.y, t.position.z}},
            {"rotation", {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w}},
            {"scale", {t.scale.x, t.scale.y, t.scale.z}}};
}

void grow(Vec3& lo, Vec3& hi, Vec3 p) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
}

// A gizmo drag in progress, or one just released whose result the document hasn't shown yet.
struct Preview {
    Guid entity;
    Mat4 delta = Mat4::identity();  // applied to the world matrices of the entity's subtree
    const ed::RenderSnapshot* shown_at_release = nullptr;
    std::chrono::steady_clock::time_point released{};
};

void render_thread_main(noor_viewport* vp) {
    auto renderer = noor::render::Renderer::create(*vp->window, {.enable_validation = vp->enable_validation});
    if (!renderer) {
        NOOR_LOG_ERROR("Viewport", "Renderer creation failed: {}", renderer.error().describe());
        set_state(vp, NOOR_VIEWPORT_FAILED);
        return;
    }
    {
        std::scoped_lock lock(vp->stats_mutex);
        const std::string& name = (*renderer)->gpu_info().name;
        std::strncpy(vp->stats.gpu_name, name.c_str(), sizeof(vp->stats.gpu_name) - 1);
        vp->stats.state = NOOR_VIEWPORT_RUNNING;
    }

    auto& r = **renderer;
    auto upload = [&](const Geometry& g, std::string_view name) {
        auto h = r.create_mesh({g.first, g.second, name});
        return h ? *h : noor::render::MeshHandle{};
    };
    const auto ground = upload(make_box(srgb(0x26222F)), "Editor ground");
    const auto marker = upload(make_box(srgb(0x22D3EE)), "Editor marker");
    const auto arrow = upload(make_arrow(), "Gizmo arrow");
    const auto ring = upload(make_ring(ed::kRingRadius, 0.012f), "Gizmo ring");
    const auto plane = upload(make_box({1, 1, 1}, {ed::kPlaneMin, -0.004f, ed::kPlaneMin}, {ed::kPlaneMax, 0.004f, ed::kPlaneMax}), "Gizmo plane");
    const auto hub = upload(make_box({1, 1, 1}, {-0.04f, -0.04f, -0.04f}, {0.04f, 0.04f, 0.04f}), "Gizmo center");
    const auto scale_handle = upload(make_scale_handle(), "Gizmo scale handle");
    const float cube = ed::kScaleAllRadius * 0.8f;
    const auto scale_all = upload(make_box({1, 1, 1}, {-cube, -cube, -cube}, {cube, cube, cube}), "Gizmo uniform scale");

    // Primitive shapes (`primitive` components), with CPU copies for picking.
    auto make_loaded = [&](const Geometry& g, std::string_view name) {
        LoadedMesh lm{upload(g, name), {1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, {}, {}};
        for (const auto& v : g.first) {
            lm.positions.push_back(v.position);
            grow(lm.lo, lm.hi, v.position);
        }
        lm.indices = g.second;
        return lm;
    };
    const LoadedMesh prim_box = make_loaded(make_box({1, 1, 1}, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}), "Primitive box");
    const LoadedMesh prim_sphere = make_loaded(make_round(0.5f, 1.0f), "Primitive sphere");
    std::map<std::pair<int, int>, LoadedMesh> prim_capsules;  // by (radius, height) in millimetres

    // Loaded models by absolute path. A model is reloaded when the snapshot carries a newer
    // version of it (the document saw the file change on disk).
    struct CachedAsset {
        bool loaded = false;
        std::uint64_t version = 0;
        std::optional<std::vector<LoadedMesh>> meshes;  // nullopt: failed to load
        std::vector<std::pair<int, Mat4>> nodes;        // drawn nodes (mesh, world in the file): levels of detail
        std::vector<noor::asset::ImportedSkin> skins;   // skinned meshes' skeletons (8C)
    };
    std::unordered_map<std::string, CachedAsset> assets;
    // Levels of detail (Milestone 7, part 6): per model file and version, and this snapshot's placed models.
    std::unordered_map<std::string, noor::game::ModelLods> lod_cache;
    struct ViewModel {
        Guid entity;
        std::string path;
        std::uint64_t version = 0;
        Mat4 world;
        const noor::game::ModelLods* lods = nullptr;
    };
    std::vector<ViewModel> view_models;
    std::unordered_map<Guid, bool> model_selected;  // per model root: one of its draws is selected
    // CSG models on the GPU, by build key (ADR-016): one mesh per model; per brush a LoadedMesh
    // that shares it (its own parts and, for picking, its triangles); see-through ghosts for
    // subtract and intersect brushes. Unused builds are dropped after each new snapshot.
    struct CsgGpu {
        noor::render::MeshHandle handle;
        std::vector<LoadedMesh> groups;
        std::vector<LoadedMesh> ghosts;
    };
    std::unordered_map<std::uint64_t, CsgGpu> csg_gpu;
    // Terrains on the GPU, by entity: rebuilt when their settings change.
    struct TerrainGpu {
        std::string key;
        std::vector<std::uint64_t> versions;  // per tile: what it was built from (live heights)
        std::vector<LoadedMesh> tiles;
        std::shared_ptr<const noor::terrain::Heights> heights;  // what the tiles show (for the brush)
        Vec3 position{};
    };
    std::unordered_map<Guid, TerrainGpu> terrain_gpu;
    // Sculpting: a left-drag on the brush's terrain is a stroke; the circle shows where it lands.
    bool stroking = false;
    std::optional<Vec3> brush_hit;
    noor::render::MaterialHandle ghost_subtract, ghost_intersect, face_material;
    noor::render::MeshHandle face_mesh;     // the highlighted face, lifted 3 mm off the surface
    Mat4 face_world = Mat4::identity();
    std::uint64_t seen_face_version = ~0ull;
    std::shared_ptr<const ed::RenderSnapshot> face_snapshot;
    std::shared_ptr<const ed::RenderSnapshot> shown;
    std::optional<noor::game::Hud> hud;  // built on first use (bakes the font atlas)
    std::optional<noor::game::TextureLibrary> textures;    // textures and materials, made with the first snapshot
    std::optional<noor::game::MaterialLibrary> materials;
    std::optional<noor::game::EnvironmentLibrary> environment;
    std::optional<noor::game::SpriteLibrary> sprites;  // sprite sheets (8B part 5); billboards are placed every frame
    std::vector<std::pair<Guid, Mat4>> sprite_quads;   // last frame's sprites (entity, quad): picking
    auto refresh_clock = std::chrono::steady_clock::now();
    const auto started_at = std::chrono::steady_clock::now();  // the clouds' clock
    // Effect particles while editing (Milestone 8B, part 4): the viewport's own preview of the snapshot's emitters,
    // stepped at a fixed 60 Hz on the viewport's clock (paused, solo, mute and restart from the Effects panel).
    noor::fx::EmitterSet preview_emitters;
    noor::game::ParticleDrawStorage preview_draws;
    noor::game::ParticleGround preview_ground;  // the terrains preview particles collide with (part 7)
    const ed::RenderSnapshot* preview_ground_snapshot = nullptr;
    double preview_carry = 0.0;
    auto preview_clock = std::chrono::steady_clock::now();
    std::uint64_t preview_restart_seen = 0;
    std::uint64_t preview_particles = 0;  // for the render stats
    std::uint64_t seen_reload_token = 0;
    auto last_stats_time = std::chrono::steady_clock::now() - 1s;
    std::vector<noor::render::OverlayVertex> overlay;

    // Per snapshot: what to draw, and for each draw the entity it belongs to.
    struct SceneDraw {
        noor::render::DrawItem item;
        Guid entity;
        const LoadedMesh* mesh = nullptr;  // null for markers
        bool colored = false;              // primitives: item.tint is their color
        Guid model_root;                   // models: the placed model (a lower level of detail replaces it)
    };
    std::vector<SceneDraw> scene_draws;
    // Skinned meshes in this snapshot (8C): their palettes (the draws point at them) and their posed vertices, in the
    // world, for picking and framing. Rebuilt with each snapshot.
    std::deque<std::vector<Mat4>> snapshot_palettes;
    std::deque<LoadedMesh> posed_meshes;
    std::unordered_map<Guid, const ed::RenderSnapshot::Entity*> entity_by_id;
    std::unordered_map<Guid, std::vector<Guid>> children;
    Vec3 scene_lo{-10, 0, -10}, scene_hi{10, 10, 10};
    bool framed_once = false;
    ed::EditorCamera camera;
    bool alt_orbit = false;  // this left-drag orbits instead of picking
    auto last_frame_time = std::chrono::steady_clock::now();

    // Selection/gizmo state as last read from the UI thread, plus derived per-draw flags.
    std::vector<Guid> selection;
    std::optional<Guid> gizmo_entity;
    ed::GizmoMode gizmo_mode = ed::GizmoMode::None;
    std::uint64_t seen_selection_version = ~0ull;
    std::uint64_t seen_options_version = ~0ull;
    noor_viewport::Options options;
    std::vector<char> draw_selected, draw_in_gizmo_subtree;
    bool flags_dirty = true;

    // Pointer interaction.
    std::optional<ed::GizmoDrag> drag;
    Vec3 drag_center{};  // gizmo centre when the drag started (snapping is relative to it)
    // The result of the drag so far: recomputed every frame, used for the preview and on release.
    struct DragResult {
        Mat4 delta = Mat4::identity();
        noor::scene::Transform local;
        bool changed = false;
        const char* label = "Move";
    } drag_result;
    std::optional<Preview> preview;
    ed::Handle hovered = ed::Handle::None;
    bool click_pending = false;
    noor::platform::PointerState press{};

    noor::platform::PixelSize last_size = vp->window->pixel_size();
    noor::platform::PointerState last_pointer = vp->window->pointer_state();
    auto last_fps_time = std::chrono::steady_clock::now();
    std::uint64_t frames_at_last_fps = 0;
    double fps = 0.0;

    auto subtree_of = [&](const Guid& root) {
        std::unordered_set<Guid> set{root};
        std::vector<Guid> stack{root};
        while (!stack.empty()) {
            const Guid id = stack.back();
            stack.pop_back();
            if (auto it = children.find(id); it != children.end()) {
                for (const Guid& c : it->second)
                    if (set.insert(c).second) stack.push_back(c);
            }
        }
        return set;
    };

    // A model file on the GPU (meshes, materials, the nodes that draw them), loaded on first use and
    // again when the snapshot carries a newer version of it (the document saw the file change).
    auto load_asset = [&](const std::string& path, std::uint64_t version) -> CachedAsset& {
        CachedAsset& cached = assets.try_emplace(path).first->second;
        if (cached.loaded && cached.version == version) return cached;
        const bool reload = cached.loaded;
        if (cached.meshes) {
            for (const auto& old : *cached.meshes)
                if (old.handle.valid()) r.destroy_mesh(old.handle);
        }
        cached = {true, version, std::nullopt, {}, {}};
        if (auto model = noor::asset::import_model(path)) {
            std::vector<LoadedMesh> loaded;
            for (const auto& mesh : model->meshes) {
                auto h = r.create_mesh({{reinterpret_cast<const noor::render::Vertex*>(mesh.vertices.data()), mesh.vertices.size()},
                                        mesh.indices, mesh.name,
                                        {reinterpret_cast<const noor::render::SkinVertex*>(mesh.skin.data()), mesh.skin.size()}});
                LoadedMesh lm{h ? *h : noor::render::MeshHandle{}, {1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, {}, {}};
                lm.skin = mesh.skin;
                lm.positions.reserve(mesh.vertices.size());
                for (const auto& v : mesh.vertices) {
                    lm.positions.push_back(v.position);
                    grow(lm.lo, lm.hi, v.position);
                }
                lm.indices.assign(mesh.indices.begin(), mesh.indices.end());
                loaded.push_back(std::move(lm));
            }
            const auto model_materials = materials->model(path, *model);
            for (std::size_t mi = 0; mi < model->meshes.size(); ++mi)
                for (const auto& sm : model->meshes[mi].submeshes)
                    loaded[mi].parts.push_back({sm.first_index, sm.index_count,
                                                sm.material >= 0 && sm.material < static_cast<int>(model_materials.size())
                                                    ? model_materials[static_cast<std::size_t>(sm.material)]
                                                    : noor::render::MaterialHandle{}});
            cached.meshes = std::move(loaded);
            cached.skins = model->skins;
            for (std::size_t n = 0; n < model->nodes.size(); ++n)
                if (model->nodes[n].mesh >= 0 && !model->nodes[n].is_collision()) cached.nodes.emplace_back(model->nodes[n].mesh, model->world_matrix(n));
            if (reload) NOOR_LOG_INFO("Viewport", "Reloaded {}", std::filesystem::path(path).filename().string());
        } else {
            NOOR_LOG_WARN("Viewport", "{}", model.error().describe());
        }
        return cached;
    };

    while (!vp->stop.load()) {
        // Play mode: one game frame per drawn frame, just before drawing, so the game moves in step
        // with the screen (the UI thread's timers can't: they ticked ~35 times a second).
        {
            std::scoped_lock lock(vp->play_mutex);
            if (vp->play_doc) {
                const auto now = std::chrono::steady_clock::now();
                const double dt = vp->play_ticked ? std::chrono::duration<double>(now - vp->last_play_tick).count() : 1.0 / 60.0;
                vp->last_play_tick = now;
                vp->play_ticked = true;
                if (const auto want = noor_doc_tick_internal(vp->play_doc, dt, noor_viewport_game_input(vp)))
                    push_event(vp, {{"type", "mouse_capture"}, {"capture", *want}});  // the UI thread owns the cursor
            }
        }
        std::shared_ptr<ed::SnapshotChannel> channel;
        std::optional<std::vector<Guid>> frame_request;
        {
            std::scoped_lock lock(vp->doc_mutex);
            channel = vp->channel;
            frame_request = std::exchange(vp->frame_request, std::nullopt);
            if (vp->options_version != seen_options_version) {
                seen_options_version = vp->options_version;
                options = vp->options;
                camera.fly_speed = options.fly_speed;
                camera.turn_speed = 0.006f * options.look_sensitivity;
                if (options.msaa != r.msaa_samples()) r.set_msaa(options.msaa);
            }
            if (vp->selection_version != seen_selection_version) {
                seen_selection_version = vp->selection_version;
                selection = vp->selection;
                if (gizmo_entity != vp->gizmo_entity) drag.reset();
                gizmo_entity = vp->gizmo_entity;
                gizmo_mode = vp->gizmo_mode;
                flags_dirty = true;
            }
        }

        // New scene state? Load what it needs and rebuild the draw list.
        auto snap = channel ? channel->get() : nullptr;
        if (snap && !materials) {
            textures.emplace(r, snap->texture_cache);
            materials.emplace(r, *textures, noor::asset::AssetFileResolver{});
            environment.emplace(r, snap->texture_cache);
            sprites.emplace(r, *textures, noor::asset::AssetFileResolver{});
        }
        if (snap && environment) environment->apply(snap->environment);
        // A new reload token (the material editor saved): pick changed materials up now.
        if (snap && materials && snap->reload_token != 0 && snap->reload_token != seen_reload_token) {
            seen_reload_token = snap->reload_token;
            materials->refresh();
        }
        if (snap && materials && snap != shown) {
            const auto resolver = [files = snap->asset_files](const Guid& id) -> std::optional<std::filesystem::path> {
                if (!files) return std::nullopt;
                const auto it = files->find(id);
                return it == files->end() ? std::nullopt : std::optional(it->second);
            };
            materials->set_resolver(resolver);
            sprites->set_resolver(resolver);
        }
        // Edited material and texture files show up while the editor is open.
        if (materials && std::chrono::steady_clock::now() - refresh_clock > 500ms) {
            refresh_clock = std::chrono::steady_clock::now();
            materials->refresh();
            if (sprites) sprites->refresh();
        }
        if (snap != shown) {
            // The material preview frames its object when it first appears.
            if (!shown && snap && !snap->show_grid && !frame_request) frame_request.emplace();
            shown = snap;
            scene_draws.clear();
            snapshot_palettes.clear();
            posed_meshes.clear();
            entity_by_id.clear();
            children.clear();
            flags_dirty = true;
            bool any = false;
            Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            if (snap) {
                for (const auto& e : snap->entities) {
                    entity_by_id[e.id] = &e;
                    if (e.parent) children[*e.parent].push_back(e.id);
                }
                for (const auto& m : snap->meshes) {
                    CachedAsset& cached = load_asset(m.asset_path, m.asset_version);
                    if (!cached.meshes || m.mesh_index < 0 || m.mesh_index >= static_cast<int>(cached.meshes->size())) continue;
                    const LoadedMesh& lm = (*cached.meshes)[static_cast<std::size_t>(m.mesh_index)];
                    if (!lm.handle.valid()) continue;
                    if (m.skin >= 0 && m.skin < static_cast<int>(cached.skins.size()) && !m.joints.empty() && lm.skin.size() == lm.positions.size()) {
                        // Skinned (8C): posed by its joints. Its picking and framing copy is posed on the CPU (the same
                        // sum as the shader), already in the world.
                        const auto& palette = snapshot_palettes.emplace_back(noor::asset::skin_palette(cached.skins[static_cast<std::size_t>(m.skin)], m.joints));
                        LoadedMesh& posed = posed_meshes.emplace_back();
                        posed.handle = lm.handle;
                        posed.indices = lm.indices;
                        posed.parts = lm.parts;
                        posed.lo = {1e30f, 1e30f, 1e30f};
                        posed.hi = {-1e30f, -1e30f, -1e30f};
                        posed.positions.reserve(lm.positions.size());
                        for (std::size_t v = 0; v < lm.positions.size(); ++v) {
                            posed.positions.push_back(noor::asset::skin_point(lm.skin[v], palette, lm.positions[v]));
                            grow(posed.lo, posed.hi, posed.positions.back());
                        }
                        SceneDraw sd{{lm.handle, Mat4::identity()}, m.entity, &posed, false, m.model_root};
                        sd.item.skin_palette = palette;
                        scene_draws.push_back(sd);
                        grow(lo, hi, posed.lo);
                        grow(lo, hi, posed.hi);
                        any = true;
                        continue;
                    }
                    scene_draws.push_back({{lm.handle, m.world}, m.entity, &lm, false, m.model_root});
                    for (int c = 0; c < 8; ++c) {
                        grow(lo, hi, m.world.transform_point({c & 1 ? lm.hi.x : lm.lo.x, c & 2 ? lm.hi.y : lm.lo.y, c & 4 ? lm.hi.z : lm.lo.z}));
                        any = true;
                    }
                }
                // Placed models and their levels of detail (read once per file version).
                view_models.clear();
                for (const auto& md : snap->models) {
                    const std::string key = std::format("{}@{}", md.asset_path, md.asset_version);
                    auto lit = lod_cache.find(key);
                    if (lit == lod_cache.end()) lit = lod_cache.emplace(key, noor::game::read_model_lods(md.asset_path)).first;
                    if (lit->second.switches()) view_models.push_back({md.entity, md.asset_path, md.asset_version, md.world, &lit->second});
                }
                if (marker.valid()) {
                    for (const auto& mk : snap->markers) {
                        noor::render::DrawItem item{marker, mk.world * noor::math::scale({0.25f, 0.25f, 0.25f})};
                        item.cast_shadow = false;  // editor helpers don't cast shadows
                        if (mk.color) item.tint = {mk.color->x, mk.color->y, mk.color->z, 1.0f};  // lights: in their colour
                        scene_draws.push_back({item, mk.entity, nullptr});
                    }
                }
                for (const auto& prim : snap->primitives) {
                    const LoadedMesh* lm = &prim_box;
                    if (prim.shape == ed::RenderSnapshot::Primitive::Shape::Sphere) {
                        lm = &prim_sphere;
                    } else if (prim.shape == ed::RenderSnapshot::Primitive::Shape::Capsule) {
                        const std::pair<int, int> key{static_cast<int>(prim.radius * 1000), static_cast<int>(prim.height * 1000)};
                        auto it = prim_capsules.find(key);
                        if (it == prim_capsules.end())
                            it = prim_capsules.emplace(key, make_loaded(make_round(prim.radius, std::max(prim.height, prim.radius * 2)), "Primitive capsule")).first;
                        lm = &it->second;
                    }
                    if (!lm->handle.valid()) continue;
                    noor::render::DrawItem item{lm->handle, prim.world};
                    item.tint = {prim.color.x, prim.color.y, prim.color.z, 1.0f};
                    item.material = noor::game::SceneRenderer::material(prim, *materials);
                    scene_draws.push_back({item, prim.entity, lm, true});
                    for (int c = 0; c < 8; ++c) {
                        grow(lo, hi, prim.world.transform_point({c & 1 ? lm->hi.x : lm->lo.x, c & 2 ? lm->hi.y : lm->lo.y, c & 4 ? lm->hi.z : lm->lo.z}));
                        any = true;
                    }
                }
                // Terrains: one draw per tile.
                for (const auto& t : snap->terrains) {
                    TerrainGpu& tg = terrain_gpu[t.entity];
                    const std::size_t count = static_cast<std::size_t>(t.settings.tiles_x) * t.settings.tiles_z;
                    // Live (sculpted) heights come with a version per tile: only changed tiles are rebuilt.
                    const bool live = t.heights && t.tile_versions.size() == count;
                    const std::string key = live ? std::format("live {} {}", t.settings.tiles_x, t.settings.tiles_z) : t.settings.key();
                    if (tg.key != key || tg.tiles.size() != count) {
                        for (const auto& old : tg.tiles)
                            if (old.handle.valid()) r.destroy_mesh(old.handle);
                        tg.key = key;
                        tg.tiles.assign(count, LoadedMesh{});
                        tg.versions.assign(count, ~0ull);
                    }
                    const auto wanted = [&](std::size_t i) { return live ? t.tile_versions[i] : 0ull; };
                    bool rebuild = false;
                    for (std::size_t i = 0; i < count; ++i) rebuild = rebuild || tg.versions[i] != wanted(i);
                    if (rebuild) {
                        std::shared_ptr<const noor::terrain::Heights> heights = live ? t.heights : nullptr;
                        if (!heights) {
                            if (auto shared = noor::terrain::Heights::shared(t.settings)) heights = *shared;
                            else NOOR_LOG_WARN("Viewport", "Terrain: {}", shared.error().describe());
                        }
                        for (std::uint32_t tz = 0; tz < t.settings.tiles_z; ++tz)
                            for (std::uint32_t tx = 0; tx < t.settings.tiles_x; ++tx) {
                                const std::size_t i = static_cast<std::size_t>(tz) * t.settings.tiles_x + tx;
                                if (tg.versions[i] == wanted(i)) continue;
                                tg.versions[i] = wanted(i);  // also when it failed: no warning every frame
                                if (tg.tiles[i].handle.valid()) r.destroy_mesh(tg.tiles[i].handle);
                                tg.tiles[i] = LoadedMesh{};
                                if (!heights) continue;
                                auto mesh = noor::terrain::tile_mesh(*heights, tx, tz);
                                LoadedMesh lm{{}, mesh.lo, mesh.hi, {}, std::move(mesh.indices), {}};
                                if (auto h = r.create_mesh({{mesh.vertices.data(), mesh.vertices.size()}, lm.indices, "Terrain tile"})) lm.handle = *h;
                                lm.positions.reserve(mesh.vertices.size());
                                for (const auto& v : mesh.vertices) lm.positions.push_back(v.position);
                                tg.tiles[i] = std::move(lm);
                            }
                        if (heights) tg.heights = heights;
                    }
                    tg.position = t.position;
                    for (auto& lm : tg.tiles) {
                        if (!lm.handle.valid()) continue;
                        const auto item = noor::game::terrain_draw(t, lm.handle, [&](const std::filesystem::path& f) { return materials->file(f); });
                        lm.parts = {{0, static_cast<std::uint32_t>(lm.indices.size()), item.material}};
                        const Mat4 world = item.world_from_object;
                        scene_draws.push_back({item, t.entity, &lm});
                        for (int c = 0; c < 8; ++c) {
                            grow(lo, hi, world.transform_point({c & 1 ? lm.hi.x : lm.lo.x, c & 2 ? lm.hi.y : lm.lo.y, c & 4 ? lm.hi.z : lm.lo.z}));
                            any = true;
                        }
                    }
                }
                for (auto it = terrain_gpu.begin(); it != terrain_gpu.end();) {
                    if (std::ranges::any_of(snap->terrains, [&](const auto& t) { return t.entity == it->first; })) {
                        ++it;
                        continue;
                    }
                    for (const auto& lm : it->second.tiles)
                        if (lm.handle.valid()) r.destroy_mesh(lm.handle);
                    it = terrain_gpu.erase(it);
                }
                // CSG models: one draw per brush (so a click selects the brush that made the face).
                if (!ghost_subtract.valid()) {
                    noor::render::MaterialDesc ghost;
                    ghost.alpha = noor::render::AlphaMode::Blend;
                    ghost.double_sided = true;
                    ghost.roughness = 0.7f;
                    ghost.base_color = {0.95f, 0.3f, 0.25f, 0.22f};
                    if (auto h = r.create_material(ghost)) ghost_subtract = *h;
                    ghost.base_color = {0.25f, 0.55f, 1.0f, 0.22f};
                    if (auto h = r.create_material(ghost)) ghost_intersect = *h;
                }
                for (const auto& model : snap->csg) {
                    const ed::CsgMesh& m = *model.mesh;
                    auto [git, fresh] = csg_gpu.try_emplace(m.key);
                    CsgGpu& gpu = git->second;
                    if (fresh) {
                        std::vector<Vec3> positions;
                        positions.reserve(m.vertices.size());
                        for (const auto& v : m.vertices) positions.push_back(v.position);
                        if (!m.indices.empty())
                            if (auto h = r.create_mesh({{m.vertices.data(), m.vertices.size()}, m.indices, "CSG model"})) gpu.handle = *h;
                        for (const auto& group : m.groups) {
                            LoadedMesh lm{gpu.handle, {1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, positions, {}, {}};
                            for (const auto& part : group.parts) {
                                const auto& file = part.slot < m.materials.size() ? m.materials[part.slot] : std::filesystem::path{};
                                lm.parts.push_back({part.first_index, part.index_count,
                                                    file.empty() ? noor::render::MaterialHandle{} : materials->file(file)});
                                for (std::uint32_t i = part.first_index; i < part.first_index + part.index_count; ++i) {
                                    lm.indices.push_back(m.indices[i]);
                                    grow(lm.lo, lm.hi, positions[m.indices[i]]);
                                }
                                for (std::uint32_t t = part.first_index / 3; t < (part.first_index + part.index_count) / 3; ++t)
                                    lm.triangle_face.push_back(t < m.triangle_face.size() ? m.triangle_face[t] : 0u);
                            }
                            gpu.groups.push_back(std::move(lm));
                        }
                        for (const auto& ghost : m.ghosts) {
                            // Flat-shaded triangles (the ghost is a hint, not a surface).
                            std::vector<noor::render::Vertex> verts;
                            std::vector<std::uint32_t> idx;
                            for (std::size_t i = 0; i + 2 < ghost.positions.size(); i += 3) {
                                const Vec3 a = ghost.positions[i], b = ghost.positions[i + 1], c = ghost.positions[i + 2];
                                const Vec3 n = noor::math::normalize(noor::math::cross(b - a, c - a));
                                for (const Vec3 p : {a, b, c}) {
                                    idx.push_back(static_cast<std::uint32_t>(verts.size()));
                                    verts.push_back({p, n, {1, 1, 1}, {0, 0}, {1, 0, 0, 1}});
                                }
                            }
                            LoadedMesh lm{{}, {1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, ghost.positions, idx, {}};
                            for (const Vec3 p : ghost.positions) grow(lm.lo, lm.hi, p);
                            if (!verts.empty())
                                if (auto h = r.create_mesh({{verts.data(), verts.size()}, idx, "CSG ghost"})) lm.handle = *h;
                            lm.parts.push_back({0, static_cast<std::uint32_t>(idx.size()), ghost.intersect ? ghost_intersect : ghost_subtract});
                            gpu.ghosts.push_back(std::move(lm));
                        }
                    }
                    for (std::size_t gi = 0; gi < gpu.groups.size(); ++gi) {
                        const LoadedMesh& lm = gpu.groups[gi];
                        if (!lm.handle.valid()) continue;
                        scene_draws.push_back({{lm.handle, model.world}, m.groups[gi].brush, &lm});
                        for (int c = 0; c < 8; ++c) {
                            grow(lo, hi, model.world.transform_point({c & 1 ? lm.hi.x : lm.lo.x, c & 2 ? lm.hi.y : lm.lo.y, c & 4 ? lm.hi.z : lm.lo.z}));
                            any = true;
                        }
                    }
                    if (!snap->camera)  // editing, not playing: show the cutters
                        for (std::size_t gi = 0; gi < gpu.ghosts.size(); ++gi) {
                            const LoadedMesh& lm = gpu.ghosts[gi];
                            if (!lm.handle.valid()) continue;
                            noor::render::DrawItem item{lm.handle, model.world};
                            item.cast_shadow = false;
                            scene_draws.push_back({item, m.ghosts[gi].brush, &lm});
                        }
                }
                // Builds no longer in the scene: free their GPU meshes.
                for (auto it = csg_gpu.begin(); it != csg_gpu.end();) {
                    const bool used = std::ranges::any_of(snap->csg, [&](const auto& c) { return c.mesh->key == it->first; });
                    if (used) {
                        ++it;
                        continue;
                    }
                    if (it->second.handle.valid()) r.destroy_mesh(it->second.handle);
                    for (const auto& g : it->second.ghosts)
                        if (g.handle.valid()) r.destroy_mesh(g.handle);
                    it = csg_gpu.erase(it);
                }
            }
            if (any) {
                scene_lo = lo;
                scene_hi = hi;
                if (!framed_once) {
                    camera.frame(lo, hi);
                    framed_once = true;
                }
            }
            // The document has caught up with a released drag (or moved on): stop previewing.
            if (preview && !drag && preview->shown_at_release != snap.get()) preview.reset();
        }
        if (preview && !drag && std::chrono::steady_clock::now() - preview->released > 1500ms) preview.reset();  // edit rejected

        // The highlighted brush face: rebuilt when asked for another one or the scene changed.
        {
            std::optional<Guid> want;
            std::uint32_t want_face = 0;
            std::uint64_t version = 0;
            {
                std::scoped_lock lock(vp->doc_mutex);
                want = vp->face_brush;
                want_face = vp->face_index;
                version = vp->face_version;
            }
            if (version != seen_face_version || face_snapshot != shown) {
                seen_face_version = version;
                face_snapshot = shown;
                if (face_mesh.valid()) r.destroy_mesh(face_mesh);
                face_mesh = {};
                if (want && shown) {
                    std::vector<noor::render::Vertex> verts;
                    std::vector<std::uint32_t> idx;
                    for (const auto& d : scene_draws) {
                        if (d.entity != *want || !d.mesh || d.mesh->triangle_face.empty()) continue;
                        const auto& lm = *d.mesh;
                        for (std::size_t t = 0; t < lm.triangle_face.size(); ++t) {
                            if (lm.triangle_face[t] != want_face) continue;
                            const Vec3 a = lm.positions[lm.indices[t * 3]], b = lm.positions[lm.indices[t * 3 + 1]], c = lm.positions[lm.indices[t * 3 + 2]];
                            const Vec3 n = noor::math::normalize(noor::math::cross(b - a, c - a));
                            for (const Vec3 q : {a, b, c}) {
                                idx.push_back(static_cast<std::uint32_t>(verts.size()));
                                verts.push_back({q + n * 0.003f, n, {1, 1, 1}, {0, 0}, {1, 0, 0, 1}});
                            }
                        }
                        face_world = d.item.world_from_object;
                    }
                    if (!face_material.valid()) {
                        noor::render::MaterialDesc fm;
                        fm.alpha = noor::render::AlphaMode::Blend;
                        fm.double_sided = true;
                        fm.base_color = {0.13f, 0.83f, 0.93f, 0.35f};  // Noor cyan, see-through
                        fm.emissive = {0.05f, 0.33f, 0.37f};  // a little glow, so it shows in the shade too
                        if (auto h = r.create_material(fm)) face_material = *h;
                    }
                    if (!verts.empty())
                        if (auto h = r.create_mesh({{verts.data(), verts.size()}, idx, "Highlighted face"})) face_mesh = *h;
                }
            }
        }

        if (flags_dirty) {
            flags_dirty = false;
            std::unordered_set<Guid> selected(selection.begin(), selection.end());
            const auto moving = gizmo_entity ? subtree_of(*gizmo_entity) : std::unordered_set<Guid>{};
            draw_selected.assign(scene_draws.size(), 0);
            draw_in_gizmo_subtree.assign(scene_draws.size(), 0);
            model_selected.clear();
            for (std::size_t i = 0; i < scene_draws.size(); ++i) {
                draw_selected[i] = selected.contains(scene_draws[i].entity);
                draw_in_gizmo_subtree[i] = moving.contains(scene_draws[i].entity);
                if (!scene_draws[i].model_root.is_nil() && draw_selected[i]) model_selected[scene_draws[i].model_root] = true;
            }
        }

        // Frame request from the editor (F key, double-click in the hierarchy...).
        if (frame_request && shown) {
            if (frame_request->empty()) {
                camera.frame(scene_lo, scene_hi);
            } else {
                const std::unordered_set<Guid> wanted(frame_request->begin(), frame_request->end());
                Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
                bool any = false;
                for (const auto& d : scene_draws) {
                    if (!wanted.contains(d.entity)) continue;
                    const Vec3 a = d.mesh ? d.mesh->lo : Vec3{-1, -1, -1}, b = d.mesh ? d.mesh->hi : Vec3{1, 1, 1};
                    for (int c = 0; c < 8; ++c) {
                        grow(lo, hi, d.item.world_from_object.transform_point({c & 1 ? b.x : a.x, c & 2 ? b.y : a.y, c & 4 ? b.z : a.z}));
                        any = true;
                    }
                }
                for (const auto& [entity, quad] : sprite_quads) {  // sprites: their quads as last drawn
                    if (!wanted.contains(entity)) continue;
                    for (const Vec3 corner : {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}}) grow(lo, hi, quad.transform_point(corner));
                    any = true;
                }
                if (!any) {  // no geometry (an empty entity): frame its origin
                    for (const Guid& id : *frame_request) {
                        if (auto it = entity_by_id.find(id); it != entity_by_id.end()) {
                            const Vec3 p = it->second->world.transform_point({0, 0, 0});
                            grow(lo, hi, p - Vec3{2, 2, 2});
                            grow(lo, hi, p + Vec3{2, 2, 2});
                            any = true;
                        }
                    }
                }
                if (any) camera.frame(lo, hi);
            }
        }

        const noor::platform::PixelSize size = vp->window->pixel_size();
        if (size != last_size) {
            r.notify_resized();
            last_size = size;
        }

        // ---- Pointer: camera, gizmo, picking ----
        const auto pointer = vp->window->pointer_state();
        const float dx = static_cast<float>(pointer.x - last_pointer.x);
        const float dy = static_cast<float>(pointer.y - last_pointer.y);
        const auto frame_now = std::chrono::steady_clock::now();
        const float frame_seconds = std::chrono::duration<float>(frame_now - last_frame_time).count();
        last_frame_time = frame_now;
        // Fly mode (hold the right button): mouse looks, WASD moves, Q/E down/up, Shift faster,
        // wheel changes the speed. Alt + left-drag orbits; middle-drag pans; the wheel zooms.
        const bool flying = pointer.right_down;
        if (pointer.left_down && !last_pointer.left_down) alt_orbit = pointer.alt;
        if (flying && last_pointer.right_down) {
            camera.look(dx, dy);
        } else if (pointer.left_down && last_pointer.left_down && alt_orbit) {
            camera.orbit(dx, dy);
        } else if (pointer.middle_down && last_pointer.middle_down) {
            camera.pan(dx, dy);
        }
        if (flying) {
            using namespace noor::platform;
            const std::uint32_t k = vp->window->keys();
            const Vec3 move{static_cast<float>(((k & kKeyRight) != 0) - ((k & kKeyLeft) != 0)),
                            static_cast<float>(((k & kKeyUp) != 0) - ((k & kKeyDown) != 0)),
                            static_cast<float>(((k & kKeyForward) != 0) - ((k & kKeyBack) != 0))};
            camera.fly(move, frame_seconds, (k & kKeyFast) != 0);
        }
        if (const float wheel = vp->window->take_wheel(); wheel != 0.0f) {
            if (flying) camera.change_fly_speed(wheel);
            else camera.zoom(wheel);
        }

        // While playing with a camera entity, the viewport is the game view.
        auto basis = camera.basis();
        float view_near = std::max(0.05f, camera.distance * 0.002f);
        if (shown && shown->camera) {
            const Mat4& m = shown->camera->world;
            const Vec3 eye = m.transform_point({0, 0, 0});
            const Vec3 fwd = noor::math::normalize(noor::math::transform_vector(m, {0, 0, -1}));
            Vec3 right = noor::math::cross(fwd, {0, 1, 0});
            right = noor::math::length(right) < 1e-4f ? noor::math::normalize(noor::math::transform_vector(m, {1, 0, 0})) : noor::math::normalize(right);
            basis = {eye, fwd, right, noor::math::cross(right, fwd), noor::math::radians(shown->camera->fov_degrees)};
            view_near = shown->camera->near_plane;
        }
        const ed::Ray ray = ed::screen_ray(basis, static_cast<float>(size.width), static_cast<float>(size.height),
                                           static_cast<float>(pointer.x), static_cast<float>(pointer.y));

        // The gizmo sits on the entity's origin (moved by any preview) and keeps a constant size on screen.
        const ed::RenderSnapshot::Entity* target = nullptr;
        if (gizmo_entity && gizmo_mode != ed::GizmoMode::None) {
            if (auto it = entity_by_id.find(*gizmo_entity); it != entity_by_id.end() && it->second->editable) target = it->second;
        }
        Vec3 gizmo_center{};
        float gizmo_size = 1.0f;
        if (target) {
            const Mat4 world = preview ? preview->delta * target->world : target->world;
            gizmo_center = world.transform_point({0, 0, 0});
            gizmo_size = std::max(0.05f, noor::math::length(gizmo_center - basis.eye) * ed::kGizmoScreenScale * options.gizmo_scale);
        }
        // Scale handles follow the entity's own axes; move and rotate are world-aligned.
        const Quat gizmo_orientation = target && gizmo_mode == ed::GizmoMode::Scale ? target->world_rotation : Quat{};

        const bool pressed = pointer.left_down && !last_pointer.left_down;
        const bool released = !pointer.left_down && last_pointer.left_down;
        if (!drag) hovered = target && pointer.inside ? ed::pick_handle(ray, gizmo_mode, gizmo_center, gizmo_size, gizmo_orientation) : ed::Handle::None;

        // ---- Sculpt brush: the ground under the pointer, strokes as events (the document sculpts) ----
        std::optional<Guid> brush_terrain;
        float brush_radius = 8.0f;
        {
            std::scoped_lock lock(vp->doc_mutex);
            brush_terrain = vp->brush_terrain;
            brush_radius = vp->brush_radius;
        }
        const bool brush_on = brush_terrain && shown && !shown->camera;
        const TerrainGpu* brush_tg = nullptr;
        brush_hit.reset();
        if (brush_on) {
            if (auto it = terrain_gpu.find(*brush_terrain); it != terrain_gpu.end() && it->second.heights) brush_tg = &it->second;
            if (brush_tg && (pointer.inside || stroking)) {
                if (auto t = noor::terrain::raycast(*brush_tg->heights, ray.origin - brush_tg->position, noor::math::normalize(ray.dir)))
                    brush_hit = ray.origin + noor::math::normalize(ray.dir) * *t;
            }
            auto stroke_event = [&](const char* phase) {
                noor::asset::Json ev{{"type", "stroke"}, {"phase", phase}, {"terrain", brush_terrain->to_string()}, {"seconds", frame_seconds}};
                if (brush_hit) ev["point"] = {brush_hit->x, brush_hit->y, brush_hit->z};
                push_event(vp, std::move(ev));
            };
            if (pressed && !alt_orbit && brush_hit) {
                stroking = true;
                stroke_event("begin");
            } else if (stroking && pointer.left_down) {
                stroke_event("dab");
            }
            if (stroking && !pointer.left_down) {
                stroke_event("end");
                stroking = false;
            }
        } else if (stroking) {
            push_event(vp, {{"type", "stroke"}, {"phase", "end"}, {"terrain", nullptr}, {"seconds", 0.0}});
            stroking = false;
        }

        if (pressed && alt_orbit) {
            click_pending = false;
        } else if (pressed && brush_on) {
            click_pending = false;  // the brush has the left button
        } else if (pressed) {
            // Hit-test at the press itself (hover tracking may not have run, e.g. a click that
            // arrives without a preceding move).
            const ed::Handle grabbed = target ? ed::pick_handle(ray, gizmo_mode, gizmo_center, gizmo_size, gizmo_orientation) : ed::Handle::None;
            if (grabbed != ed::Handle::None) {
                hovered = grabbed;
                drag = ed::GizmoDrag::begin(grabbed, ray, gizmo_center, basis.forward, gizmo_size, gizmo_orientation);
                drag_center = gizmo_center;
                if (drag) preview = Preview{target->id};
            }
            click_pending = !drag;
            press = pointer;
        }
        if (drag && preview && target) {
            // The drag so far, snapped if snapping is on (Ctrl inverts it), as a world-space delta for
            // the preview and as the entity's new local transform for the event on release.
            const bool snapping = options.snap != pointer.ctrl;
            const ed::Handle h = drag->handle();
            const Vec3 c = drag_center;
            DragResult res;
            res.local = target->local;
            const auto parent = target->parent ? entity_by_id.find(*target->parent) : entity_by_id.end();
            const Mat4 parent_world = parent != entity_by_id.end() ? parent->second->world : Mat4::identity();
            const Quat parent_rot = parent != entity_by_id.end() ? parent->second->world_rotation : Quat{};
            if (ed::is_rotate(h)) {
                float angle = drag->angle(ray);
                if (snapping) angle = ed::snap(angle, noor::math::radians(options.rotate_step));
                const Vec3 axis = ed::handle_axis(h);
                res.delta = noor::math::translation(c) * noor::math::rotation(axis, angle) * noor::math::translation(-c);
                res.local.rotation = noor::math::normalize_quat(noor::math::conjugate(parent_rot) * Quat::from_axis_angle(axis, angle) *
                                                                parent_rot * res.local.rotation);
                res.changed = std::fabs(angle) > 1e-5f;
                res.label = "Rotate";
            } else if (ed::is_scale(h)) {
                Vec3 f = drag->scale(ray);
                if (snapping) {
                    auto snap_factor = [&](float factor, float current) {
                        if (std::fabs(current) < 1e-6f) return factor;
                        float target_scale = ed::snap(current * factor, options.scale_step);
                        if (target_scale <= 0.0f) target_scale = options.scale_step > 0 ? options.scale_step : 0.01f;
                        return target_scale / current;
                    };
                    f = {snap_factor(f.x, res.local.scale.x), snap_factor(f.y, res.local.scale.y), snap_factor(f.z, res.local.scale.z)};
                }
                const Mat4 rot = noor::math::to_mat4(target->world_rotation);
                const Mat4 inv_rot = noor::math::to_mat4(noor::math::conjugate(target->world_rotation));
                res.delta = noor::math::translation(c) * rot * noor::math::scale(f) * inv_rot * noor::math::translation(-c);
                res.local.scale = {res.local.scale.x * f.x, res.local.scale.y * f.y, res.local.scale.z * f.z};
                res.changed = noor::math::length(f - Vec3{1, 1, 1}) > 1e-5f;
                res.label = "Scale";
            } else {
                Vec3 d = drag->translation(ray);
                if (snapping) {  // land on the world grid, on the axes being moved
                    const bool all = h == ed::Handle::MoveXZ;
                    if (h == ed::Handle::MoveX || all) d.x = ed::snap(c.x + d.x, options.move_step) - c.x;
                    if (h == ed::Handle::MoveY) d.y = ed::snap(c.y + d.y, options.move_step) - c.y;
                    if (h == ed::Handle::MoveZ || all) d.z = ed::snap(c.z + d.z, options.move_step) - c.z;
                }
                res.delta = noor::math::translation(d);
                res.local.position = res.local.position + noor::math::transform_vector(noor::math::inverse(parent_world), d);
                res.changed = noor::math::length(d) > 1e-6f;
            }
            drag_result = res;
            preview->delta = res.delta;
        }
        if (released) {
            if (drag && preview && target) {
                if (drag_result.changed) {
                    push_event(vp, {{"type", "transform"}, {"entity", target->id.to_string()}, {"label", drag_result.label},
                                    {"transform", transform_json(drag_result.local)}});
                    preview->shown_at_release = shown.get();
                    preview->released = std::chrono::steady_clock::now();
                } else {
                    preview.reset();
                }
            } else if (click_pending && std::abs(pointer.x - press.x) + std::abs(pointer.y - press.y) <= 4 && shown) {
                // Pick the nearest mesh or marker under the pointer.
                float best = std::numeric_limits<float>::max();
                std::optional<Guid> hit;
                std::optional<std::uint32_t> hit_face;  // CSG: the brush face under the pointer
                for (const auto& [entity, quad] : sprite_quads) {  // sprites: their whole quad
                    const ed::Ray local_ray = ed::to_object_space(ray, quad);
                    for (const auto& tri : {std::array<Vec3, 3>{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}}, std::array<Vec3, 3>{Vec3{0, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}}})
                        if (auto t = ed::ray_triangle(local_ray, tri[0], tri[1], tri[2]); t && *t < best) {
                            best = *t;
                            hit = entity;
                        }
                }
                for (const auto& d : scene_draws) {
                    const ed::Ray local_ray = ed::to_object_space(ray, d.item.world_from_object);
                    if (!d.mesh) {
                        if (auto t = ed::ray_aabb(local_ray, {-1, -1, -1}, {1, 1, 1}); t && *t < best) {
                            best = *t;
                            hit = d.entity;
                        }
                        continue;
                    }
                    const auto box = ed::ray_aabb(local_ray, d.mesh->lo, d.mesh->hi);
                    if (!box || *box >= best) continue;
                    const auto& p = d.mesh->positions;
                    const auto& idx = d.mesh->indices;
                    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
                        if (auto t = ed::ray_triangle(local_ray, p[idx[i]], p[idx[i + 1]], p[idx[i + 2]]); t && *t < best) {
                            best = *t;
                            hit = d.entity;
                            hit_face = i / 3 < d.mesh->triangle_face.size() ? std::optional(d.mesh->triangle_face[i / 3]) : std::nullopt;
                        }
                    }
                }
                noor::asset::Json click{{"type", "click"}, {"entity", hit ? noor::asset::Json(hit->to_string()) : noor::asset::Json(nullptr)}};
                if (hit && hit_face) click["face"] = *hit_face;
                push_event(vp, std::move(click));
            }
            drag.reset();
            click_pending = false;
        }
        last_pointer = pointer;

        {
            std::scoped_lock lock(vp->camera_mutex);
            vp->camera = basis;
            vp->camera_width = static_cast<float>(size.width);
            vp->camera_height = static_cast<float>(size.height);
        }

        // ---- Draw ----
        noor::render::FrameDesc frame;
        std::vector<noor::render::DrawItem> frame_draws;
        if (channel) {
            r.set_clear_color({srgb(0x14121C).x, srgb(0x14121C).y, srgb(0x14121C).z, 1.0f});
            frame_draws.reserve(scene_draws.size() + 8);
            const Vec4 highlight = tint(0x9B7BFF, 0.45f);
            // Levels of detail (Milestone 7, part 6): a model far enough from this camera is drawn whole
            // from a lower level's file instead of its own entities (picking still uses level 0).
            std::unordered_map<Guid, int> lowered;
            for (const auto& vm : view_models) {
                const Mat4& w = vm.world;
                const float scale = std::max({noor::math::length(Vec3{w.m[0].x, w.m[0].y, w.m[0].z}), noor::math::length(Vec3{w.m[1].x, w.m[1].y, w.m[1].z}),
                                              noor::math::length(Vec3{w.m[2].x, w.m[2].y, w.m[2].z})});
                const int level = noor::game::choose_lod(*vm.lods, noor::math::length(w.transform_point({0, 0, 0}) - basis.eye), basis.fov_y, scale);
                if (level <= 0) continue;
                const CachedAsset& la = load_asset(vm.lods->levels[static_cast<std::size_t>(level)].file.generic_string(), vm.version);
                if (!la.meshes) continue;  // failed: keep level 0
                lowered.emplace(vm.entity, level);
                const bool picked = model_selected.contains(vm.entity);
                for (const auto& [mesh_index, local] : la.nodes) {
                    if (mesh_index < 0 || mesh_index >= static_cast<int>(la.meshes->size())) continue;
                    const LoadedMesh& lm = (*la.meshes)[static_cast<std::size_t>(mesh_index)];
                    if (!lm.handle.valid()) continue;
                    for (const auto& part : lm.parts) {
                        noor::render::DrawItem piece{lm.handle, w * local};
                        piece.first_index = part.first_index;
                        piece.index_count = part.index_count;
                        piece.material = part.material;
                        if (picked) piece.tint = highlight;
                        frame_draws.push_back(piece);
                    }
                }
            }
            // Scattered instances (Milestone 7, part 7): whole models from their files, a level per instance
            // (they're not entities: the scatter entity is selected in the hierarchy, not by clicking a tree).
            if (shown) {
                for (const auto& sc : shown->scattered) {
                    const std::string key = std::format("{}@{}", sc.asset_path, sc.asset_version);
                    auto lit = lod_cache.find(key);
                    if (lit == lod_cache.end()) lit = lod_cache.emplace(key, noor::game::read_model_lods(sc.asset_path)).first;
                    const auto& lods = lit->second;
                    const bool picked = std::ranges::find(selection, sc.entity) != selection.end();
                    const float limit2 = sc.draw_distance * sc.draw_distance;
                    for (const auto& w : *sc.worlds) {
                        const Vec3 d{w.m[3].x - basis.eye.x, w.m[3].y - basis.eye.y, w.m[3].z - basis.eye.z};
                        const float dist2 = noor::math::dot(d, d);
                        if (sc.draw_distance > 0.0f && dist2 > limit2) continue;  // grass beyond its draw distance
                        int level = 0;
                        if (lods.switches())
                            level = noor::game::choose_lod(lods, std::sqrt(dist2), basis.fov_y, noor::math::length(Vec3{w.m[0].x, w.m[0].y, w.m[0].z}));
                        const CachedAsset& ca = load_asset(level > 0 ? lods.levels[static_cast<std::size_t>(level)].file.generic_string() : sc.asset_path, sc.asset_version);
                        if (!ca.meshes) continue;
                        for (const auto& [mesh_index, local] : ca.nodes) {
                            if (mesh_index < 0 || mesh_index >= static_cast<int>(ca.meshes->size())) continue;
                            const LoadedMesh& lm = (*ca.meshes)[static_cast<std::size_t>(mesh_index)];
                            if (!lm.handle.valid()) continue;
                            for (const auto& part : lm.parts) {
                                noor::render::DrawItem piece{lm.handle, w * local};
                                piece.first_index = part.first_index;
                                piece.index_count = part.index_count;
                                piece.material = part.material;
                                piece.cast_shadow = sc.cast_shadows;
                                if (picked) piece.tint = highlight;
                                frame_draws.push_back(piece);
                            }
                        }
                    }
                }
            }
            for (std::size_t i = 0; i < scene_draws.size(); ++i) {
                if (!scene_draws[i].model_root.is_nil() && lowered.contains(scene_draws[i].model_root)) continue;
                noor::render::DrawItem item = scene_draws[i].item;
                if (preview && i < draw_in_gizmo_subtree.size() && draw_in_gizmo_subtree[i]) item.world_from_object = preview->delta * item.world_from_object;
                if (i < draw_selected.size() && draw_selected[i]) {
                    item.tint = scene_draws[i].colored
                                    ? Vec4{item.tint.x * 0.55f + highlight.x * 0.45f, item.tint.y * 0.55f + highlight.y * 0.45f,
                                           item.tint.z * 0.55f + highlight.z * 0.45f, 1.0f}
                                    : highlight;
                }
                const LoadedMesh* lm = scene_draws[i].mesh;
                if (lm && !lm->parts.empty()) {
                    for (const auto& part : lm->parts) {  // a model: one draw per material
                        noor::render::DrawItem piece = item;
                        piece.first_index = part.first_index;
                        piece.index_count = part.index_count;
                        piece.material = part.material;
                        frame_draws.push_back(piece);
                    }
                } else {
                    frame_draws.push_back(item);
                }
            }
            if (face_mesh.valid() && shown && !shown->camera) {
                noor::render::DrawItem face_item{face_mesh, face_world};
                face_item.material = face_material;
                face_item.cast_shadow = false;
                frame_draws.push_back(face_item);
            }
            if (ground.valid() && (!shown || (shown->show_grid && shown->terrains.empty()))) {  // a terrain is the ground: no grid to fight with
                const float half = std::max({200.0f, scene_hi.x - scene_lo.x, scene_hi.z - scene_lo.z});
                const Vec3 c = (scene_lo + scene_hi) * 0.5f;
                noor::render::DrawItem ground_item{ground, noor::math::translation({c.x, -0.05f, c.z}) * noor::math::scale({half, 0.05f, half})};
                ground_item.cast_shadow = false;
                frame_draws.push_back(ground_item);
            }
            if (target) {
                const Mat4 place = noor::math::translation(gizmo_center);
                const Mat4 sized = noor::math::scale({gizmo_size, gizmo_size, gizmo_size});
                auto gizmo = [&](noor::render::MeshHandle mesh, ed::Handle h, const Mat4& frame_m) {
                    const bool hot = (drag && drag->handle() == h) || (!drag && hovered == h);
                    Vec3 c = srgb(handle_color(h));
                    if (hot) c = c * 0.35f + Vec3{0.65f, 0.65f, 0.65f};
                    frame_draws.push_back({mesh, place * frame_m * sized, {c.x, c.y, c.z, 1.0f}, true});
                };
                if (gizmo_mode == ed::GizmoMode::Move) {
                    for (ed::Handle h : {ed::Handle::MoveX, ed::Handle::MoveY, ed::Handle::MoveZ}) gizmo(arrow, h, axis_frame(h));
                    gizmo(plane, ed::Handle::MoveXZ, Mat4::identity());
                    frame_draws.push_back({hub, place * sized, tint(0xE7E3F2, 1.0f), true});
                } else if (gizmo_mode == ed::GizmoMode::Rotate) {
                    for (ed::Handle h : {ed::Handle::RotateX, ed::Handle::RotateY, ed::Handle::RotateZ}) gizmo(ring, h, axis_frame(h));
                } else {
                    const Mat4 orient = noor::math::to_mat4(gizmo_orientation);
                    for (ed::Handle h : {ed::Handle::ScaleX, ed::Handle::ScaleY, ed::Handle::ScaleZ}) gizmo(scale_handle, h, orient * axis_frame(h));
                    gizmo(scale_all, ed::Handle::ScaleAll, orient);
                }
            }
            frame.camera.view = noor::math::look_at(basis.eye, basis.eye + basis.forward, basis.up);
            frame.camera.fov_y = basis.fov_y;
            frame.camera.near_plane = view_near;
            if (shown) frame.lighting = shown->lighting;
            // The clouds drift and sprites animate in the editor too: seconds since the viewport opened, or the game's time
            // while playing (so animations scripts play line up).
            frame.lighting.time = shown && shown->game_time
                                      ? static_cast<float>(*shown->game_time)
                                      : static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count());
            // Sprites (8B part 5): turned to this camera every frame, animated on the clock above; a sprite being
            // moved follows the gizmo, a selected one is tinted like the rest.
            sprite_quads.clear();
            if (shown && sprites && !shown->sprites.empty()) {
                const auto moving = preview && gizmo_entity ? subtree_of(*gizmo_entity) : std::unordered_set<Guid>{};
                const std::unordered_set<Guid> selected(selection.begin(), selection.end());
                std::vector<noor::game::SceneView::Sprite> placed(shown->sprites.begin(), shown->sprites.end());
                for (auto& sp : placed)
                    if (moving.contains(sp.entity)) sp.world = preview->delta * sp.world;
                for (const auto& sp : placed) {
                    const std::size_t before = frame_draws.size();
                    sprites->add_draws({&sp, 1}, basis.eye, static_cast<double>(frame.lighting.time), frame_draws);
                    for (std::size_t i = before; i < frame_draws.size(); ++i) {
                        sprite_quads.emplace_back(sp.entity, frame_draws[i].world_from_object);
                        if (selected.contains(sp.entity)) frame_draws[i].tint = {highlight.x, highlight.y, highlight.z, 0.45f};
                    }
                }
            }
            frame.draws = frame_draws;
            // Effect particles: the preview (8B part 4).
            {
                const auto now_clock = std::chrono::steady_clock::now();
                const double elapsed = std::min(0.1, std::chrono::duration<double>(now_clock - preview_clock).count());
                preview_clock = now_clock;
                if (shown && shown->preview_restart != preview_restart_seen) {
                    preview_restart_seen = shown->preview_restart;
                    preview_emitters.clear();
                    preview_carry = 0.0;
                }
                std::vector<noor::fx::EmitterSet::Input> inputs;
                if (shown) {
                    inputs.reserve(shown->emitters.size());
                    for (const auto& em : shown->emitters) inputs.push_back({em.entity, &em.settings, em.world});
                }
                if (inputs.empty()) {
                    preview_emitters.clear();
                } else if (!shown->preview_paused) {
                    constexpr double kStep = 1.0 / 60.0;
                    preview_carry += elapsed;
                    // Colliding particles land on the snapshot's terrains (part 7), live sculpting included.
                    if (preview_ground_snapshot != shown.get()) {
                        preview_ground_snapshot = shown.get();
                        std::vector<noor::game::ParticleGround::Piece> pieces;
                        for (const auto& t : shown->terrains) {
                            std::shared_ptr<const noor::terrain::Heights> h = t.heights;
                            if (!h)
                                if (auto made = noor::terrain::Heights::shared(t.settings)) h = *made;
                            if (h) pieces.push_back({t.position, h});
                        }
                        preview_ground.set(std::move(pieces));
                    }
                    const noor::fx::GroundQuery preview_floor = preview_ground.query();
                    for (int steps = 0; preview_carry >= kStep && steps < 6; ++steps) {
                        preview_emitters.step(inputs, static_cast<float>(kStep), shown->wind, preview_floor ? &preview_floor : nullptr);
                        preview_carry -= kStep;
                    }
                }
                noor::game::ParticleDrawSources sources;
                std::unordered_map<Guid, Mat4> where;
                if (shown)
                    for (const auto& e : shown->entities) where.emplace(e.id, e.world);
                sources.world_of = [&where](const Guid& id) -> std::optional<Mat4> {
                    const auto it = where.find(id);
                    return it == where.end() ? std::nullopt : std::optional(it->second);
                };
                const auto files = shown ? shown->asset_files : nullptr;
                sources.texture = [&](const Guid& id) -> noor::render::TextureHandle {
                    if (!files || !textures) return {};
                    const auto it = files->find(id);
                    return it == files->end() ? noor::render::TextureHandle{} : textures->get(it->second);
                };
                const auto solo = shown ? shown->preview_solo : std::nullopt;
                const std::vector<Guid> muted = shown ? shown->preview_muted : std::vector<Guid>{};
                sources.shown = [&](const Guid& id) {
                    if (solo) return id == *solo;
                    return std::find(muted.begin(), muted.end(), id) == muted.end();
                };
                frame.particles = noor::game::particle_draws(preview_emitters, basis.eye, sources, preview_draws);
                preview_particles = 0;
                for (const auto& d : frame.particles) preview_particles += d.particles.size();
            }
            // The game's HUD while playing. The font atlas is baked the first time a HUD appears.
            overlay.clear();
            if (shown && !shown->ui.empty()) {
                if (!hud) {
                    hud.emplace();
                    if (auto s = r.set_overlay_atlas(hud->atlas_width(), hud->atlas_height(), hud->atlas()); !s)
                        NOOR_LOG_ERROR("Viewport", "HUD font: {}", s.error().describe());
                }
                const auto pixels = r.swapchain_size();
                hud->build(shown->ui, static_cast<float>(pixels.width), static_cast<float>(pixels.height), overlay);
            }
            // The sculpt brush: a circle lying on the ground under the pointer.
            if (brush_hit && brush_tg) {
                if (!hud) {
                    hud.emplace();
                    if (auto s = r.set_overlay_atlas(hud->atlas_width(), hud->atlas_height(), hud->atlas()); !s)
                        NOOR_LOG_ERROR("Viewport", "HUD font: {}", s.error().describe());
                }
                const Vec4 color{0.85f, 0.75f, 1.0f, stroking ? 1.0f : 0.8f};
                const Vec3 c = *brush_hit - brush_tg->position;
                constexpr int kSegments = 64;
                std::optional<ed::ScreenPoint> prev;
                for (int i = 0; i <= kSegments; ++i) {
                    const float a = static_cast<float>(i) / kSegments * 6.2831853f;
                    const float x = c.x + std::cos(a) * brush_radius, z = c.z + std::sin(a) * brush_radius;
                    const Vec3 p = Vec3{x, brush_tg->heights->height(x, z) + 0.05f, z} + brush_tg->position;
                    const auto s = ed::world_to_screen(basis, static_cast<float>(size.width), static_cast<float>(size.height), p);
                    if (s && prev) {
                        // A 2-pixel band from the previous point to this one (uv -1: a solid colour).
                        const float sx = s->x - prev->x, sy = s->y - prev->y, len = std::max(1e-3f, std::sqrt(sx * sx + sy * sy));
                        const float nx = -sy / len, ny = sx / len;
                        const noor::render::OverlayVertex v0{{prev->x + nx, prev->y + ny}, {-1, -1}, color}, v1{{s->x + nx, s->y + ny}, {-1, -1}, color};
                        const noor::render::OverlayVertex v2{{s->x - nx, s->y - ny}, {-1, -1}, color}, v3{{prev->x - nx, prev->y - ny}, {-1, -1}, color};
                        overlay.insert(overlay.end(), {v0, v1, v2, v0, v2, v3});
                    }
                    prev = s;
                }
            }
            // The Animation panel's preview (8C part 6): the character's skeleton over everything, a line per bone and
            // a dot per joint; the bone picked in the panel brighter and thicker.
            if (shown && !shown->bones.empty()) {
                if (!hud) {
                    hud.emplace();
                    if (auto s = r.set_overlay_atlas(hud->atlas_width(), hud->atlas_height(), hud->atlas()); !s)
                        NOOR_LOG_ERROR("Viewport", "HUD font: {}", s.error().describe());
                }
                const float w = static_cast<float>(size.width), h = static_cast<float>(size.height);
                const auto band = [&](const ed::ScreenPoint& a, const ed::ScreenPoint& b, float half, const Vec4& color) {
                    const float sx = b.x - a.x, sy = b.y - a.y, len = std::max(1e-3f, std::sqrt(sx * sx + sy * sy));
                    const float nx = -sy / len * half, ny = sx / len * half;
                    const noor::render::OverlayVertex v0{{a.x + nx, a.y + ny}, {-1, -1}, color}, v1{{b.x + nx, b.y + ny}, {-1, -1}, color};
                    const noor::render::OverlayVertex v2{{b.x - nx, b.y - ny}, {-1, -1}, color}, v3{{a.x - nx, a.y - ny}, {-1, -1}, color};
                    overlay.insert(overlay.end(), {v0, v1, v2, v0, v2, v3});
                };
                const auto dot = [&](const ed::ScreenPoint& p, float half, const Vec4& color) {
                    band({p.x - half, p.y, p.distance}, {p.x + half, p.y, p.distance}, half, color);
                };
                const Vec4 line{0.80f, 0.72f, 1.0f, 0.9f}, picked{1.0f, 0.62f, 0.25f, 1.0f}, joint{1.0f, 1.0f, 1.0f, 0.95f};
                for (const auto& bone : shown->bones) {
                    const auto a = ed::world_to_screen(basis, w, h, bone.from), b = ed::world_to_screen(basis, w, h, bone.to);
                    if (a && b && (bone.from.x != bone.to.x || bone.from.y != bone.to.y || bone.from.z != bone.to.z))
                        band(*a, *b, bone.highlight ? 2.0f : 1.0f, bone.highlight ? picked : line);
                }
                for (const auto& bone : shown->bones)  // dots on top of the lines
                    if (const auto b = ed::world_to_screen(basis, w, h, bone.to)) dot(*b, bone.joint ? 4.0f : 2.0f, bone.joint ? picked : joint);
            }
            frame.overlay = overlay;
        } else {
            r.set_clear_color(demo_clear_color(pointer, size));
            frame.lighting.sky_background = false;  // no document: the animated test colour, exactly
        }

        auto result = r.render_frame(frame);
        if (!result) {
            NOOR_LOG_ERROR("Viewport", "Frame failed: {}", result.error().describe());
            set_state(vp, NOOR_VIEWPORT_FAILED);
            break;
        }
        if (*result == noor::render::FrameResult::Skipped) std::this_thread::sleep_for(10ms);  // hidden: don't spin

        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t presented = r.frames_presented();
        if (now - last_fps_time >= 1s) {
            fps = static_cast<double>(presented - frames_at_last_fps) / std::chrono::duration<double>(now - last_fps_time).count();
            last_fps_time = now;
            frames_at_last_fps = presented;
        }
        std::string stats_json;
        if (now - last_stats_time >= 500ms) {
            last_stats_time = now;
            const auto& fs = r.last_frame_stats();
            noor::asset::Json passes = noor::asset::Json::array();
            for (const auto& p : fs.passes) passes.push_back({{"name", p.name}, {"ms", p.ms}});
            stats_json = noor::asset::Json{{"gpu_ms", fs.gpu_ms},
                                           {"passes", std::move(passes)},
                                           {"draw_calls", fs.draw_calls},
                                           {"instances", fs.instances},
                                           {"culled", fs.culled},
                                           {"triangles", fs.triangles},
                                           {"shadow_draw_calls", fs.shadow_draw_calls},
                                           {"shadow_draws_culled", fs.shadow_draws_culled},
                                           {"transparent_draws", fs.transparent_draws},
                                           {"particle_draws", fs.particle_draws},
                                           {"particles", preview_particles},
                                           {"sprites", sprite_quads.size()},
                                           {"weather_drops", fs.weather_drops},
                                           {"msaa_samples", fs.msaa_samples},
                                           {"texture_bytes", r.texture_memory()},
                                           {"mesh_bytes", r.mesh_memory()},
                                           {"render_target_bytes", r.render_target_memory()},
                                           {"width", size.width},
                                           {"height", size.height},
                                           {"fps", fps}}
                             .dump();
        }

        std::scoped_lock lock(vp->stats_mutex);
        if (presented > 0 && vp->stats.seconds_to_first_frame == 0.0) {
            vp->stats.seconds_to_first_frame = std::chrono::duration<double>(now - vp->created_at).count();
            NOOR_LOG_INFO("Viewport", "First frame presented {:.0f} ms after viewport creation",
                          vp->stats.seconds_to_first_frame * 1000.0);
        }
        vp->stats.frames_presented = presented;
        vp->stats.frames_per_second = fps;
        vp->stats.width = size.width;
        vp->stats.height = size.height;
        vp->stats.swapchain_rebuilds = r.swapchain_rebuilds();
        vp->stats.validation_errors = r.validation_error_count();
        if (!stats_json.empty()) vp->render_stats = std::move(stats_json);
    }

    for (auto& [id, tg] : terrain_gpu)
        for (const auto& lm : tg.tiles)
            if (lm.handle.valid()) r.destroy_mesh(lm.handle);
    terrain_gpu.clear();
    for (auto& [key, gpu] : csg_gpu) {
        if (gpu.handle.valid()) r.destroy_mesh(gpu.handle);
        for (const auto& g : gpu.ghosts)
            if (g.handle.valid()) r.destroy_mesh(g.handle);
    }
    csg_gpu.clear();
    if (face_mesh.valid()) r.destroy_mesh(face_mesh);
    if (face_material.valid()) r.destroy_material(face_material);
    if (ghost_subtract.valid()) r.destroy_material(ghost_subtract);
    if (ghost_intersect.valid()) r.destroy_material(ghost_intersect);
    environment.reset();
    sprites.reset();
    materials.reset();  // GPU materials and textures go before the renderer that owns them
    textures.reset();
    renderer->reset();
    if (vp->stats.state != NOOR_VIEWPORT_FAILED) set_state(vp, NOOR_VIEWPORT_STOPPED);
}

// Waits for the render thread while still dispatching this (UI) thread's messages.
void join_pumping_messages(std::thread& thread) {
#if defined(_WIN32)
    HANDLE handle = static_cast<HANDLE>(thread.native_handle());
    for (;;) {
        const DWORD r = MsgWaitForMultipleObjects(1, &handle, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
#endif
    thread.join();
}

std::vector<Guid> parse_ids(const char* json) {
    std::vector<Guid> ids;
    if (!json) return ids;
    auto parsed = noor::asset::parse_json(json, "entity ids");
    if (!parsed || !parsed->is_array()) return ids;
    for (const auto& v : *parsed) {
        if (!v.is_string()) continue;
        if (auto g = Guid::parse(v.get<std::string>())) ids.push_back(*g);
    }
    return ids;
}

thread_local std::string t_events;

#if defined(_WIN32)
noor::runtime::Key key_from_vk(int vk) {
    using noor::runtime::Key;
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(static_cast<int>(Key::A) + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return static_cast<Key>(static_cast<int>(Key::Num0) + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return static_cast<Key>(static_cast<int>(Key::F1) + (vk - VK_F1));
    switch (vk) {
    case VK_SPACE: return Key::Space;
    case VK_RETURN: return Key::Enter;
    case VK_ESCAPE: return Key::Escape;
    case VK_TAB: return Key::Tab;
    case VK_BACK: return Key::Backspace;
    case VK_SHIFT: return Key::Shift;
    case VK_CONTROL: return Key::Ctrl;
    case VK_MENU: return Key::Alt;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    default: return Key::None;
    }
}
#endif

} // namespace

noor::runtime::InputState noor_viewport_game_input(noor_viewport* vp) {
    noor::runtime::InputState in;
    if (!vp || !vp->window) return in;
    const auto pointer = vp->window->pointer_state();
    if (pointer.right_down) {  // the editor camera is flying: the game gets nothing
        vp->game_mouse_valid = false;
        return in;
    }
    in.mouse = {static_cast<float>(pointer.x), static_cast<float>(pointer.y)};
    in.mouse_captured = vp->window->mouse_captured();
    if (in.mouse_captured) {
        const auto [dx, dy] = vp->window->take_raw_motion();
        in.mouse_delta = {static_cast<float>(dx), static_cast<float>(dy)};
    } else if (vp->game_mouse_valid) {
        in.mouse_delta = {static_cast<float>(pointer.x - vp->last_game_mouse_x), static_cast<float>(pointer.y - vp->last_game_mouse_y)};
    }
    vp->last_game_mouse_x = pointer.x;
    vp->last_game_mouse_y = pointer.y;
    vp->game_mouse_valid = true;
    if (!vp->window->has_focus()) return in;
    in.set(noor::runtime::Key::MouseLeft, pointer.left_down);
    in.set(noor::runtime::Key::MouseMiddle, pointer.middle_down);
#if defined(_WIN32)
    for (int vk = 0; vk < 256; ++vk) {
        if (vp->window->key_down(vk)) in.set(key_from_vk(vk));
    }
#endif
    return in;
}

extern "C" {

NOOR_API noor_viewport* noor_viewport_create(void* parent_window, const noor_viewport_desc* desc) {
    auto window = noor::platform::NativeChildWindow::create(parent_window);
    if (!window) {
        noor::editor_api::set_last_error(window.error().describe());
        NOOR_LOG_ERROR("Viewport", "{}", window.error().describe());
        return nullptr;
    }
    auto* vp = new noor_viewport();
    vp->window = std::move(*window);
    vp->enable_validation = desc ? desc->enable_validation != 0 : true;
    vp->created_at = std::chrono::steady_clock::now();
    vp->stats.state = NOOR_VIEWPORT_STARTING;
    vp->render_thread = std::thread(&render_thread_main, vp);
    NOOR_LOG_INFO("Viewport", "Viewport created");
    return vp;
}

NOOR_API void* noor_viewport_native_handle(noor_viewport* viewport) {
    return viewport ? viewport->window->native_handle() : nullptr;
}

NOOR_API void noor_viewport_get_stats(noor_viewport* viewport, noor_viewport_stats* out_stats) {
    if (!viewport || !out_stats) return;
    std::scoped_lock lock(viewport->stats_mutex);
    *out_stats = viewport->stats;
}

NOOR_API void noor_viewport_set_document(noor_viewport* viewport, noor_doc* doc) {
    if (!viewport) return;
    std::scoped_lock lock(viewport->doc_mutex);
    viewport->channel = noor_doc_channel(doc);
}

NOOR_API void noor_viewport_set_preview(noor_viewport* viewport, noor_doc* doc) {
    if (!viewport) return;
    std::scoped_lock lock(viewport->doc_mutex);
    viewport->channel = noor_doc_preview_channel(doc);
}

NOOR_API void noor_viewport_frame(noor_viewport* viewport, const char* entity_ids_json) {
    if (!viewport) return;
    std::scoped_lock lock(viewport->doc_mutex);
    viewport->frame_request = parse_ids(entity_ids_json);
}

NOOR_API void noor_viewport_set_selection(noor_viewport* viewport, const char* entity_ids_json) {
    if (!viewport) return;
    auto ids = parse_ids(entity_ids_json);
    std::scoped_lock lock(viewport->doc_mutex);
    viewport->selection = std::move(ids);
    ++viewport->selection_version;
}

NOOR_API void noor_viewport_set_gizmo(noor_viewport* viewport, const char* entity_id, int32_t mode) {
    if (!viewport) return;
    std::optional<Guid> id;
    if (entity_id) {
        if (auto g = Guid::parse(entity_id)) id = *g;
    }
    std::scoped_lock lock(viewport->doc_mutex);
    viewport->gizmo_entity = id;
    viewport->gizmo_mode = mode == NOOR_GIZMO_MOVE     ? ed::GizmoMode::Move
                           : mode == NOOR_GIZMO_ROTATE ? ed::GizmoMode::Rotate
                           : mode == NOOR_GIZMO_SCALE  ? ed::GizmoMode::Scale
                                                       : ed::GizmoMode::None;
    ++viewport->selection_version;
}

NOOR_API void noor_viewport_set_options(noor_viewport* viewport, const char* options_json) {
    if (!viewport || !options_json) return;
    auto parsed = noor::asset::parse_json(options_json, "viewport options");
    if (!parsed || !parsed->is_object()) {
        NOOR_LOG_WARN("Viewport", "Ignoring viewport options: not a JSON object");
        return;
    }
    const auto& j = *parsed;
    std::scoped_lock lock(viewport->doc_mutex);
    auto& o = viewport->options;
    auto num = [&](const char* key, float& out, float lo, float hi) {
        if (j.contains(key) && j[key].is_number()) out = std::clamp(j[key].get<float>(), lo, hi);
    };
    if (j.contains("snap") && j["snap"].is_boolean()) o.snap = j["snap"].get<bool>();
    num("move_step", o.move_step, 0.001f, 1000.0f);
    num("rotate_step", o.rotate_step, 0.1f, 180.0f);
    num("scale_step", o.scale_step, 0.001f, 100.0f);
    num("fly_speed", o.fly_speed, 0.25f, 2000.0f);
    num("look_sensitivity", o.look_sensitivity, 0.1f, 5.0f);
    num("gizmo_scale", o.gizmo_scale, 0.3f, 3.0f);
    if (j.contains("msaa") && j["msaa"].is_number_integer()) o.msaa = std::clamp(j["msaa"].get<std::uint32_t>(), 1u, 8u);
    ++viewport->options_version;
}

NOOR_API int32_t noor_viewport_world_to_screen(noor_viewport* viewport, const float world[3], float out[3]) {
    if (!viewport || !world || !out) return 0;
    std::scoped_lock lock(viewport->camera_mutex);
    if (!viewport->camera) return 0;
    const auto sp = ed::world_to_screen(*viewport->camera, viewport->camera_width, viewport->camera_height, {world[0], world[1], world[2]});
    if (!sp) return 0;
    out[0] = sp->x;
    out[1] = sp->y;
    out[2] = sp->distance;
    return 1;
}

NOOR_API void noor_viewport_test_pointer(noor_viewport* viewport, int32_t enabled, int32_t x, int32_t y, int32_t buttons) {
    if (!viewport || !viewport->window) return;
    noor::platform::PointerState s;
    s.x = x;
    s.y = y;
    s.left_down = (buttons & 1) != 0;
    s.right_down = (buttons & 2) != 0;
    s.middle_down = (buttons & 4) != 0;
    s.ctrl = (buttons & 8) != 0;
    s.alt = (buttons & 16) != 0;
    viewport->window->set_test_pointer(enabled != 0, s);
}

NOOR_API void noor_viewport_set_play(noor_viewport* viewport, noor_doc* doc) {
    if (!viewport) return;
    std::scoped_lock lock(viewport->play_mutex);  // waits for a tick in progress
    viewport->play_doc = doc;
    viewport->play_ticked = false;
}

NOOR_API void noor_viewport_set_mouse_capture(noor_viewport* viewport, int32_t capture) {
    if (!viewport || !viewport->window) return;
    // Only a focused game view may take the mouse (the player clicked into it).
    if (capture && !viewport->window->has_focus() && !viewport->window->mouse_captured()) return;
    viewport->window->set_mouse_capture(capture != 0);
}

NOOR_API int32_t noor_viewport_has_focus(noor_viewport* viewport) {
    return viewport && viewport->window && viewport->window->has_focus() ? 1 : 0;
}

NOOR_API int32_t noor_viewport_mouse_captured(noor_viewport* viewport) {
    return viewport && viewport->window && viewport->window->mouse_captured() ? 1 : 0;
}

NOOR_API void noor_viewport_set_brush(noor_viewport* viewport, const char* brush_json) {
    if (!viewport) return;
    std::optional<Guid> terrain;
    float radius = 8.0f;
    if (brush_json) {
        if (auto j = noor::asset::parse_json(brush_json, "brush"); j && j->is_object()) {
            if (j->contains("terrain") && (*j)["terrain"].is_string())
                if (auto id = Guid::parse((*j)["terrain"].get<std::string>())) terrain = *id;
            if (j->contains("radius") && (*j)["radius"].is_number()) radius = std::clamp((*j)["radius"].get<float>(), 0.1f, 500.0f);
        }
    }
    std::scoped_lock lock(viewport->doc_mutex);
    viewport->brush_terrain = terrain;
    viewport->brush_radius = radius;
}

NOOR_API void noor_viewport_set_face_highlight(noor_viewport* viewport, const char* brush_id, int32_t face) {
    if (!viewport) return;
    std::scoped_lock lock(viewport->doc_mutex);
    viewport->face_brush.reset();
    if (brush_id)
        if (auto id = Guid::parse(brush_id)) viewport->face_brush = *id;
    viewport->face_index = static_cast<std::uint32_t>(std::max(0, face));
    ++viewport->face_version;
}

NOOR_API const char* noor_viewport_render_stats(noor_viewport* viewport) {
    thread_local std::string t_render_stats;
    t_render_stats = "{}";
    if (viewport) {
        std::scoped_lock lock(viewport->stats_mutex);
        t_render_stats = viewport->render_stats;
    }
    return t_render_stats.c_str();
}

NOOR_API const char* noor_viewport_poll_events(noor_viewport* viewport) {
    noor::asset::Json events = noor::asset::Json::array();
    if (viewport) {
        std::scoped_lock lock(viewport->event_mutex);
        std::swap(events, viewport->events);
    }
    t_events = events.dump();
    return t_events.c_str();
}

NOOR_API void noor_viewport_destroy(noor_viewport* viewport) {
    if (!viewport) return;
    const auto start = std::chrono::steady_clock::now();
    viewport->stop.store(true);
    if (viewport->render_thread.joinable()) join_pumping_messages(viewport->render_thread);
    viewport->window.reset();  // destroys the child window on the UI thread
    NOOR_LOG_INFO("Viewport", "Viewport destroyed in {:.0f} ms",
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    delete viewport;
}

} // extern "C"
