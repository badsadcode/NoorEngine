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
    case WM_KILLFOCUS:  // keys released elsewhere would never reach us
        if (input) {
            input->keys.store(0);
            for (auto& word : input->vk) word.store(0);
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

bool NativeChildWindow::has_focus() const noexcept { return GetFocus() == static_cast<HWND>(hwnd_); }

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
