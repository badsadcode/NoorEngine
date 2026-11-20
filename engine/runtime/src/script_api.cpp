#include "noor/runtime/script_api.h"

#include "noor/asset/tileset_asset.h"

#include <algorithm>
#include <unordered_set>
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

std::optional<Guid> bone(World& w, const Guid& e, std::string_view n) {
    std::unordered_set<Guid> below{e};  // entities come parents first: one pass finds every descendant
    for (const auto& x : w.scene().entities) {
        if (!x.parent || !below.contains(*x.parent)) continue;
        if (x.name == n) return x.id;
        below.insert(x.id);
    }
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

// ---- Tilemaps (2D part 4) ----
std::optional<std::int32_t> tile(World& w, const Guid& e, int x, int y) {
    auto* x_entity = w.find(e);
    if (!x_entity || !x_entity->has("tilemap") || !x_entity->components["tilemap"].is_object()) return std::nullopt;
    const auto& c = x_entity->components["tilemap"];
    return c.contains("rows") ? asset::tile_at(c["rows"], x, y) : -1;
}

bool set_tile(World& w, const Guid& e, int x, int y, std::int32_t t) {
    auto* x_entity = w.find(e);
    if (!x_entity || !x_entity->has("tilemap") || !x_entity->components["tilemap"].is_object()) return false;
    auto& c = x_entity->components["tilemap"];
    if (!c.contains("rows")) return false;
    return asset::set_tile(c["rows"], x, y, t);
}

// ---- Sprites ----
namespace {
// Seconds into its animation (the same reckoning as game::sprite_time, which draws it).
double animation_seconds(const asset::Json& c, double now) {
    if (c.contains("paused_at") && c["paused_at"].is_number()) return c["paused_at"].get<double>();
    const double speed = c.contains("speed") && c["speed"].is_number() ? c["speed"].get<double>() : 1.0;
    if (c.contains("started") && c["started"].is_number()) return std::max(0.0, (now - c["started"].get<double>()) * speed);
    const double offset = c.contains("time_offset") && c["time_offset"].is_number() ? c["time_offset"].get<double>() : 0.0;
    return now * speed + offset;
}
} // namespace

// The component an entity's animations live in: a model's animator, or a sprite.
const char* animated(World& w, const Guid& e) {
    if (const auto* a = component(w, e, "animator"); a && a->is_object()) return "animator";
    return "sprite";
}

bool controlled(World& w, const Guid& e) {
    const auto* a = component(w, e, "animator");
    return a && a->is_object() && a->contains("controller");
}

bool play_animation(World& w, const Guid& e, std::string_view animation, float speed) {
    if (controlled(w, e)) return w.command("animator.play", e, asset::Json{{"state", std::string(animation)}, {"fade", 0.0}});
    const char* type = animated(w, e);
    const auto* c = component(w, e, type);
    if (!c || !c->is_object()) return false;
    asset::Json s = *c;
    s["animation"] = std::string(animation);
    s["started"] = w.time();
    s["speed"] = speed;
    s.erase("paused_at");
    s.erase("from");
    s.erase("fade");
    set_component(w, e, type, std::move(s));
    return true;
}

bool cross_fade(World& w, const Guid& e, std::string_view animation, float seconds, float speed) {
    if (controlled(w, e))
        return w.command("animator.play", e, asset::Json{{"state", std::string(animation)}, {"fade", std::max(0.0f, seconds)}});
    const auto* c = component(w, e, "animator");
    if (!c || !c->is_object() || seconds <= 0.0f || !c->contains("animation")) return play_animation(w, e, animation, speed);
    asset::Json s = *c;
    asset::Json from = asset::Json::object();  // the clip it's leaving, with its own clock
    for (const char* key : {"animation", "started", "speed", "loop", "paused_at", "time_offset"})
        if (s.contains(key)) from[key] = s[key];
    s["from"] = std::move(from);
    s["fade"] = seconds;
    s["animation"] = std::string(animation);
    s["started"] = w.time();
    s["speed"] = speed;
    s.erase("paused_at");
    s.erase("time_offset");
    set_component(w, e, "animator", std::move(s));
    return true;
}

bool set_animation_parameter(World& w, const Guid& e, std::string_view name, const asset::Json& value) {
    const auto* c = component(w, e, "animator");
    if (!c || !c->is_object()) return false;
    asset::Json s = *c;
    s["parameters"][std::string(name)] = value;
    set_component(w, e, "animator", std::move(s));
    return true;
}

void stop_animation(World& w, const Guid& e) {
    if (controlled(w, e)) return set_animation_speed(w, e, 0.0f);
    const char* type = animated(w, e);
    const auto* c = component(w, e, type);
    if (!c || !c->is_object() || c->contains("paused_at")) return;
    asset::Json s = *c;
    s["paused_at"] = animation_seconds(s, w.time());
    set_component(w, e, type, std::move(s));
}

void set_animation_speed(World& w, const Guid& e, float speed) {
    if (controlled(w, e)) {  // a controller's time just runs slower or faster from the next step
        asset::Json s = *component(w, e, "animator");
        s["speed"] = std::max(0.0f, speed);
        set_component(w, e, "animator", std::move(s));
        return;
    }
    const char* type = animated(w, e);
    const auto* c = component(w, e, type);
    if (!c || !c->is_object()) return;
    asset::Json s = *c;
    const double now = w.time();
    const double at = animation_seconds(s, now);
    if (speed <= 0.0f) {
        s["paused_at"] = at;
    } else {
        s.erase("paused_at");
        s["started"] = now - at / static_cast<double>(speed);  // the same moment of the animation, from now at the new speed
        s.erase("time_offset");
    }
    s["speed"] = std::max(0.0f, speed);
    set_component(w, e, type, std::move(s));
}

std::optional<std::string> current_animation(World& w, const Guid& e) {
    if (controlled(w, e)) {
        const auto* c = component(w, e, "animator");
        if (c->contains("state") && (*c)["state"].is_string()) return (*c)["state"].get<std::string>();
        return std::nullopt;
    }
    const auto* c = component(w, e, animated(w, e));
    if (!c || !c->is_object() || !c->contains("animation") || !(*c)["animation"].is_string()) return std::nullopt;
    return (*c)["animation"].get<std::string>();
}

bool emitter_burst(World& w, const Guid& e, int count) {
    if (!exists(w, e) || count <= 0) return false;
    return w.command("emitter.burst", e, asset::Json{{"count", count}});
}

bool input_down(World& w, std::string_view a) { return w.input().down(a); }
bool input_pressed(World& w, std::string_view a) { return w.input().pressed(a); }
bool input_released(World& w, std::string_view a) { return w.input().released(a); }
float input_axis(World& w, std::string_view a) { return w.input().axis(a); }
double time(World& w) { return w.time(); }
void capture_mouse(World& w, bool capture) { w.request_mouse_capture(capture); }
bool mouse_captured(World& w) { return w.input().state().mouse_captured; }

std::optional<float> water_height(World& w, float x, float z) {
    if (const auto s = w.water_at(x, z)) return s->height;
    return std::nullopt;
}
std::optional<math::Vec3> water_normal(World& w, float x, float z) {
    if (const auto s = w.water_at(x, z)) return s->normal;
    return std::nullopt;
}

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
