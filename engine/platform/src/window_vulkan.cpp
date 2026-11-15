// Vulkan surface support for SDL-owned windows.

#include "noor/platform/window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <format>

namespace noor::platform {

Result<std::vector<const char*>> Window::vulkan_instance_extensions() const {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) {
        return fail(ErrorCode::PlatformError,
                    std::format("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError()));
    }
    return std::vector<const char*>(names, names + count);
}

Result<VkSurfaceKHR> Window::create_vulkan_surface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(handle_), instance, nullptr, &surface)) {
        return fail(ErrorCode::PlatformError, std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
    }
    return surface;
}

void Window::destroy_vulkan_surface(VkInstance instance, VkSurfaceKHR surface) const {
    if (surface != VK_NULL_HANDLE) {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    }
}

} // namespace noor::platform
