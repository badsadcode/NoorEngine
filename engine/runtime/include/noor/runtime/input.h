#pragma once
// Host-neutral input (ADR-027). Hosts (the editor viewport, the player window) fill an
// InputState each frame; projects map keys to actions and axes; scripts ask for actions.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"

#include <bitset>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace noor::runtime {

// Keys and mouse buttons. Names (key_name/key_from_name) are what input.json uses.
enum class Key : std::uint8_t {
    None = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace, Shift, Ctrl, Alt,
    Up, Down, Left, Right,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    MouseLeft, MouseRight, MouseMiddle,
    Count
};
inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);

[[nodiscard]] std::string_view key_name(Key key);
[[nodiscard]] Key key_from_name(std::string_view name);  // case-insensitive; Key::None if unknown

// One frame of raw input from the host.
struct InputState {
    std::bitset<kKeyCount> keys;  // includes mouse buttons
    math::Vec2 mouse{0, 0};        // pixels in the game view, origin top-left
    math::Vec2 mouse_delta{0, 0};  // pixels moved since the previous frame
    float wheel = 0;               // notches since the previous frame
    bool mouse_captured = false;   // the host has captured the mouse (hidden; mouse_delta is raw movement)

    [[nodiscard]] bool down(Key k) const { return k != Key::None && keys.test(static_cast<std::size_t>(k)); }
    void set(Key k, bool on = true) {
        if (k != Key::None) keys.set(static_cast<std::size_t>(k), on);
    }
};

// Named actions ("jump": Space) and axes ("move_x": A/Left negative, D/Right positive).
struct InputMap {
    std::unordered_map<std::string, std::vector<Key>> actions;
    struct Axis {
        std::vector<Key> negative;
        std::vector<Key> positive;
    };
    std::unordered_map<std::string, Axis> axes;

    // move_x, move_y (W/S: +/-), look_x/look_y are mouse-driven; jump, sprint, crouch, fire, aim,
    // interact, pause.
    [[nodiscard]] static InputMap defaults();
    // {"actions": {"jump": ["Space"]}, "axes": {"move_x": {"negative": ["A"], "positive": ["D"]}}}
    // Entries override the defaults with the same name; unknown key names are errors.
    [[nodiscard]] static Result<InputMap> from_json(const asset::Json& json);
    // Project settings file (Settings/input.json); defaults if the file doesn't exist.
    [[nodiscard]] static Result<InputMap> load(const std::filesystem::path& file);
};

// The per-frame view scripts and systems use.
class Input {
public:
    explicit Input(InputMap map = InputMap::defaults()) : map_(std::move(map)) {}

    void begin_frame(const InputState& now) {
        previous_ = current_;
        current_ = now;
    }

    [[nodiscard]] bool down(std::string_view action) const;      // held now
    [[nodiscard]] bool pressed(std::string_view action) const;   // went down this frame
    [[nodiscard]] bool released(std::string_view action) const;  // went up this frame
    [[nodiscard]] float axis(std::string_view name) const;       // -1..1 (keys) or mouse delta for look_x/look_y
    [[nodiscard]] const InputState& state() const noexcept { return current_; }
    [[nodiscard]] const InputMap& map() const noexcept { return map_; }

private:
    [[nodiscard]] bool any(const InputState& s, std::string_view action) const;
    InputMap map_;
    InputState current_, previous_;
};

} // namespace noor::runtime
