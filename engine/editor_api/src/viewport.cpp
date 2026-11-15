// Editor viewport: native child window + dedicated render thread.
//
// The UI thread owns the child window (Win32 rule). The render thread owns all Vulkan
// objects; it creates the renderer, draws until asked to stop, then destroys it. While
// the UI thread waits for the render thread to finish, it keeps pumping messages,
// because presentation may need the window's thread to respond.
//
// With a document attached (ADR-025) the viewport draws the document's scene: it picks up
// each published RenderSnapshot, loads the GLBs it references (on the render thread) and
// draws every mesh with its world matrix. Camera: right-drag orbits, middle-drag pans, the
// wheel zooms. Without a document it shows the spike's input demo (hue follows the pointer).

#include "noor_editor_api.h"

#include "api_internal.h"
#include "document.h"
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
using noor::math::Vec3;

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
    std::shared_ptr<noor::editor::SnapshotChannel> channel;
    std::optional<std::vector<noor::Guid>> frame_request;  // empty vector = frame everything
};

namespace {

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

Vec3 srgb(std::uint32_t rgb) {
    return {srgb_to_linear(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f),
            srgb_to_linear(static_cast<float>((rgb >> 8) & 0xFF) / 255.0f),
            srgb_to_linear(static_cast<float>(rgb & 0xFF) / 255.0f)};
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

// A unit box (−1..1), one color, counter-clockwise faces: the ground slab and marker cubes.
std::pair<std::vector<noor::render::Vertex>, std::vector<std::uint32_t>> make_box(Vec3 color) {
    struct Face { Vec3 n, u, v; };
    const Face faces[] = {{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                          {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
                          {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}};
    std::vector<noor::render::Vertex> v;
    std::vector<std::uint32_t> idx;
    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(v.size());
        for (const Vec3 p : {f.n - f.u - f.v, f.n + f.u - f.v, f.n + f.u + f.v, f.n - f.u + f.v}) v.push_back({p, f.n, color});
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
    }
    return {v, idx};
}

// Orbit camera: yaw/pitch/distance around a target.
struct OrbitCamera {
    Vec3 target{0, 0, 0};
    float yaw = 0.6f;
    float pitch = 0.45f;
    float distance = 30.0f;

    [[nodiscard]] Vec3 eye() const {
        return target + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)} * distance;
    }
    [[nodiscard]] noor::math::Mat4 view() const { return noor::math::look_at(eye(), target, {0, 1, 0}); }

    void frame(Vec3 lo, Vec3 hi) {
        target = (lo + hi) * 0.5f;
        const float radius = std::max(1.0f, noor::math::length(hi - lo) * 0.5f);
        distance = radius / std::sin(noor::math::radians(25.0f)) * 1.1f;  // fits a 50° field of view
    }
};

struct LoadedAsset {
    std::vector<noor::render::MeshHandle> handles;
    std::vector<std::pair<Vec3, Vec3>> bounds;  // per mesh, local space
};

void set_state(noor_viewport* vp, noor_viewport_state state) {
    std::scoped_lock lock(vp->stats_mutex);
    vp->stats.state = state;
}

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
    const auto [ground_v, ground_i] = make_box(srgb(0x26222F));
    const auto [marker_v, marker_i] = make_box(srgb(0x22D3EE));
    const auto ground = r.create_mesh({ground_v, ground_i, "Editor ground"});
    const auto marker = r.create_mesh({marker_v, marker_i, "Editor marker"});

    std::unordered_map<std::string, std::optional<LoadedAsset>> assets;  // by absolute path
    std::shared_ptr<const noor::editor::RenderSnapshot> shown;
    std::vector<noor::render::DrawItem> draws;
    Vec3 scene_lo{-10, 0, -10}, scene_hi{10, 10, 10};
    bool framed_once = false;
    OrbitCamera camera;

    noor::platform::PixelSize last_size = vp->window->pixel_size();
    noor::platform::PointerState last_pointer = vp->window->pointer_state();
    auto last_fps_time = std::chrono::steady_clock::now();
    std::uint64_t frames_at_last_fps = 0;
    double fps = 0.0;

    while (!vp->stop.load()) {
        std::shared_ptr<noor::editor::SnapshotChannel> channel;
        std::optional<std::vector<noor::Guid>> frame_request;
        {
            std::scoped_lock lock(vp->doc_mutex);
            channel = vp->channel;
            frame_request = std::exchange(vp->frame_request, std::nullopt);
        }

        // New scene state? Load what it needs and rebuild the draw list.
        auto snap = channel ? channel->get() : nullptr;
        if (snap != shown) {
            shown = snap;
            draws.clear();
            bool any = false;
            Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            if (snap) {
                for (const auto& m : snap->meshes) {
                    auto [it, fresh] = assets.try_emplace(m.asset_path);
                    if (fresh) {
                        if (auto model = noor::asset::import_gltf(m.asset_path)) {
                            LoadedAsset loaded;
                            for (const auto& mesh : model->meshes) {
                                auto h = r.create_mesh({{reinterpret_cast<const noor::render::Vertex*>(mesh.vertices.data()),
                                                         mesh.vertices.size()},
                                                        mesh.indices, mesh.name});
                                loaded.handles.push_back(h ? *h : noor::render::MeshHandle{});
                                Vec3 a{1e30f, 1e30f, 1e30f}, b{-1e30f, -1e30f, -1e30f};
                                for (const auto& v : mesh.vertices) {
                                    a = {std::min(a.x, v.position.x), std::min(a.y, v.position.y), std::min(a.z, v.position.z)};
                                    b = {std::max(b.x, v.position.x), std::max(b.y, v.position.y), std::max(b.z, v.position.z)};
                                }
                                loaded.bounds.emplace_back(a, b);
                            }
                            it->second = std::move(loaded);
                        } else {
                            NOOR_LOG_WARN("Viewport", "{}", model.error().describe());
                        }
                    }
                    if (!it->second || m.mesh_index < 0 || m.mesh_index >= static_cast<int>(it->second->handles.size())) continue;
                    const auto handle = it->second->handles[static_cast<std::size_t>(m.mesh_index)];
                    if (!handle.valid()) continue;
                    draws.push_back({handle, m.world});
                    const auto [a, b] = it->second->bounds[static_cast<std::size_t>(m.mesh_index)];
                    for (int c = 0; c < 8; ++c) {
                        const Vec3 p = m.world.transform_point({c & 1 ? b.x : a.x, c & 2 ? b.y : a.y, c & 4 ? b.z : a.z});
                        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                        any = true;
                    }
                }
                if (marker) {
                    for (const auto& w : snap->markers) draws.push_back({*marker, w * noor::math::scale({0.25f, 0.25f, 0.25f})});
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
        }

        // Frame request from the editor (F key, double-click in the hierarchy...).
        if (frame_request && shown) {
            if (frame_request->empty()) {
                camera.frame(scene_lo, scene_hi);
            } else {
                const std::unordered_set<noor::Guid> wanted(frame_request->begin(), frame_request->end());
                Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
                bool any = false;
                for (const auto& m : shown->meshes) {
                    if (!wanted.contains(m.entity)) continue;
                    const auto it = assets.find(m.asset_path);
                    if (it == assets.end() || !it->second || m.mesh_index < 0 ||
                        m.mesh_index >= static_cast<int>(it->second->bounds.size()))
                        continue;
                    const auto [a, b] = it->second->bounds[static_cast<std::size_t>(m.mesh_index)];
                    for (int c = 0; c < 8; ++c) {
                        const Vec3 p = m.world.transform_point({c & 1 ? b.x : a.x, c & 2 ? b.y : a.y, c & 4 ? b.z : a.z});
                        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                        any = true;
                    }
                }
                if (any) camera.frame(lo, hi);
            }
        }

        // Camera input.
        const auto pointer = vp->window->pointer_state();
        const float dx = static_cast<float>(pointer.x - last_pointer.x);
        const float dy = static_cast<float>(pointer.y - last_pointer.y);
        if (pointer.right_down && last_pointer.right_down) {
            camera.yaw -= dx * 0.006f;
            camera.pitch = std::clamp(camera.pitch + dy * 0.006f, -1.5f, 1.5f);
        } else if (pointer.middle_down && last_pointer.middle_down) {
            const Vec3 fwd = noor::math::normalize(camera.target - camera.eye());
            const Vec3 right = noor::math::normalize(noor::math::cross(fwd, {0, 1, 0}));
            const Vec3 up = noor::math::cross(right, fwd);
            const float k = camera.distance * 0.0015f;
            camera.target = camera.target - right * (dx * k) + up * (dy * k);
        }
        if (const float wheel = vp->window->take_wheel(); wheel != 0.0f) {
            camera.distance = std::clamp(camera.distance * std::pow(0.88f, wheel), 0.5f, 50'000.0f);
        }
        last_pointer = pointer;

        const noor::platform::PixelSize size = vp->window->pixel_size();
        if (size != last_size) {
            r.notify_resized();
            last_size = size;
        }

        noor::render::FrameDesc frame;
        std::vector<noor::render::DrawItem> frame_draws;
        if (channel) {
            r.set_clear_color({srgb(0x14121C).x, srgb(0x14121C).y, srgb(0x14121C).z, 1.0f});
            frame_draws = draws;
            if (ground) {
                const float half = std::max({200.0f, scene_hi.x - scene_lo.x, scene_hi.z - scene_lo.z});
                const Vec3 c = (scene_lo + scene_hi) * 0.5f;
                frame_draws.push_back({*ground, noor::math::translation({c.x, -0.05f, c.z}) * noor::math::scale({half, 0.05f, half})});
            }
            frame.camera.view = camera.view();
            frame.camera.fov_y = noor::math::radians(50.0f);
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

std::vector<noor::Guid> parse_ids(const char* json) {
    std::vector<noor::Guid> ids;
    if (!json) return ids;
    auto parsed = noor::asset::parse_json(json, "entity ids");
    if (!parsed || !parsed->is_array()) return ids;
    for (const auto& v : *parsed) {
        if (!v.is_string()) continue;
        if (auto g = noor::Guid::parse(v.get<std::string>())) ids.push_back(*g);
    }
    return ids;
}

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
