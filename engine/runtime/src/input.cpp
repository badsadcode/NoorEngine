#include "noor/runtime/input.h"

#include "noor/foundation/file_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <format>

namespace noor::runtime {
namespace {

constexpr std::array<std::string_view, kKeyCount> kNames = {
    "None",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "Space", "Enter", "Escape", "Tab", "Backspace", "Shift", "Ctrl", "Alt",
    "Up", "Down", "Left", "Right",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "MouseLeft", "MouseRight", "MouseMiddle",
};

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

struct PadName {
    const char* name;
    Binding binding;
};
// The canonical name first (binding_name gives it); the letters of an Xbox pad as aliases.
const std::vector<PadName>& pad_names() {
    static const std::vector<PadName> names = {
        {"Pad.South", Binding::pad(PadButton::South)},       {"Pad.East", Binding::pad(PadButton::East)},
        {"Pad.West", Binding::pad(PadButton::West)},         {"Pad.North", Binding::pad(PadButton::North)},
        {"Pad.Back", Binding::pad(PadButton::Back)},         {"Pad.Guide", Binding::pad(PadButton::Guide)},
        {"Pad.Start", Binding::pad(PadButton::Start)},       {"Pad.LS", Binding::pad(PadButton::LeftStick)},
        {"Pad.RS", Binding::pad(PadButton::RightStick)},     {"Pad.LB", Binding::pad(PadButton::LeftShoulder)},
        {"Pad.RB", Binding::pad(PadButton::RightShoulder)},  {"Pad.Up", Binding::pad(PadButton::DpadUp)},
        {"Pad.Down", Binding::pad(PadButton::DpadDown)},     {"Pad.Left", Binding::pad(PadButton::DpadLeft)},
        {"Pad.Right", Binding::pad(PadButton::DpadRight)},
        {"Pad.LeftX+", Binding::pad(PadAxis::LeftX, false)}, {"Pad.LeftX-", Binding::pad(PadAxis::LeftX, true)},
        {"Pad.LeftY+", Binding::pad(PadAxis::LeftY, false)}, {"Pad.LeftY-", Binding::pad(PadAxis::LeftY, true)},
        {"Pad.RightX+", Binding::pad(PadAxis::RightX, false)}, {"Pad.RightX-", Binding::pad(PadAxis::RightX, true)},
        {"Pad.RightY+", Binding::pad(PadAxis::RightY, false)}, {"Pad.RightY-", Binding::pad(PadAxis::RightY, true)},
        {"Pad.LT", Binding::pad(PadAxis::LeftTrigger, false)}, {"Pad.RT", Binding::pad(PadAxis::RightTrigger, false)},
        // Aliases.
        {"Pad.A", Binding::pad(PadButton::South)}, {"Pad.B", Binding::pad(PadButton::East)},
        {"Pad.X", Binding::pad(PadButton::West)},  {"Pad.Y", Binding::pad(PadButton::North)},
    };
    return names;
}

Result<std::vector<Binding>> bindings_of(const asset::Json& list, std::string_view where) {
    std::vector<Binding> out;
    if (!list.is_array()) return fail(ErrorCode::ParseError, std::format("{}: expected a list of key or pad names", where));
    for (const auto& k : list) {
        const auto b = k.is_string() ? binding_from_name(k.get<std::string>()) : std::nullopt;
        if (!b) return fail(ErrorCode::ParseError, std::format("{}: unknown key or pad control {}", where, k.dump()));
        out.push_back(*b);
    }
    return out;
}

asset::Json names_of(const std::vector<Binding>& list) {
    asset::Json out = asset::Json::array();
    for (const auto& b : list) out.push_back(binding_name(b));
    return out;
}

bool is_stick(PadAxis a) { return a == PadAxis::LeftX || a == PadAxis::LeftY || a == PadAxis::RightX || a == PadAxis::RightY; }
PadAxis partner(PadAxis a) {
    switch (a) {
    case PadAxis::LeftX: return PadAxis::LeftY;
    case PadAxis::LeftY: return PadAxis::LeftX;
    case PadAxis::RightX: return PadAxis::RightY;
    case PadAxis::RightY: return PadAxis::RightX;
    default: return a;
    }
}

} // namespace

bool operator==(const Binding& a, const Binding& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case Binding::Kind::Key: return a.key == b.key;
    case Binding::Kind::Button: return a.button == b.button;
    case Binding::Kind::Axis: return a.axis == b.axis && a.negative == b.negative;
    }
    return false;
}

std::string binding_name(const Binding& b) {
    if (b.kind == Binding::Kind::Key) return std::string(key_name(b.key));
    for (const auto& n : pad_names())
        if (n.binding == b) return n.name;
    return "None";
}

std::optional<Binding> binding_from_name(std::string_view name) {
    if (const Key k = key_from_name(name); k != Key::None) return Binding(k);
    for (const auto& n : pad_names())
        if (iequals(name, n.name)) return n.binding;
    return std::nullopt;
}

std::string_view key_name(Key key) {
    const auto i = static_cast<std::size_t>(key);
    return i < kNames.size() ? kNames[i] : "None";
}

Key key_from_name(std::string_view name) {
    for (std::size_t i = 1; i < kNames.size(); ++i) {
        if (iequals(name, kNames[i])) return static_cast<Key>(i);
    }
    return Key::None;
}

InputMap InputMap::defaults() {
    using B = PadButton;
    using A = PadAxis;
    InputMap m;
    m.axes["move_x"] = {{Key::A, Key::Left, Binding::pad(A::LeftX, true), Binding::pad(B::DpadLeft)},
                        {Key::D, Key::Right, Binding::pad(A::LeftX, false), Binding::pad(B::DpadRight)}};
    m.axes["move_y"] = {{Key::S, Key::Down, Binding::pad(A::LeftY, false), Binding::pad(B::DpadDown)},
                        {Key::W, Key::Up, Binding::pad(A::LeftY, true), Binding::pad(B::DpadUp)}};  // stick up is -y
    m.axes["look_stick_x"] = {{Binding::pad(A::RightX, true)}, {Binding::pad(A::RightX, false)}};
    m.axes["look_stick_y"] = {{Binding::pad(A::RightY, true)}, {Binding::pad(A::RightY, false)}};
    m.actions["jump"] = {Key::Space, Binding::pad(B::South)};
    m.actions["sprint"] = {Key::Shift, Binding::pad(B::LeftStick)};
    m.actions["crouch"] = {Key::Ctrl, Key::C, Binding::pad(B::East)};
    m.actions["fire"] = {Key::MouseLeft, Binding::pad(A::RightTrigger, false)};
    m.actions["aim"] = {Key::MouseRight, Binding::pad(A::LeftTrigger, false)};
    m.actions["interact"] = {Key::E, Binding::pad(B::West)};
    m.actions["pause"] = {Key::Escape, Binding::pad(B::Start)};
    return m;
}

Result<InputMap> InputMap::from_json(const asset::Json& json, const InputMap& base) {
    InputMap m = base;
    if (!json.is_object()) return fail(ErrorCode::ParseError, "input settings: expected an object");
    if (const auto a = json.find("actions"); a != json.end()) {
        if (!a->is_object()) return fail(ErrorCode::ParseError, "input settings: \"actions\" must be an object");
        for (const auto& [name, keys] : a->items()) {
            auto k = bindings_of(keys, std::format("action \"{}\"", name));
            if (!k) return std::unexpected(k.error());
            m.actions[name] = std::move(*k);
        }
    }
    if (const auto x = json.find("axes"); x != json.end()) {
        if (!x->is_object()) return fail(ErrorCode::ParseError, "input settings: \"axes\" must be an object");
        for (const auto& [name, axis] : x->items()) {
            if (!axis.is_object()) return fail(ErrorCode::ParseError, std::format("axis \"{}\": expected an object", name));
            Axis out;
            if (axis.contains("negative")) {
                auto k = bindings_of(axis["negative"], std::format("axis \"{}\" negative", name));
                if (!k) return std::unexpected(k.error());
                out.negative = std::move(*k);
            }
            if (axis.contains("positive")) {
                auto k = bindings_of(axis["positive"], std::format("axis \"{}\" positive", name));
                if (!k) return std::unexpected(k.error());
                out.positive = std::move(*k);
            }
            m.axes[name] = std::move(out);
        }
    }
    if (json.contains("dead_zone")) {
        if (!json["dead_zone"].is_number() || json["dead_zone"].get<double>() < 0 || json["dead_zone"].get<double>() >= 1)
            return fail(ErrorCode::ParseError, "input settings: \"dead_zone\" is a number from 0 to under 1");
        m.dead_zone = json["dead_zone"].get<float>();
    }
    if (json.contains("press")) {
        if (!json["press"].is_number() || json["press"].get<double>() <= 0 || json["press"].get<double>() > 1)
            return fail(ErrorCode::ParseError, "input settings: \"press\" is a number above 0, up to 1");
        m.press = json["press"].get<float>();
    }
    return m;
}

Result<InputMap> InputMap::load(const std::filesystem::path& file, const std::filesystem::path& overrides) {
    InputMap m = defaults();
    std::error_code ec;
    for (const auto& f : {file, overrides}) {
        if (f.empty() || !std::filesystem::exists(f, ec)) continue;
        auto text = fs::read_file(f);
        if (!text) return std::unexpected(text.error());
        auto json = asset::parse_json(*text, f.generic_string());
        if (!json) return std::unexpected(json.error());
        auto read = from_json(*json, m);
        if (!read) return std::unexpected(read.error());
        m = std::move(*read);
    }
    return m;
}

asset::Json InputMap::to_json() const {
    asset::Json out{{"actions", asset::Json::object()}, {"axes", asset::Json::object()}, {"dead_zone", dead_zone}, {"press", press}};
    for (const auto& [name, list] : actions) out["actions"][name] = names_of(list);
    for (const auto& [name, axis] : axes) out["axes"][name] = {{"negative", names_of(axis.negative)}, {"positive", names_of(axis.positive)}};
    return out;
}

Status InputMap::save_overrides(const InputMap& base, const std::filesystem::path& file) const {
    asset::Json out = asset::Json::object();
    for (const auto& [name, list] : actions) {
        const auto it = base.actions.find(name);
        if (it == base.actions.end() || it->second != list) out["actions"][name] = names_of(list);
    }
    for (const auto& [name, axis] : axes) {
        const auto it = base.axes.find(name);
        if (it == base.axes.end() || it->second.negative != axis.negative || it->second.positive != axis.positive)
            out["axes"][name] = {{"negative", names_of(axis.negative)}, {"positive", names_of(axis.positive)}};
    }
    if (dead_zone != base.dead_zone) out["dead_zone"] = dead_zone;
    if (press != base.press) out["press"] = press;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return fs::write_file_atomic(file, out.dump(2) + "\n");
}

std::vector<Binding>* InputMap::bindings(std::string_view name) {
    return const_cast<std::vector<Binding>*>(static_cast<const InputMap*>(this)->bindings(name));
}

const std::vector<Binding>* InputMap::bindings(std::string_view name) const {
    if (const auto it = actions.find(std::string(name)); it != actions.end()) return &it->second;
    if (name.size() > 1 && (name.back() == '+' || name.back() == '-')) {
        if (const auto it = axes.find(std::string(name.substr(0, name.size() - 1))); it != axes.end())
            return name.back() == '+' ? &it->second.positive : &it->second.negative;
    }
    return nullptr;
}

float Input::value(const InputState& s, const Binding& b, int player) const {
    if (player < 1 || player > static_cast<int>(kMaxPlayers)) return 0.0f;
    if (b.kind == Binding::Kind::Key) return player == 1 && s.down(b.key) ? 1.0f : 0.0f;
    const PadState& pad = s.pads[static_cast<std::size_t>(player - 1)];
    if (!pad.connected) return 0.0f;
    if (b.kind == Binding::Kind::Button) return pad.down(b.button) ? 1.0f : 0.0f;
    float v = pad.axis(b.axis);
    if (is_stick(b.axis)) {
        // A round dead zone (the stick's distance from its centre, not each axis alone), the rest rescaled to 0..1.
        const float x = v, y = pad.axis(partner(b.axis));
        const float length = std::sqrt(x * x + y * y);
        if (length <= map_.dead_zone) return 0.0f;
        v = x / length * std::min(1.0f, (std::min(length, 1.0f) - map_.dead_zone) / (1.0f - map_.dead_zone));
    } else if (v < 0.05f) {
        v = 0.0f;  // triggers rest a little above 0 on some pads
    }
    return std::clamp(b.negative ? -v : v, 0.0f, 1.0f);
}

bool Input::any(const InputState& s, std::string_view action, int player) const {
    const auto it = map_.actions.find(std::string(action));
    if (it == map_.actions.end()) return false;
    for (const Binding& b : it->second)
        if (value(s, b, player) >= map_.press) return true;
    return false;
}

bool Input::down(std::string_view action, int player) const { return any(current_, action, player); }
bool Input::pressed(std::string_view action, int player) const { return any(current_, action, player) && !any(previous_, action, player); }
bool Input::released(std::string_view action, int player) const { return !any(current_, action, player) && any(previous_, action, player); }

float Input::axis(std::string_view name, int player) const {
    if (name == "look_x") return player == 1 ? current_.mouse_delta.x : 0.0f;
    if (name == "look_y") return player == 1 ? current_.mouse_delta.y : 0.0f;
    const auto it = map_.axes.find(std::string(name));
    if (it == map_.axes.end()) return 0.0f;
    float plus = 0.0f, minus = 0.0f;
    for (const Binding& b : it->second.positive) plus = std::max(plus, value(current_, b, player));
    for (const Binding& b : it->second.negative) minus = std::max(minus, value(current_, b, player));
    return std::clamp(plus - minus, -1.0f, 1.0f);
}

std::vector<int> Input::pads_connected() const {
    std::vector<int> out;
    for (std::size_t i = 0; i < kMaxPlayers; ++i)
        if (current_.pads[i].connected && !previous_.pads[i].connected) out.push_back(static_cast<int>(i) + 1);
    return out;
}

std::vector<int> Input::pads_disconnected() const {
    std::vector<int> out;
    for (std::size_t i = 0; i < kMaxPlayers; ++i)
        if (!current_.pads[i].connected && previous_.pads[i].connected) out.push_back(static_cast<int>(i) + 1);
    return out;
}

std::optional<Binding> Input::last_pressed(int player) const {
    if (player == 1)
        for (std::size_t k = 1; k < kKeyCount; ++k)
            if (current_.keys.test(k) && !previous_.keys.test(k)) return Binding(static_cast<Key>(k));
    if (player < 1 || player > static_cast<int>(kMaxPlayers)) return std::nullopt;
    for (std::size_t b = 0; b < kPadButtonCount; ++b) {
        const Binding x = Binding::pad(static_cast<PadButton>(b));
        if (value(current_, x, player) >= map_.press && value(previous_, x, player) < map_.press) return x;
    }
    for (std::size_t a = 0; a < kPadAxisCount; ++a)
        for (const bool negative : {false, true}) {
            const Binding x = Binding::pad(static_cast<PadAxis>(a), negative);
            if (value(current_, x, player) >= map_.press && value(previous_, x, player) < map_.press) return x;
        }
    return std::nullopt;
}

} // namespace noor::runtime
