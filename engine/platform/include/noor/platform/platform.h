#pragma once
// OS services: startup/shutdown and the event pump.
// Implemented on SDL3 (ADR-021); SDL types never appear in these headers.

#include "noor/foundation/error.h"
#include "noor/platform/input.h"

#include <cstdint>
#include <functional>

namespace noor::platform {

class Window;

// Must be called once before creating windows. Safe to call shutdown() after a failed init.
[[nodiscard]] Status initialize();
void shutdown();

// What happened since the last pump. Single-window for now; multi-window
// routing arrives with the editor.
struct EventSummary {
    bool quit_requested = false;
    bool window_resized = false;
    bool window_minimized = false;
    bool window_restored = false;
    bool focus_lost = false;    // keys and buttons were released; relative mouse mode is off
};

// Processes pending OS events and updates the input state.
[[nodiscard]] EventSummary pump_events();

// Keyboard/mouse state as of the last pump_events().
[[nodiscard]] const InputState& input() noexcept;

// Relative mouse mode hides and captures the cursor and reports raw movement
// (for mouse-look). Turn it off to give the cursor back.
void set_relative_mouse(Window& window, bool enabled);

// Called when the OS wants the window redrawn while the normal loop is blocked, e.g. while
// the user drags a window edge on Windows (the OS runs its own modal loop then). The callback
// should render one frame with the current state; it must not call pump_events().
// Pass an empty function to remove it.
void set_live_redraw_callback(std::function<void()> callback);

// Blocks until an event arrives or the timeout passes (used while minimized).
void wait_for_events(std::uint32_t timeout_ms);

// Monotonic high-resolution time in seconds since initialize().
[[nodiscard]] double time_seconds();

} // namespace noor::platform
