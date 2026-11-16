#include "noor/platform/platform.h"

#include "noor/foundation/log.h"
#include "noor/platform/window.h"

#include <SDL3/SDL.h>
#include <format>

namespace noor::platform {
namespace {
bool g_initialized = false;
std::uint64_t g_start_ticks = 0;
InputState g_input;
std::function<void()> g_live_redraw;
bool g_in_live_redraw = false;

// SDL calls event watchers as events are produced, including from inside the Windows modal
// size/move loop, where our own frame loop is blocked. Redraw on exposure and size changes.
bool SDLCALL live_redraw_watch(void*, SDL_Event* event) {
    if (g_live_redraw && !g_in_live_redraw &&
        (event->type == SDL_EVENT_WINDOW_EXPOSED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)) {
        g_in_live_redraw = true;
        g_live_redraw();
        g_in_live_redraw = false;
    }
    return true;
}

Key key_from_scancode(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + (sc - SDL_SCANCODE_A));
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        return static_cast<Key>(static_cast<int>(Key::Num1) + (sc - SDL_SCANCODE_1));
    }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (sc - SDL_SCANCODE_F1));
    }
    switch (sc) {
    case SDL_SCANCODE_0: return Key::Num0;
    case SDL_SCANCODE_SPACE: return Key::Space;
    case SDL_SCANCODE_RETURN: return Key::Enter;
    case SDL_SCANCODE_ESCAPE: return Key::Escape;
    case SDL_SCANCODE_TAB: return Key::Tab;
    case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
    case SDL_SCANCODE_LSHIFT: return Key::LeftShift;
    case SDL_SCANCODE_RSHIFT: return Key::RightShift;
    case SDL_SCANCODE_LCTRL: return Key::LeftCtrl;
    case SDL_SCANCODE_RCTRL: return Key::RightCtrl;
    case SDL_SCANCODE_LALT: return Key::LeftAlt;
    case SDL_SCANCODE_RALT: return Key::RightAlt;
    case SDL_SCANCODE_UP: return Key::Up;
    case SDL_SCANCODE_DOWN: return Key::Down;
    case SDL_SCANCODE_LEFT: return Key::Left;
    case SDL_SCANCODE_RIGHT: return Key::Right;
    default: return Key::Unknown;
    }
}

int button_index(Uint8 sdl_button) {
    switch (sdl_button) {
    case SDL_BUTTON_LEFT: return static_cast<int>(MouseButton::Left);
    case SDL_BUTTON_RIGHT: return static_cast<int>(MouseButton::Right);
    case SDL_BUTTON_MIDDLE: return static_cast<int>(MouseButton::Middle);
    default: return -1;
    }
}

} // namespace

Status initialize() {
    if (g_initialized) {
        return {};
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return fail(ErrorCode::PlatformError, std::format("SDL_Init failed: {}", SDL_GetError()));
    }
    g_initialized = true;
    g_start_ticks = SDL_GetPerformanceCounter();
    g_input = {};

    const int version = SDL_GetVersion();
    NOOR_LOG_INFO("Platform", "SDL {}.{}.{} initialized, video driver '{}'",
                  SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
                  SDL_VERSIONNUM_MICRO(version), SDL_GetCurrentVideoDriver());
    return {};
}

void shutdown() {
    if (!g_initialized) {
        return;
    }
    set_live_redraw_callback({});
    SDL_Quit();
    g_initialized = false;
    NOOR_LOG_INFO("Platform", "Platform shut down");
}

EventSummary pump_events() {
    EventSummary summary;
    g_input.begin_frame();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: summary.quit_requested = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: summary.window_resized = true; break;
        case SDL_EVENT_WINDOW_MINIMIZED: summary.window_minimized = true; break;
        case SDL_EVENT_WINDOW_RESTORED: summary.window_restored = true; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_input.release_all();
            summary.focus_lost = true;
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const Key key = key_from_scancode(event.key.scancode);
            if (key == Key::Unknown) break;
            const auto i = static_cast<std::size_t>(key);
            const bool down = event.type == SDL_EVENT_KEY_DOWN;
            if (down && !event.key.repeat && !g_input.keys_down[i]) g_input.keys_pressed[i] = true;
            if (!down && g_input.keys_down[i]) g_input.keys_released[i] = true;
            g_input.keys_down[i] = down;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int b = button_index(event.button.button);
            if (b < 0) break;
            const auto i = static_cast<std::size_t>(b);
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (down && !g_input.buttons_down[i]) g_input.buttons_pressed[i] = true;
            if (!down && g_input.buttons_down[i]) g_input.buttons_released[i] = true;
            g_input.buttons_down[i] = down;
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            g_input.mouse_x = event.motion.x;
            g_input.mouse_y = event.motion.y;
            g_input.mouse_dx += event.motion.xrel;
            g_input.mouse_dy += event.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            g_input.wheel += event.wheel.y;
            break;
        default: break;
        }
    }
    return summary;
}

const InputState& input() noexcept { return g_input; }

void set_relative_mouse(Window& window, bool enabled) {
    if (!SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window*>(window.backend_handle()), enabled)) {
        NOOR_LOG_WARN("Platform", "Relative mouse mode {} failed: {}", enabled ? "on" : "off", SDL_GetError());
    }
}

void set_live_redraw_callback(std::function<void()> callback) {
    const bool had = static_cast<bool>(g_live_redraw);
    g_live_redraw = std::move(callback);
    if (g_live_redraw && !had) {
        SDL_AddEventWatch(&live_redraw_watch, nullptr);
    } else if (!g_live_redraw && had) {
        SDL_RemoveEventWatch(&live_redraw_watch, nullptr);
    }
}

void wait_for_events(std::uint32_t timeout_ms) {
    SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(timeout_ms));
}

double time_seconds() {
    const auto ticks = SDL_GetPerformanceCounter() - g_start_ticks;
    return static_cast<double>(ticks) / static_cast<double>(SDL_GetPerformanceFrequency());
}

} // namespace noor::platform
