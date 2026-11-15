#pragma once

#include "noor/foundation/error.h"

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

struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// A top-level OS window that can host a Vulkan surface.
class Window {
public:
    [[nodiscard]] static Result<std::unique_ptr<Window>> create(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Drawable size in physical pixels (high-DPI aware). Zero while minimized.
    [[nodiscard]] PixelSize pixel_size() const;
    [[nodiscard]] float display_scale() const;
    [[nodiscard]] bool is_minimized() const;
    void set_title(const std::string& title);

    // Opaque backend handle for platform bridges (e.g. vulkan_bridge). Not for general use.
    [[nodiscard]] void* backend_handle() const noexcept { return handle_; }

private:
    explicit Window(void* handle) : handle_(handle) {}
    void* handle_ = nullptr;
};

} // namespace noor::platform
