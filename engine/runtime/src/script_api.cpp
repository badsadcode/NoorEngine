#include "noor/runtime/script_api.h"

#include <algorithm>
#include <cmath>

namespace noor::runtime::api {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

// World rotation: the entity's rotation composed with its parents'.
math::Quat world_rotation(World& w, const scene::Entity& e) {
    math::Quat q = e.transform.rotation;
    std::optional<Guid> p = e.parent;
    for (int depth = 0; p && depth < 256; ++depth) {
        const scene::Entity* pe = w.find(*p);
        if (!pe) break;
        q = pe->transform.rotation * q;
        p = pe->parent;
    }
    return math::normalize_quat(q);
}

math::Mat4 world_matrix(World& w, const scene::Entity& e) {
    math::Mat4 m = math::compose(e.transform.position, e.transform.rotation, e.transform.scale);
    std::optional<Guid> p = e.parent;
    for (int depth = 0; p && depth < 256; ++depth) {
        const scene::Entity* pe = w.find(*p);
        if (!pe) break;
        m = math::compose(pe->transform.position, pe->transform.rotation, pe->transform.scale) * m;
        p = pe->parent;
    }
    return m;
}

} // namespace

bool exists(World& w, const Guid& e) { return w.find(e) != nullptr; }

std::optional<std::string> name(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return x->name;
    return std::nullopt;
}

std::optional<Guid> find(World& w, std::string_view n) {
    for (const auto& e : w.scene().entities)
        if (e.name == n) return e.id;
    return std::nullopt;
}

std::optional<Guid> parent(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return x->parent;
    return std::nullopt;
}

std::optional<Guid> child(World& w, const Guid& e, std::string_view n) {
    for (const auto& x : w.scene().entities)
        if (x.parent == e && x.name == n) return x.id;
    return std::nullopt;
}

void destroy(World& w, const Guid& e) {
    if (w.find(e)) w.destroy(e);
}

std::optional<math::Vec3> position(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return x->transform.position;
    return std::nullopt;
}

void set_position(World& w, const Guid& e, math::Vec3 p) {
    if (auto* x = w.find(e)) x->transform.position = p;
}

void translate(World& w, const Guid& e, math::Vec3 d, bool in_own_space) {
    if (auto* x = w.find(e)) x->transform.position = x->transform.position + (in_own_space ? math::rotate(x->transform.rotation, d) : d);
}

std::optional<math::Vec3> euler(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return euler_from_quat(x->transform.rotation);
    return std::nullopt;
}

void set_euler(World& w, const Guid& e, math::Vec3 degrees) {
    if (auto* x = w.find(e)) x->transform.rotation = quat_from_euler(degrees);
}

void rotate(World& w, const Guid& e, math::Vec3 axis, float degrees) {
    auto* x = w.find(e);
    if (!x || math::length(axis) < 1e-9f) return;
    const auto q = math::Quat::from_axis_angle(axis, static_cast<float>(degrees * kDegToRad));
    x->transform.rotation = math::normalize_quat(q * x->transform.rotation);
}

std::optional<math::Vec3> scale(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return x->transform.scale;
    return std::nullopt;
}

void set_scale(World& w, const Guid& e, math::Vec3 s) {
    if (auto* x = w.find(e)) x->transform.scale = s;
}

std::optional<math::Vec3> world_position(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return world_matrix(w, *x).transform_point({0, 0, 0});
    return std::nullopt;
}

std::optional<math::Mat4> world_matrix(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return world_matrix(w, *x);
    return std::nullopt;
}

std::optional<math::Quat> world_rotation(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return world_rotation(w, *x);
    return std::nullopt;
}

void set_world_pose(World& w, const Guid& e, math::Vec3 position, math::Quat rotation) {
    auto* x = w.find(e);
    if (!x) return;
    if (!x->parent || !w.find(*x->parent)) {
        x->transform.position = position;
        x->transform.rotation = math::normalize_quat(rotation);
        return;
    }
    const scene::Entity& p = *w.find(*x->parent);
    const math::Mat4 parent_inv = math::inverse(world_matrix(w, p));
    x->transform.position = parent_inv.transform_point(position);
    x->transform.rotation = math::normalize_quat(math::conjugate(world_rotation(w, p)) * rotation);
}

std::optional<math::Vec3> forward(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return math::rotate(world_rotation(w, *x), {0, 0, -1});
    return std::nullopt;
}

std::optional<math::Vec3> right(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return math::rotate(world_rotation(w, *x), {1, 0, 0});
    return std::nullopt;
}

std::optional<math::Vec3> up(World& w, const Guid& e) {
    if (auto* x = w.find(e)) return math::rotate(world_rotation(w, *x), {0, 1, 0});
    return std::nullopt;
}

const asset::Json* component(World& w, const Guid& e, std::string_view type) {
    auto* x = w.find(e);
    if (!x) return nullptr;
    const auto it = x->components.find(std::string(type));
    return it == x->components.end() ? nullptr : &*it;
}

void set_component(World& w, const Guid& e, std::string_view type, asset::Json value) {
    auto* x = w.find(e);
    if (!x) return;
    if (value.is_null()) x->components.erase(std::string(type));
    else x->components[std::string(type)] = std::move(value);
}

bool input_down(World& w, std::string_view a) { return w.input().down(a); }
bool input_pressed(World& w, std::string_view a) { return w.input().pressed(a); }
bool input_released(World& w, std::string_view a) { return w.input().released(a); }
float input_axis(World& w, std::string_view a) { return w.input().axis(a); }
double time(World& w) { return w.time(); }
void capture_mouse(World& w, bool capture) { w.request_mouse_capture(capture); }
bool mouse_captured(World& w) { return w.input().state().mouse_captured; }

math::Quat quat_from_euler(math::Vec3 d) {
    const auto yaw = math::Quat::from_axis_angle({0, 1, 0}, static_cast<float>(d.y * kDegToRad));
    const auto pitch = math::Quat::from_axis_angle({1, 0, 0}, static_cast<float>(d.x * kDegToRad));
    const auto roll = math::Quat::from_axis_angle({0, 0, 1}, static_cast<float>(d.z * kDegToRad));
    return math::normalize_quat(yaw * pitch * roll);
}

math::Vec3 euler_from_quat(math::Quat q) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    const double sin_pitch = std::clamp(2 * (w * x - y * z), -1.0, 1.0);
    const double pitch = std::asin(sin_pitch);
    const double yaw = std::atan2(2 * (w * y + x * z), 1 - 2 * (x * x + y * y));
    const double roll = std::atan2(2 * (w * z + x * y), 1 - 2 * (x * x + z * z));
    return {static_cast<float>(pitch * kRadToDeg), static_cast<float>(yaw * kRadToDeg), static_cast<float>(roll * kRadToDeg)};
}

std::optional<UiAnchor> parse_anchor(std::string_view name) {
    static constexpr std::pair<std::string_view, UiAnchor> kNames[] = {
        {"top_left", UiAnchor::TopLeft},       {"top", UiAnchor::Top},       {"top_right", UiAnchor::TopRight},
        {"left", UiAnchor::Left},              {"center", UiAnchor::Center}, {"right", UiAnchor::Right},
        {"bottom_left", UiAnchor::BottomLeft}, {"bottom", UiAnchor::Bottom}, {"bottom_right", UiAnchor::BottomRight},
    };
    for (const auto& [n, a] : kNames)
        if (n == name) return a;
    return std::nullopt;
}

std::optional<math::Vec4> parse_color(std::string_view hex) {
    if ((hex.size() != 7 && hex.size() != 9) || hex[0] != '#') return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    float v[4] = {1, 1, 1, 1};
    for (std::size_t i = 0; i * 2 + 1 < hex.size(); ++i) {
        const int hi = nibble(hex[1 + i * 2]), lo = nibble(hex[2 + i * 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        v[i] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    auto linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    return math::Vec4{linear(v[0]), linear(v[1]), linear(v[2]), v[3]};  // alpha stays linear
}

void ui_text(World& w, std::string text, math::Vec2 position, float size, math::Vec4 color, UiAnchor anchor) {
    UiCommand c;
    c.kind = UiCommand::Kind::Text;
    c.anchor = anchor;
    c.position = position;
    c.text_size = std::clamp(size, 1.0f, 720.0f);
    c.color = color;
    c.text = std::move(text);
    w.ui(std::move(c));
}

void ui_rect(World& w, math::Vec2 position, math::Vec2 size, math::Vec4 color, UiAnchor anchor) {
    UiCommand c;
    c.kind = UiCommand::Kind::Rect;
    c.anchor = anchor;
    c.position = position;
    c.size = {std::max(0.0f, size.x), std::max(0.0f, size.y)};
    c.color = color;
    w.ui(std::move(c));
}

} // namespace noor::runtime::api
