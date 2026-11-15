#include "noor/platform/vulkan_bridge.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <format>

namespace noor::platform {

Result<std::vector<const char*>> vulkan_instance_extensions() {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) {
        return fail(ErrorCode::PlatformError,
                    std::format("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError()));
    }
    return std::vector<const char*>(names, names + count);
}

Result<VkSurfaceKHR> create_vulkan_surface(const Window& window, VkInstance instance) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(window.backend_handle()), instance, nullptr,
                                  &surface)) {
        return fail(ErrorCode::PlatformError, std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
    }
    return surface;
}

void destroy_vulkan_surface(VkInstance instance, VkSurfaceKHR surface) {
    if (surface != VK_NULL_HANDLE) {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    }
}

} // namespace noor::platform
