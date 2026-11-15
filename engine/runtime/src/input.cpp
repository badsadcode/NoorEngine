#include "noor/runtime/input.h"

#include "noor/foundation/file_io.h"

#include <array>
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

Result<std::vector<Key>> keys_of(const asset::Json& list, std::string_view where) {
    std::vector<Key> keys;
    if (!list.is_array()) return fail(ErrorCode::ParseError, std::format("{}: expected a list of key names", where));
    for (const auto& k : list) {
        const Key key = k.is_string() ? key_from_name(k.get<std::string>()) : Key::None;
        if (key == Key::None) return fail(ErrorCode::ParseError, std::format("{}: unknown key {}", where, k.dump()));
        keys.push_back(key);
    }
    return keys;
}

} // namespace

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
    InputMap m;
    m.axes["move_x"] = {{Key::A, Key::Left}, {Key::D, Key::Right}};
    m.axes["move_y"] = {{Key::S, Key::Down}, {Key::W, Key::Up}};
    m.actions["jump"] = {Key::Space};
    m.actions["sprint"] = {Key::Shift};
    m.actions["crouch"] = {Key::Ctrl, Key::C};
    m.actions["fire"] = {Key::MouseLeft};
    m.actions["aim"] = {Key::MouseRight};
    m.actions["interact"] = {Key::E};
    m.actions["pause"] = {Key::Escape};
    return m;
}

Result<InputMap> InputMap::from_json(const asset::Json& json) {
    InputMap m = defaults();
    if (!json.is_object()) return fail(ErrorCode::ParseError, "input settings: expected an object");
    if (const auto a = json.find("actions"); a != json.end()) {
        if (!a->is_object()) return fail(ErrorCode::ParseError, "input settings: \"actions\" must be an object");
        for (const auto& [name, keys] : a->items()) {
            auto k = keys_of(keys, std::format("action \"{}\"", name));
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
                auto k = keys_of(axis["negative"], std::format("axis \"{}\" negative", name));
                if (!k) return std::unexpected(k.error());
                out.negative = std::move(*k);
            }
            if (axis.contains("positive")) {
                auto k = keys_of(axis["positive"], std::format("axis \"{}\" positive", name));
                if (!k) return std::unexpected(k.error());
                out.positive = std::move(*k);
            }
            m.axes[name] = std::move(out);
        }
    }
    return m;
}

Result<InputMap> InputMap::load(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return defaults();
    auto text = fs::read_file(file);
    if (!text) return std::unexpected(text.error());
    auto json = asset::parse_json(*text, file.generic_string());
    if (!json) return std::unexpected(json.error());
    return from_json(*json);
}

bool Input::any(const InputState& s, std::string_view action) const {
    const auto it = map_.actions.find(std::string(action));
    if (it == map_.actions.end()) return false;
    for (const Key k : it->second) {
        if (s.down(k)) return true;
    }
    return false;
}

bool Input::down(std::string_view action) const { return any(current_, action); }
bool Input::pressed(std::string_view action) const { return any(current_, action) && !any(previous_, action); }
bool Input::released(std::string_view action) const { return !any(current_, action) && any(previous_, action); }

float Input::axis(std::string_view name) const {
    if (name == "look_x") return current_.mouse_delta.x;
    if (name == "look_y") return current_.mouse_delta.y;
    const auto it = map_.axes.find(std::string(name));
    if (it == map_.axes.end()) return 0.0f;
    float v = 0.0f;
    for (const Key k : it->second.positive) {
        if (current_.down(k)) {
            v += 1.0f;
            break;
        }
    }
    for (const Key k : it->second.negative) {
        if (current_.down(k)) {
            v -= 1.0f;
            break;
        }
    }
    return v;
}

} // namespace noor::runtime
