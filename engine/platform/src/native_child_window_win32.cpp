#include "noor/platform/native_child_window.h"

#include "noor/foundation/log.h"

#include <windows.h>
#include <windowsx.h>
// vulkan.h was already included (via presentation_target.h) without the Win32 platform,
// so pull in the Win32 surface declarations explicitly. They need <windows.h> first.
#include <vulkan/vulkan_win32.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vk_enum_string_helper.h>

#include <format>
#include <mutex>

namespace noor::platform {
namespace {

constexpr wchar_t kClassName[] = L"NoorViewportWindow";

std::uint32_t key_bit(WPARAM vk) {
    switch (vk) {
    case 'W': return kKeyForward;
    case 'A': return kKeyLeft;
    case 'S': return kKeyBack;
    case 'D': return kKeyRight;
    case 'Q': return kKeyDown;
    case 'E': return kKeyUp;
    case VK_SHIFT: return kKeyFast;
    default: return 0;
    }
}

NativeChildWindow::Input* input_of(HWND hwnd) {
    return reinterpret_cast<NativeChildWindow::Input*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void apply_capture(HWND hwnd, NativeChildWindow::Input* input, bool capture) {
    if (!input) return;
    const bool real = capture && !input->test_pointer.load();
    if (input->capture_is_real.load() && !real) {  // release the real capture
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {
        }
        RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};  // generic desktop, mouse
        RegisterRawInputDevices(&device, 1, sizeof(device));
        input->capture_is_real.store(false);
    }
    if (real && !input->capture_is_real.load()) {
        RAWINPUTDEVICE device{0x01, 0x02, 0, hwnd};
        RegisterRawInputDevices(&device, 1, sizeof(device));
        RECT r{};
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
        ClipCursor(&r);
        while (ShowCursor(FALSE) >= 0) {
        }
        SetFocus(hwnd);
        input->capture_is_real.store(true);
    }
    input->captured.store(capture);
    input->raw_dx.store(0);
    input->raw_dy.store(0);
}

LRESULT CALLBACK viewport_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    NativeChildWindow::Input* input = input_of(hwnd);
    if (input && input->test_pointer.load() &&
        ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSELEAVE || msg == WM_CAPTURECHANGED)) {
        return msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE ? 0 : DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (input && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
        input->alt.store(GetKeyState(VK_MENU) < 0);
        input->ctrl.store((wparam & MK_CONTROL) != 0);
    }
    switch (msg) {
    case WM_INPUT:
        if (input && input->captured.load()) {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                input->raw_dx.fetch_add(raw.data.mouse.lLastX);
                input->raw_dy.fetch_add(raw.data.mouse.lLastY);
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;  // Vulkan owns every pixel; erasing would flicker
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (input) {
            if (!input->inside.exchange(true)) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
            }
            input->x.store(GET_X_LPARAM(lparam));
            input->y.store(GET_Y_LPARAM(lparam));
        }
        return 0;
    case WM_MOUSELEAVE:
        if (input) input->inside.store(false);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        SetFocus(hwnd);
        if (input) input->left_down.store(true);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (input) input->left_down.store(false);
        return 0;
    case WM_RBUTTONDOWN:
        SetCapture(hwnd);
        SetFocus(hwnd);
        if (input) input->right_down.store(true);
        return 0;
    case WM_RBUTTONUP:
        ReleaseCapture();
        if (input) input->right_down.store(false);
        return 0;
    case WM_MBUTTONDOWN:
        SetCapture(hwnd);
        SetFocus(hwnd);
        if (input) input->middle_down.store(true);
        return 0;
    case WM_MBUTTONUP:
        ReleaseCapture();
        if (input) input->middle_down.store(false);
        return 0;
    case WM_MOUSEWHEEL:
        if (input) input->wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wparam));
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (input && wparam == VK_ESCAPE && (msg == WM_KEYDOWN) && input->captured.load()) {
            apply_capture(hwnd, input, false);  // Escape always gives the mouse back
        }
        if (input) {
            const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            if (wparam < 256) {
                const std::uint64_t bit = 1ull << (wparam % 64);
                if (down) input->vk[wparam / 64].fetch_or(bit);
                else input->vk[wparam / 64].fetch_and(~bit);
            }
            if (const std::uint32_t bit = key_bit(wparam)) {
                if (down) input->keys.fetch_or(bit);
                else input->keys.fetch_and(~bit);
            }
            // While the right button is held the keys fly the camera; they are not shortcuts.
            if (input->right_down.load() && key_bit(wparam)) return 0;
        }
        // Everything else goes to the top-level window so the host's shortcuts (undo, save,
        // frame...) work while the viewport has focus.
        if (HWND root = GetAncestor(hwnd, GA_ROOT); root && root != hwnd) {
            PostMessageW(root, msg, wparam, lparam);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETFOCUS:
        if (input) input->focused.store(true);
        return 0;
    case WM_KILLFOCUS:  // keys released elsewhere would never reach us; the mouse goes back too
        if (input) input->focused.store(false);
        if (input && !input->test_pointer.load()) {
            input->keys.store(0);
            for (auto& word : input->vk) word.store(0);
            if (input->captured.load()) apply_capture(hwnd, input, false);
        }
        return 0;
    case WM_CAPTURECHANGED:  // capture lost (e.g. Alt+Tab while dragging): release everything
        if (input) {
            input->left_down.store(false);
            input->right_down.store(false);
            input->middle_down.store(false);
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

bool register_class_once() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = &viewport_wndproc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW (wide form)
        wc.lpszClassName = kClassName;
        ok = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return ok;
}

} // namespace

NativeChildWindow::NativeChildWindow(void* hwnd) : hwnd_(hwnd), input_(std::make_unique<Input>()) {
    SetWindowLongPtrW(static_cast<HWND>(hwnd_), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(input_.get()));
}

Result<std::unique_ptr<NativeChildWindow>> NativeChildWindow::create(void* parent) {
    if (!parent || !IsWindow(static_cast<HWND>(parent))) {
        return fail(ErrorCode::InvalidArgument, "Parent is not a valid window handle");
    }
    if (!register_class_once()) {
        return fail(ErrorCode::PlatformError, std::format("RegisterClassExW failed (error {})", GetLastError()));
    }
    RECT rect{};
    GetClientRect(static_cast<HWND>(parent), &rect);
    HWND hwnd = CreateWindowExW(0, kClassName, L"NoorViewport", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                0, 0, rect.right - rect.left, rect.bottom - rect.top, static_cast<HWND>(parent),
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        return fail(ErrorCode::PlatformError, std::format("CreateWindowExW failed (error {})", GetLastError()));
    }
    return std::unique_ptr<NativeChildWindow>(new NativeChildWindow(hwnd));
}

NativeChildWindow::~NativeChildWindow() {
    if (hwnd_) {
        apply_capture(static_cast<HWND>(hwnd_), input_.get(), false);
        SetWindowLongPtrW(static_cast<HWND>(hwnd_), GWLP_USERDATA, 0);
        DestroyWindow(static_cast<HWND>(hwnd_));
    }
}

PointerState NativeChildWindow::pointer_state() const noexcept {
    return {input_->x.load(), input_->y.load(), input_->inside.load(), input_->left_down.load(),
            input_->right_down.load(), input_->middle_down.load(), input_->alt.load(), input_->ctrl.load()};
}

std::uint32_t NativeChildWindow::keys() const noexcept { return input_->keys.load(); }

bool NativeChildWindow::key_down(int vk) const noexcept {
    if (vk < 0 || vk >= 256) return false;
    return (input_->vk[vk / 64].load() >> (vk % 64)) & 1u;
}

bool NativeChildWindow::has_focus() const noexcept {
    // Test mode stands in for the user: the window counts as focused (a test process launched in the
    // background may not be allowed to take the foreground, which would make focus flaky).
    return input_->test_pointer.load() || input_->focused.load();
}

void NativeChildWindow::set_mouse_capture(bool capture) noexcept { apply_capture(static_cast<HWND>(hwnd_), input_.get(), capture); }

bool NativeChildWindow::mouse_captured() const noexcept { return input_->captured.load(); }

std::pair<std::int32_t, std::int32_t> NativeChildWindow::take_raw_motion() noexcept {
    return {input_->raw_dx.exchange(0), input_->raw_dy.exchange(0)};
}

void NativeChildWindow::set_test_pointer(bool enabled, const PointerState& s) noexcept {
    input_->test_pointer.store(enabled);
    if (!enabled) return;
    input_->x.store(s.x);
    input_->y.store(s.y);
    input_->inside.store(true);
    input_->left_down.store(s.left_down);
    input_->right_down.store(s.right_down);
    input_->middle_down.store(s.middle_down);
    input_->alt.store(s.alt);
    input_->ctrl.store(s.ctrl);
}

float NativeChildWindow::take_wheel() noexcept {
    return static_cast<float>(input_->wheel.exchange(0)) / static_cast<float>(WHEEL_DELTA);
}

PixelSize NativeChildWindow::pixel_size() const {
    RECT rect{};
    if (!GetClientRect(static_cast<HWND>(hwnd_), &rect) || !IsWindowVisible(static_cast<HWND>(hwnd_))) {
        return {};
    }
    return {static_cast<std::uint32_t>(rect.right - rect.left), static_cast<std::uint32_t>(rect.bottom - rect.top)};
}

Result<std::vector<const char*>> NativeChildWindow::vulkan_instance_extensions() const {
    return std::vector<const char*>{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
}

Result<VkSurfaceKHR> NativeChildWindow::create_vulkan_surface(VkInstance instance) const {
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = static_cast<HWND>(hwnd_);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (const VkResult r = vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface); r != VK_SUCCESS) {
        return fail(ErrorCode::GraphicsError, std::format("vkCreateWin32SurfaceKHR failed ({})", string_VkResult(r)));
    }
    return surface;
}

void NativeChildWindow::destroy_vulkan_surface(VkInstance instance, VkSurfaceKHR surface) const {
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
}

} // namespace noor::platform
