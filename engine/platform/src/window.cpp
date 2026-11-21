#include "noor/platform/window.h"

#include "noor/foundation/log.h"

#include <SDL3/SDL.h>
#include <format>

namespace noor::platform {
namespace {
SDL_Window* sdl(void* handle) { return static_cast<SDL_Window*>(handle); }
} // namespace

Result<std::unique_ptr<Window>> Window::create(const WindowDesc& desc) {
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.mode == WindowDesc::Mode::Fullscreen) flags |= SDL_WINDOW_FULLSCREEN;  // no display mode set: the desktop's
    if (desc.mode == WindowDesc::Mode::Borderless) flags |= SDL_WINDOW_BORDERLESS;
    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.width),
                                          static_cast<int>(desc.height), flags);
    if (!window) {
        return fail(ErrorCode::PlatformError, std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }
    auto result = std::unique_ptr<Window>(new Window(window));
    const PixelSize pixels = result->pixel_size();
    NOOR_LOG_INFO("Platform", "Window '{}' created: {}x{} logical, {}x{} pixels, scale {:.2f}", desc.title,
                  desc.width, desc.height, pixels.width, pixels.height, result->display_scale());
    return result;
}

Window::~Window() {
    if (handle_) {
        SDL_DestroyWindow(sdl(handle_));
    }
}

PixelSize Window::pixel_size() const {
    int w = 0;
    int h = 0;
    if (!SDL_GetWindowSizeInPixels(sdl(handle_), &w, &h) || is_minimized()) {
        return {};
    }
    return {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
}

float Window::display_scale() const { return SDL_GetWindowDisplayScale(sdl(handle_)); }

bool Window::is_minimized() const { return (SDL_GetWindowFlags(sdl(handle_)) & SDL_WINDOW_MINIMIZED) != 0; }

void Window::set_title(const std::string& title) { SDL_SetWindowTitle(sdl(handle_), title.c_str()); }

void Window::minimize() {
    SDL_MinimizeWindow(sdl(handle_));
    SDL_SyncWindow(sdl(handle_));
}

void Window::restore() {
    SDL_RestoreWindow(sdl(handle_));
    SDL_SyncWindow(sdl(handle_));
}

void Window::set_fullscreen(bool on) {
    SDL_SetWindowFullscreen(sdl(handle_), on);
    SDL_SyncWindow(sdl(handle_));
}

void Window::set_size(std::uint32_t width, std::uint32_t height) {
    SDL_SetWindowSize(sdl(handle_), static_cast<int>(width), static_cast<int>(height));
    SDL_SyncWindow(sdl(handle_));
}

std::uint32_t Window::display_index() const {
    const SDL_DisplayID current = SDL_GetDisplayForWindow(sdl(handle_));
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    std::uint32_t index = 0;
    for (int i = 0; displays && i < count; ++i) {
        if (displays[i] == current) index = static_cast<std::uint32_t>(i);
    }
    SDL_free(displays);
    return index;
}

bool Window::move_to_display(std::uint32_t index) {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    const bool ok = displays && index < static_cast<std::uint32_t>(count);
    if (ok) {
        const SDL_DisplayID id = displays[index];
        SDL_SetWindowPosition(sdl(handle_), SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        SDL_SyncWindow(sdl(handle_));
    }
    SDL_free(displays);
    return ok;
}

std::uint32_t display_count() {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_free(displays);
    return static_cast<std::uint32_t>(count);
}

} // namespace noor::platform
