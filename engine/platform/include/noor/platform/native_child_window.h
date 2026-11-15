#pragma once
// A native child window created inside a window owned by someone else (e.g. the
// NoorEditor UI toolkit), used as a Vulkan viewport. Windows-only for now.
//
// Threading: create() and the destructor must run on the thread that owns the
// parent window (the UI thread), because Win32 windows belong to the thread that
// created them. pixel_size(), input_state() and the Vulkan functions are safe to
// call from a render thread.

#include "noor/foundation/error.h"
#include "noor/platform/presentation_target.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace noor::platform {

// Latest pointer state inside the child window, in physical pixels.
struct PointerState {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool inside = false;
    bool left_down = false;
    bool right_down = false;
    bool middle_down = false;
    bool alt = false;  // Alt held at the last mouse message
};

// Keys the viewport itself uses (fly camera), as bits of NativeChildWindow::keys().
enum ViewportKey : std::uint32_t {
    kKeyForward = 1u << 0,   // W
    kKeyLeft = 1u << 1,      // A
    kKeyBack = 1u << 2,      // S
    kKeyRight = 1u << 3,     // D
    kKeyDown = 1u << 4,      // Q
    kKeyUp = 1u << 5,        // E
    kKeyFast = 1u << 6,      // Shift
};

class NativeChildWindow final : public PresentationTarget {
public:
    // parent: the native parent handle (HWND on Windows).
    [[nodiscard]] static Result<std::unique_ptr<NativeChildWindow>> create(void* parent);
    ~NativeChildWindow() override;

    NativeChildWindow(const NativeChildWindow&) = delete;
    NativeChildWindow& operator=(const NativeChildWindow&) = delete;

    [[nodiscard]] void* native_handle() const noexcept { return hwnd_; }
    [[nodiscard]] PointerState pointer_state() const noexcept;
    // Wheel movement since the last call, in notches (positive = away from the user).
    [[nodiscard]] float take_wheel() noexcept;
    // ViewportKey bits currently held while the viewport has keyboard focus.
    [[nodiscard]] std::uint32_t keys() const noexcept;

    [[nodiscard]] PixelSize pixel_size() const override;
    [[nodiscard]] Result<std::vector<const char*>> vulkan_instance_extensions() const override;
    [[nodiscard]] Result<VkSurfaceKHR> create_vulkan_surface(VkInstance instance) const override;
    void destroy_vulkan_surface(VkInstance instance, VkSurfaceKHR surface) const override;

    // Window procedure plumbing (internal).
    struct Input {
        std::atomic<std::int32_t> x{0};
        std::atomic<std::int32_t> y{0};
        std::atomic<bool> inside{false};
        std::atomic<bool> left_down{false};
        std::atomic<bool> right_down{false};
        std::atomic<bool> middle_down{false};
        std::atomic<std::int32_t> wheel{0};  // raw WHEEL_DELTA units
        std::atomic<bool> alt{false};
        std::atomic<std::uint32_t> keys{0};
    };

private:
    explicit NativeChildWindow(void* hwnd);
    void* hwnd_ = nullptr;
    std::unique_ptr<Input> input_;
};

} // namespace noor::platform
