#pragma once

#include "noor/foundation/error.h"
#include "noor/platform/presentation_target.h"

#include <cstdint>
#include <memory>
#include <string>

namespace noor::platform {

struct WindowDesc {
    std::string title = "NoorEngine";
    std::uint32_t width = 1280;  // logical size; actual pixels depend on display scale
    std::uint32_t height = 720;
    bool resizable = true;
};

// A top-level OS window owned by the engine (SDL3), able to host a Vulkan surface.
class Window final : public PresentationTarget {
public:
    [[nodiscard]] static Result<std::unique_ptr<Window>> create(const WindowDesc& desc);
    ~Window() override;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Drawable size in physical pixels (high-DPI aware). Zero while minimized.
    [[nodiscard]] PixelSize pixel_size() const override;
    [[nodiscard]] float display_scale() const;
    [[nodiscard]] bool is_minimized() const;
    void set_title(const std::string& title);

    // Window management. Each call waits until the OS has applied the change (SDL_SyncWindow),
    // which makes them usable from automated lifecycle tests.
    void minimize();
    void restore();
    void set_size(std::uint32_t width, std::uint32_t height);  // logical size
    [[nodiscard]] std::uint32_t display_index() const;
    bool move_to_display(std::uint32_t index);  // centres the window there; false if no such display

    [[nodiscard]] Result<std::vector<const char*>> vulkan_instance_extensions() const override;
    [[nodiscard]] Result<VkSurfaceKHR> create_vulkan_surface(VkInstance instance) const override;
    void destroy_vulkan_surface(VkInstance instance, VkSurfaceKHR surface) const override;

    // Opaque backend handle for the platform layer itself. Not for general use.
    [[nodiscard]] void* backend_handle() const noexcept { return handle_; }

private:
    explicit Window(void* handle) : handle_(handle) {}
    void* handle_ = nullptr;
};

// Number of connected displays (monitors).
[[nodiscard]] std::uint32_t display_count();

} // namespace noor::platform
