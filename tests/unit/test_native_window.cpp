// The editor viewport's native child window (platform). Focus must be readable from any thread:
// play mode reads game input on the viewport's render thread, not the window's thread.
#include "noor/platform/native_child_window.h"

#include <ostream>
#include <doctest/doctest.h>

#include <thread>

#if defined(_WIN32)
#include <windows.h>

TEST_CASE("native child window: keyboard focus is visible from other threads") {
    // A hidden parent, like the editor's window (never shown: the test takes no real focus).
    HWND parent = CreateWindowExW(0, L"STATIC", L"noor focus test", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    REQUIRE(parent != nullptr);
    {
        auto window = noor::platform::NativeChildWindow::create(parent);
        REQUIRE(window.has_value());
        const auto hwnd = static_cast<HWND>((*window)->native_handle());
        auto focused_elsewhere = [&] {
            bool result = true;
            std::thread([&] { result = (*window)->has_focus(); }).join();
            return result;
        };
        CHECK_FALSE(focused_elsewhere());
        SendMessageW(hwnd, WM_SETFOCUS, 0, 0);  // what Windows sends when the viewport gets the keyboard
        CHECK(focused_elsewhere());             // GetFocus() on another thread would say no
        SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
        CHECK_FALSE(focused_elsewhere());
    }
    DestroyWindow(parent);
}
#endif
