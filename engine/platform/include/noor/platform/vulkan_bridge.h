#pragma once
// The one place where the platform layer knows about Vulkan: instance extensions
// and surface creation for a Window. Used only by noor::render.

#include "noor/foundation/error.h"
#include "noor/platform/window.h"

#include <vector>
#include <vulkan/vulkan.h>

namespace noor::platform {

// Instance extensions the OS needs to present to a window (e.g. VK_KHR_win32_surface).
[[nodiscard]] Result<std::vector<const char*>> vulkan_instance_extensions();

[[nodiscard]] Result<VkSurfaceKHR> create_vulkan_surface(const Window& window, VkInstance instance);

void destroy_vulkan_surface(VkInstance instance, VkSurfaceKHR surface);

} // namespace noor::platform
