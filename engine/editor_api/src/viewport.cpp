// Editor viewport: native child window + dedicated render thread.
//
// The UI thread owns the child window (Win32 rule). The render thread owns all Vulkan
// objects; it creates the renderer, draws until asked to stop, then destroys it. While
// the UI thread waits for the render thread to finish, it keeps pumping messages,
// because presentation may need the window's thread to respond.

#include "noor_editor_api.h"

#include "api_internal.h"
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
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace std::chrono_literals;

struct noor_viewport {
    std::unique_ptr<noor::platform::NativeChildWindow> window;
    std::thread render_thread;
    std::atomic<bool> stop{false};
    bool enable_validation = true;
    std::chrono::steady_clock::time_point created_at;

    std::mutex stats_mutex;
    noor_viewport_stats stats{};
};

namespace {

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

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

// Spike input demo: Noor purple at rest; hovering shifts the hue with the pointer's
// x position, and holding the left button brightens it. Proves input reaches the viewport.
std::array<float, 4> clear_color_for(const noor::platform::PointerState& p, noor::platform::PixelSize size) {
    std::array<float, 3> srgb{0x6B / 255.0f, 0x3F / 255.0f, 0xD9 / 255.0f};
    if (p.inside && size.width > 0) {
        const float t = std::clamp(static_cast<float>(p.x) / static_cast<float>(size.width), 0.0f, 1.0f);
        srgb = hsv_to_srgb(t * 359.0f, 0.71f, p.left_down ? 1.0f : 0.85f);
    }
    return {srgb_to_linear(srgb[0]), srgb_to_linear(srgb[1]), srgb_to_linear(srgb[2]), 1.0f};
}

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

    noor::platform::PixelSize last_size = vp->window->pixel_size();
    auto last_fps_time = std::chrono::steady_clock::now();
    std::uint64_t frames_at_last_fps = 0;
    double fps = 0.0;

    while (!vp->stop.load()) {
        const noor::platform::PixelSize size = vp->window->pixel_size();
        if (size != last_size) {
            (*renderer)->notify_resized();
            last_size = size;
        }
        (*renderer)->set_clear_color(clear_color_for(vp->window->pointer_state(), size));

        auto frame = (*renderer)->render_frame();
        if (!frame) {
            NOOR_LOG_ERROR("Viewport", "Frame failed: {}", frame.error().describe());
            set_state(vp, NOOR_VIEWPORT_FAILED);
            break;
        }
        if (*frame == noor::render::FrameResult::Skipped) {
            std::this_thread::sleep_for(10ms);  // hidden/zero-sized: don't spin
        }

        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t presented = (*renderer)->frames_presented();
        if (now - last_fps_time >= 1s) {
            fps = static_cast<double>(presented - frames_at_last_fps) /
                  std::chrono::duration<double>(now - last_fps_time).count();
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
        vp->stats.swapchain_rebuilds = (*renderer)->swapchain_rebuilds();
        vp->stats.validation_errors = (*renderer)->validation_error_count();
    }

    renderer->reset();
    if (vp->stats.state != NOOR_VIEWPORT_FAILED) {
        set_state(vp, NOOR_VIEWPORT_STOPPED);
    }
}

// Waits for the render thread while still dispatching this (UI) thread's messages.
void join_pumping_messages(std::thread& thread) {
#if defined(_WIN32)
    HANDLE handle = static_cast<HANDLE>(thread.native_handle());
    for (;;) {
        const DWORD r = MsgWaitForMultipleObjects(1, &handle, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) {
            break;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
#endif
    thread.join();
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
    if (!viewport || !out_stats) {
        return;
    }
    std::scoped_lock lock(viewport->stats_mutex);
    *out_stats = viewport->stats;
}

NOOR_API void noor_viewport_destroy(noor_viewport* viewport) {
    if (!viewport) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    viewport->stop.store(true);
    if (viewport->render_thread.joinable()) {
        join_pumping_messages(viewport->render_thread);
    }
    viewport->window.reset();  // destroys the child window on the UI thread
    NOOR_LOG_INFO("Viewport", "Viewport destroyed in {:.0f} ms",
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    delete viewport;
}

} // extern "C"
