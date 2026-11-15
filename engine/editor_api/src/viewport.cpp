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
#include "noor/platform/native_child_window.h"
#include "noor/render/renderer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
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
    } options;
    std::uint64_t options_version = 0;

    // The camera of the last drawn frame (for noor_viewport_world_to_screen).
    std::mutex camera_mutex;
    std::optional<ed::CameraBasis> camera;
    float camera_width = 0, camera_height = 0;

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

using Geometry = std::pair<std::vector<noor::render::Vertex>, std::vector<std::uint32_t>>;

// An axis-aligned box from lo to hi, one color, counter-clockwise faces.
Geometry make_box(Vec3 color, Vec3 lo = {-1, -1, -1}, Vec3 hi = {1, 1, 1}) {
    struct Face { Vec3 n, u, v; };
    const Face faces[] = {{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                          {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
                          {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}};
    const Vec3 c = (lo + hi) * 0.5f, h = (hi - lo) * 0.5f;
    Geometry g;
    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(g.first.size());
        for (const Vec3 p : {f.n - f.u - f.v, f.n + f.u - f.v, f.n + f.u + f.v, f.n - f.u + f.v})
            g.first.push_back({{c.x + p.x * h.x, c.y + p.y * h.y, c.z + p.z * h.z}, f.n, color});
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) g.second.push_back(base + i);
    }
    return g;
}

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

    std::unordered_map<std::string, std::optional<std::vector<LoadedMesh>>> assets;  // by absolute path
    std::shared_ptr<const ed::RenderSnapshot> shown;

    // Per snapshot: what to draw, and for each draw the entity it belongs to.
    struct SceneDraw {
        noor::render::DrawItem item;
        Guid entity;
        const LoadedMesh* mesh = nullptr;  // null for markers
    };
    std::vector<SceneDraw> scene_draws;
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

    while (!vp->stop.load()) {
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
        if (snap != shown) {
            shown = snap;
            scene_draws.clear();
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
                    auto [it, fresh] = assets.try_emplace(m.asset_path);
                    if (fresh) {
                        if (auto model = noor::asset::import_gltf(m.asset_path)) {
                            std::vector<LoadedMesh> loaded;
                            for (const auto& mesh : model->meshes) {
                                auto h = r.create_mesh({{reinterpret_cast<const noor::render::Vertex*>(mesh.vertices.data()),
                                                         mesh.vertices.size()},
                                                        mesh.indices, mesh.name});
                                LoadedMesh lm{h ? *h : noor::render::MeshHandle{}, {1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, {}, {}};
                                lm.positions.reserve(mesh.vertices.size());
                                for (const auto& v : mesh.vertices) {
                                    lm.positions.push_back(v.position);
                                    grow(lm.lo, lm.hi, v.position);
                                }
                                lm.indices.assign(mesh.indices.begin(), mesh.indices.end());
                                loaded.push_back(std::move(lm));
                            }
                            it->second = std::move(loaded);
                        } else {
                            NOOR_LOG_WARN("Viewport", "{}", model.error().describe());
                        }
                    }
                    if (!it->second || m.mesh_index < 0 || m.mesh_index >= static_cast<int>(it->second->size())) continue;
                    const LoadedMesh& lm = (*it->second)[static_cast<std::size_t>(m.mesh_index)];
                    if (!lm.handle.valid()) continue;
                    scene_draws.push_back({{lm.handle, m.world}, m.entity, &lm});
                    for (int c = 0; c < 8; ++c) {
                        grow(lo, hi, m.world.transform_point({c & 1 ? lm.hi.x : lm.lo.x, c & 2 ? lm.hi.y : lm.lo.y, c & 4 ? lm.hi.z : lm.lo.z}));
                        any = true;
                    }
                }
                if (marker.valid()) {
                    for (const auto& mk : snap->markers)
                        scene_draws.push_back({{marker, mk.world * noor::math::scale({0.25f, 0.25f, 0.25f})}, mk.entity, nullptr});
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

        if (flags_dirty) {
            flags_dirty = false;
            std::unordered_set<Guid> selected(selection.begin(), selection.end());
            const auto moving = gizmo_entity ? subtree_of(*gizmo_entity) : std::unordered_set<Guid>{};
            draw_selected.assign(scene_draws.size(), 0);
            draw_in_gizmo_subtree.assign(scene_draws.size(), 0);
            for (std::size_t i = 0; i < scene_draws.size(); ++i) {
                draw_selected[i] = selected.contains(scene_draws[i].entity);
                draw_in_gizmo_subtree[i] = moving.contains(scene_draws[i].entity);
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

        const auto basis = camera.basis();
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

        if (pressed && alt_orbit) {
            click_pending = false;
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
                        }
                    }
                }
                push_event(vp, {{"type", "click"}, {"entity", hit ? noor::asset::Json(hit->to_string()) : noor::asset::Json(nullptr)}});
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
            for (std::size_t i = 0; i < scene_draws.size(); ++i) {
                noor::render::DrawItem item = scene_draws[i].item;
                if (preview && i < draw_in_gizmo_subtree.size() && draw_in_gizmo_subtree[i]) item.world_from_object = preview->delta * item.world_from_object;
                if (i < draw_selected.size() && draw_selected[i]) item.tint = highlight;
                frame_draws.push_back(item);
            }
            if (ground.valid()) {
                const float half = std::max({200.0f, scene_hi.x - scene_lo.x, scene_hi.z - scene_lo.z});
                const Vec3 c = (scene_lo + scene_hi) * 0.5f;
                frame_draws.push_back({ground, noor::math::translation({c.x, -0.05f, c.z}) * noor::math::scale({half, 0.05f, half})});
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
            frame.camera.view = camera.view();
            frame.camera.fov_y = camera.fov_y;
            frame.camera.near_plane = std::max(0.05f, camera.distance * 0.002f);
            frame.draws = frame_draws;
        } else {
            r.set_clear_color(demo_clear_color(pointer, size));
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
    }

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

} // namespace

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
