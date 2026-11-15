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

NativeChildWindow::Input* input_of(HWND hwnd) {
    return reinterpret_cast<NativeChildWindow::Input*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK viewport_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    NativeChildWindow::Input* input = input_of(hwnd);
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
        if (input) input->right_down.store(true);
        return 0;
    case WM_RBUTTONUP:
        if (input) input->right_down.store(false);
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
            input_->right_down.load()};
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
