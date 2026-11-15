#include "noor/platform/platform.h"

#include "noor/foundation/log.h"

#include <SDL3/SDL.h>
#include <format>

namespace noor::platform {
namespace {
bool g_initialized = false;
std::uint64_t g_start_ticks = 0;
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
    SDL_Quit();
    g_initialized = false;
    NOOR_LOG_INFO("Platform", "Platform shut down");
}

EventSummary pump_events() {
    EventSummary summary;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: summary.quit_requested = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: summary.window_resized = true; break;
        case SDL_EVENT_WINDOW_MINIMIZED: summary.window_minimized = true; break;
        case SDL_EVENT_WINDOW_RESTORED: summary.window_restored = true; break;
        default: break;
        }
    }
    return summary;
}

void wait_for_events(std::uint32_t timeout_ms) {
    SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(timeout_ms));
}

double time_seconds() {
    const auto ticks = SDL_GetPerformanceCounter() - g_start_ticks;
    return static_cast<double>(ticks) / static_cast<double>(SDL_GetPerformanceFrequency());
}

} // namespace noor::platform
