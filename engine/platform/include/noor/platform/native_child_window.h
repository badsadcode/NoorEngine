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
#include <utility>

namespace noor::platform {

// Latest pointer state inside the child window, in physical pixels.
struct PointerState {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool inside = false;
    bool left_down = false;
    bool right_down = false;
    bool middle_down = false;
    bool alt = false;   // Alt held at the last mouse message
    bool ctrl = false;  // Ctrl held at the last mouse message
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
    // Any key held while the viewport has keyboard focus, by platform key code (Win32 virtual key).
    [[nodiscard]] bool key_down(int platform_key) const noexcept;
    // True if this window has the keyboard focus (UI thread only).
    [[nodiscard]] bool has_focus() const noexcept;
    // Automated tests: while on, real mouse messages are ignored and the pointer is whatever the
    // test sets, and the window counts as focused (focus changes don't drop held keys). (Posted mouse messages are not enough: Windows also delivers the real cursor
    // position, e.g. after SetCapture, so a test would depend on where the user left the mouse.)
    void set_test_pointer(bool enabled, const PointerState& state = {}) noexcept;

    // Mouse capture for first-person look (UI thread): hides the cursor, keeps it inside the window
    // and reports raw movement. Released by Escape, by losing focus, or by calling with false.
    // While a test pointer is active only the flag changes (tests never take the user's mouse).
    void set_mouse_capture(bool capture) noexcept;
    [[nodiscard]] bool mouse_captured() const noexcept;
    // Raw mouse movement since the last call, while captured (counts, roughly pixels).
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> take_raw_motion() noexcept;

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
        std::atomic<bool> ctrl{false};
        std::atomic<std::uint32_t> keys{0};
        std::atomic<std::uint64_t> vk[4]{};  // 256 virtual-key bits
        std::atomic<bool> test_pointer{false};
        std::atomic<bool> captured{false};
        // Keyboard focus, from WM_SETFOCUS / WM_KILLFOCUS: readable from any thread (GetFocus() only
        // answers for the calling thread's windows, so the render thread always got "no").
        std::atomic<bool> focused{false};
        std::atomic<bool> capture_is_real{false};  // cursor hidden/clipped (not a test)
        std::atomic<std::int32_t> raw_dx{0};
        std::atomic<std::int32_t> raw_dy{0};
    };

private:
    explicit NativeChildWindow(void* hwnd);
    void* hwnd_ = nullptr;
    std::unique_ptr<Input> input_;
};

} // namespace noor::platform
