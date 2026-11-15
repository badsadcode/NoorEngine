#pragma once
// OS services: startup/shutdown and the event pump.
// Implemented on SDL3 (ADR-021); SDL types never appear in these headers.

#include "noor/foundation/error.h"
#include "noor/platform/input.h"

#include <cstdint>

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
};

// Processes pending OS events and updates the input state.
[[nodiscard]] EventSummary pump_events();

// Keyboard/mouse state as of the last pump_events().
[[nodiscard]] const InputState& input() noexcept;

// Relative mouse mode hides and captures the cursor and reports raw movement
// (for mouse-look). Turn it off to give the cursor back.
void set_relative_mouse(Window& window, bool enabled);

// Blocks until an event arrives or the timeout passes (used while minimized).
void wait_for_events(std::uint32_t timeout_ms);

// Monotonic high-resolution time in seconds since initialize().
[[nodiscard]] double time_seconds();

} // namespace noor::platform
