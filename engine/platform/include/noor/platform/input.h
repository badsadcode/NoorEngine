#pragma once
// Keyboard and mouse state, updated by platform::pump_events().
//
// Keys are physical positions (scancodes), so WASD stays WASD on AZERTY or QWERTZ keyboards.
// "pressed"/"released" are true only during the frame in which the change happened.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace noor::platform {

enum class Key : std::uint8_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    Up, Down, Left, Right,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Delete, Home, End, PageUp, PageDown,  // text editing (game UI, gameplay foundations part 6b)
    Count
};

enum class MouseButton : std::uint8_t { Left = 0, Right, Middle, Count };

struct InputState {
    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
    static constexpr std::size_t kButtonCount = static_cast<std::size_t>(MouseButton::Count);

    std::array<bool, kKeyCount> keys_down{};
    std::array<bool, kKeyCount> keys_pressed{};
    std::array<bool, kKeyCount> keys_released{};
    std::array<bool, kButtonCount> buttons_down{};
    std::array<bool, kButtonCount> buttons_pressed{};
    std::array<bool, kButtonCount> buttons_released{};

    float mouse_x = 0.0f;   // position in the focused window, in window coordinates
    float mouse_y = 0.0f;
    float mouse_dx = 0.0f;  // movement since last pump (also valid in relative mode)
    float mouse_dy = 0.0f;
    float wheel = 0.0f;     // vertical wheel steps since last pump, positive = away from the user
    // Every key press since last pump in order, the keyboard's auto-repeat included (a held Backspace in a text box);
    // `pressed` has only the first. And the text typed (UTF-8), while text input is on (platform::set_text_input).
    std::vector<Key> key_presses;
    std::string text;

    [[nodiscard]] bool down(Key k) const noexcept { return keys_down[static_cast<std::size_t>(k)]; }
    [[nodiscard]] bool pressed(Key k) const noexcept { return keys_pressed[static_cast<std::size_t>(k)]; }
    [[nodiscard]] bool released(Key k) const noexcept { return keys_released[static_cast<std::size_t>(k)]; }
    [[nodiscard]] bool down(MouseButton b) const noexcept { return buttons_down[static_cast<std::size_t>(b)]; }
    [[nodiscard]] bool pressed(MouseButton b) const noexcept { return buttons_pressed[static_cast<std::size_t>(b)]; }
    [[nodiscard]] bool released(MouseButton b) const noexcept { return buttons_released[static_cast<std::size_t>(b)]; }

    // Clears per-frame data (pressed/released, deltas, wheel); keeps what is held down.
    void begin_frame() noexcept {
        keys_pressed.fill(false);
        keys_released.fill(false);
        buttons_pressed.fill(false);
        buttons_released.fill(false);
        mouse_dx = mouse_dy = wheel = 0.0f;
        key_presses.clear();
        text.clear();
    }
    // Releases everything, e.g. when the window loses focus, so no key stays "stuck".
    void release_all() noexcept {
        for (std::size_t i = 0; i < kKeyCount; ++i) {
            keys_released[i] = keys_down[i];
            keys_down[i] = false;
        }
        for (std::size_t i = 0; i < kButtonCount; ++i) {
            buttons_released[i] = buttons_down[i];
            buttons_down[i] = false;
        }
    }
};

} // namespace noor::platform
