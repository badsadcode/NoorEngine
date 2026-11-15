#pragma once
// Something the renderer can present frames to: an SDL window owned by the engine,
// or a native child window embedded in the editor UI. The renderer only sees this
// interface, so it does not care who owns the OS window.

#include "noor/foundation/error.h"

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace noor::platform {

struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend bool operator==(const PixelSize&, const PixelSize&) = default;
};

class PresentationTarget {
public:
    virtual ~PresentationTarget() = default;

    // Current drawable size in physical pixels; zero when hidden or minimized.
    // Must be safe to call from the render thread.
    [[nodiscard]] virtual PixelSize pixel_size() const = 0;

    // Instance extensions needed to create a surface for this target.
    [[nodiscard]] virtual Result<std::vector<const char*>> vulkan_instance_extensions() const = 0;

    [[nodiscard]] virtual Result<VkSurfaceKHR> create_vulkan_surface(VkInstance instance) const = 0;
    virtual void destroy_vulkan_surface(VkInstance instance, VkSurfaceKHR surface) const = 0;
};

} // namespace noor::platform
