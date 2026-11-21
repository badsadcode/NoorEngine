#pragma once
// Host-neutral input (ADR-027). Hosts (the editor viewport, the player window) fill an
// InputState each frame; projects map keys and gamepad controls to actions and axes; scripts ask for actions.
//
// Gamepads (gameplay foundations part 6e): up to four, each a player's. Player 1 is the keyboard and mouse together with
// the first pad; players 2 to 4 are the other pads. A binding is a key, a pad button, or one direction of a pad's stick
// or trigger, written as a name: "Space", "Pad.South", "Pad.LeftX+". Sticks have a dead zone (input.json "dead_zone",
// default 0.2), and axes read them as smooth values from -1 to 1.
//
// Rebinding: the project's Settings/input.json is the game's own map; what players change at run time goes to their
// own settings file (InputMap::save_overrides), read over it at the next start.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"

#include <array>
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

// A gamepad's buttons, by where they are (South is Xbox's A, PlayStation's cross, Nintendo's B).
enum class PadButton : std::uint8_t {
    South, East, West, North, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
    DpadUp, DpadDown, DpadLeft, DpadRight, Count
};
inline constexpr std::size_t kPadButtonCount = static_cast<std::size_t>(PadButton::Count);
// Sticks from -1 to 1 (+x right, +y down, as the pad reports them); triggers from 0 to 1.
enum class PadAxis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };
inline constexpr std::size_t kPadAxisCount = static_cast<std::size_t>(PadAxis::Count);
inline constexpr std::size_t kMaxPlayers = 4;

struct PadState {
    bool connected = false;
    std::string name;  // "Xbox Series X Controller"...
    std::bitset<kPadButtonCount> buttons;
    std::array<float, kPadAxisCount> axes{};
    [[nodiscard]] bool down(PadButton b) const { return buttons.test(static_cast<std::size_t>(b)); }
    [[nodiscard]] float axis(PadAxis a) const { return axes[static_cast<std::size_t>(a)]; }
};

// One frame of raw input from the host.
struct InputState {
    std::bitset<kKeyCount> keys;  // includes mouse buttons
    math::Vec2 mouse{0, 0};        // pixels in the game view, origin top-left
    math::Vec2 mouse_delta{0, 0};  // pixels moved since the previous frame
    float wheel = 0;               // notches since the previous frame
    bool mouse_captured = false;   // the host has captured the mouse (hidden; mouse_delta is raw movement)
    std::array<PadState, kMaxPlayers> pads;  // by player: pads[0] is player 1's (part 6e)

    [[nodiscard]] bool down(Key k) const { return k != Key::None && keys.test(static_cast<std::size_t>(k)); }
    void set(Key k, bool on = true) {
        if (k != Key::None) keys.set(static_cast<std::size_t>(k), on);
    }
};

// Something an action or an axis direction is bound to.
struct Binding {
    enum class Kind : std::uint8_t { Key, Button, Axis } kind = Kind::Key;
    Key key = Key::None;
    PadButton button = PadButton::South;
    PadAxis axis = PadAxis::LeftX;
    bool negative = false;  // Axis: the stick's minus side ("Pad.LeftX-")

    Binding() = default;
    Binding(Key k) : kind(Kind::Key), key(k) {}  // NOLINT: a key is a binding
    static Binding pad(PadButton b) {
        Binding x;
        x.kind = Kind::Button;
        x.button = b;
        return x;
    }
    static Binding pad(PadAxis a, bool negative) {
        Binding x;
        x.kind = Kind::Axis;
        x.axis = a;
        x.negative = negative;
        return x;
    }
    [[nodiscard]] bool is_pad() const { return kind != Kind::Key; }
    friend bool operator==(const Binding&, const Binding&);
};
// "Space", "MouseLeft", "Pad.South" (or Pad.A, Pad.B, Pad.X, Pad.Y by an Xbox pad's letters), "Pad.Start", "Pad.Back",
// "Pad.LB", "Pad.RB", "Pad.LS", "Pad.RS", "Pad.Up" .. "Pad.Right" (the d-pad), "Pad.LeftX+", "Pad.LeftY-", "Pad.RightX+",
// "Pad.LT", "Pad.RT" (the triggers).
[[nodiscard]] std::string binding_name(const Binding& b);
[[nodiscard]] std::optional<Binding> binding_from_name(std::string_view name);  // case-insensitive

// Named actions ("jump": Space, Pad.South) and axes ("move_x": A/Left/stick left negative, D/Right/stick right positive).
struct InputMap {
    std::unordered_map<std::string, std::vector<Binding>> actions;
    struct Axis {
        std::vector<Binding> negative;
        std::vector<Binding> positive;
    };
    std::unordered_map<std::string, Axis> axes;
    float dead_zone = 0.2f;  // of a stick's travel, round its centre
    float press = 0.5f;      // a stick or trigger counts as a press past this

    // move_x, move_y (W/S: +/-), look_x/look_y are mouse-driven; jump, sprint, crouch, fire, aim, interact, pause; each
    // with the usual pad controls too (the left stick and d-pad move, South jumps, Start pauses...).
    [[nodiscard]] static InputMap defaults();
    // {"actions": {"jump": ["Space", "Pad.South"]}, "axes": {"move_x": {"negative": ["A"], "positive": ["D"]}},
    //  "dead_zone": 0.2}. Entries replace those with the same name in `base` (the defaults); unknown names are errors.
    [[nodiscard]] static Result<InputMap> from_json(const asset::Json& json, const InputMap& base = defaults());
    // Project settings file (Settings/input.json); defaults if the file doesn't exist. With `overrides` (a player's own
    // file, InputMap::save_overrides), read over it.
    [[nodiscard]] static Result<InputMap> load(const std::filesystem::path& file,
                                               const std::filesystem::path& overrides = {});
    [[nodiscard]] asset::Json to_json() const;
    // A player's changes: what differs from `base`, written to `file` (atomically).
    [[nodiscard]] Status save_overrides(const InputMap& base, const std::filesystem::path& file) const;

    // The bindings of an action, or of an axis's side ("move_x+", "move_x-"); nullptr when there's no such name.
    [[nodiscard]] std::vector<Binding>* bindings(std::string_view name);
    [[nodiscard]] const std::vector<Binding>* bindings(std::string_view name) const;
};

// The per-frame view scripts and systems use. `player` is 1 to 4 (1: keyboard, mouse and the first pad).
class Input {
public:
    explicit Input(InputMap map = InputMap::defaults()) : map_(std::move(map)), base_(map_) {}

    void begin_frame(const InputState& now) {
        previous_ = current_;
        current_ = now;
    }

    [[nodiscard]] bool down(std::string_view action, int player = 1) const;      // held now
    [[nodiscard]] bool pressed(std::string_view action, int player = 1) const;   // went down this frame
    [[nodiscard]] bool released(std::string_view action, int player = 1) const;  // went up this frame
    // -1..1: keys give whole steps, sticks smooth values past their dead zone; look_x/look_y are the mouse's movement.
    [[nodiscard]] float axis(std::string_view name, int player = 1) const;
    [[nodiscard]] const InputState& state() const noexcept { return current_; }
    [[nodiscard]] const InputMap& map() const noexcept { return map_; }
    [[nodiscard]] InputMap& map() noexcept { return map_; }
    // The game's own map (before players' changes), for resetting and saving what changed.
    [[nodiscard]] const InputMap& base() const noexcept { return base_; }
    void set_base(InputMap base) { base_ = std::move(base); }

    // A pad connected or gone since the last frame (players 1..4), for "pad_connected" / "pad_disconnected" messages.
    [[nodiscard]] std::vector<int> pads_connected() const;
    [[nodiscard]] std::vector<int> pads_disconnected() const;
    // What was pressed this frame (a key, a mouse button, a pad button, a stick or trigger pushed past `press`), as a
    // binding: a settings menu's "press a key for Jump". Player 1's pad for player 1.
    [[nodiscard]] std::optional<Binding> last_pressed(int player = 1) const;

private:
    [[nodiscard]] float value(const InputState& s, const Binding& b, int player) const;  // 0..1
    [[nodiscard]] bool any(const InputState& s, std::string_view action, int player) const;
    InputMap map_, base_;
    InputState current_, previous_;
};

} // namespace noor::runtime
