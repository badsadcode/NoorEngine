#include "noor/runtime/tween.h"

#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace noor::runtime {

using asset::Json;

namespace {

constexpr double kPi = std::numbers::pi;

double bounce_out(double t) {
    constexpr double n = 7.5625, d = 2.75;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) return t -= 1.5 / d, n * t * t + 0.75;
    if (t < 2.5 / d) return t -= 2.25 / d, n * t * t + 0.9375;
    return t -= 2.625 / d, n * t * t + 0.984375;
}

// "a.b.c" in a component's JSON.
Json* at_path(Json& root, std::string_view path) {
    Json* node = &root;
    while (!path.empty()) {
        const auto dot = path.find('.');
        const std::string key(path.substr(0, dot));
        if (!node->is_object() || !node->contains(key)) return nullptr;
        node = &(*node)[key];
        path = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
    }
    return node;
}

bool is_hex(const Json& j) { return j.is_string() && api::parse_color(j.get<std::string>()).has_value(); }

// A "#RRGGBB[AA]" colour as 0..1 sRGB channels (blended as they're written, the way colour pickers show them).
std::array<double, 4> hex_channels(const std::string& s) {
    std::array<double, 4> c{1, 1, 1, 1};
    for (std::size_t i = 0; i * 2 + 2 < s.size() + 1 && i < 4; ++i) c[i] = std::stoi(s.substr(1 + i * 2, 2), nullptr, 16) / 255.0;
    return c;
}

std::string hex_of(const std::array<double, 4>& c, bool alpha) {
    const auto b = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return alpha ? std::format("#{:02X}{:02X}{:02X}{:02X}", b(c[0]), b(c[1]), b(c[2]), b(c[3]))
                 : std::format("#{:02X}{:02X}{:02X}", b(c[0]), b(c[1]), b(c[2]));
}

// Blends two values of the same shape: numbers, arrays of numbers, colours.
std::optional<Json> blend(const Json& a, const Json& b, double k) {
    if (a.is_number() && b.is_number()) return a.get<double>() + (b.get<double>() - a.get<double>()) * k;
    if (a.is_array() && b.is_array() && a.size() == b.size()) {
        Json out = Json::array();
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto v = blend(a[i], b[i], k);
            if (!v) return std::nullopt;
            out.push_back(*v);
        }
        return out;
    }
    if (is_hex(a) && is_hex(b)) {
        const auto ca = hex_channels(a.get<std::string>()), cb = hex_channels(b.get<std::string>());
        std::array<double, 4> c{};
        for (int i = 0; i < 4; ++i) c[i] = ca[i] + (cb[i] - ca[i]) * k;
        return hex_of(c, a.get<std::string>().size() == 9 || b.get<std::string>().size() == 9);
    }
    return std::nullopt;
}

Json vec(math::Vec3 v) { return Json::array({v.x, v.y, v.z}); }
std::optional<math::Vec3> vec_of(const Json& j) {
    if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) return std::nullopt;
    return math::Vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

bool transform_property(std::string_view p) {
    return p == "position" || p == "euler" || p == "rotation" || p == "scale" || p == "world_position";
}

// The property's value now, or nothing if the entity hasn't got it.
std::optional<Json> read(World& w, const Guid& e, std::string_view property) {
    if (property == "position") return api::position(w, e).transform(vec);
    if (property == "euler" || property == "rotation") return api::euler(w, e).transform(vec);
    if (property == "scale") return api::scale(w, e).transform(vec);
    if (property == "world_position") return api::world_position(w, e).transform(vec);
    const auto dot = property.find('.');
    if (dot == std::string_view::npos) return std::nullopt;
    const Json* comp = api::component(w, e, property.substr(0, dot));
    if (!comp) return std::nullopt;
    Json copy = *comp;
    const Json* v = at_path(copy, property.substr(dot + 1));
    return v ? std::optional(*v) : std::nullopt;
}

void write(World& w, const Guid& e, std::string_view property, const Json& from, const Json& to, double k) {
    if (property == "rotation") {
        const auto a = vec_of(from), b = vec_of(to);
        if (!a || !b) return;
        const math::Quat q = math::slerp(api::quat_from_euler(*a), api::quat_from_euler(*b), static_cast<float>(k));
        api::set_euler(w, e, api::euler_from_quat(q));
        return;
    }
    const auto v = blend(from, to, k);
    if (!v) return;
    if (transform_property(property)) {
        const auto p = vec_of(*v);
        if (!p) return;
        if (property == "position") api::set_position(w, e, *p);
        else if (property == "euler") api::set_euler(w, e, *p);
        else if (property == "scale") api::set_scale(w, e, *p);
        else api::set_world_pose(w, e, *p, api::world_rotation(w, e).value_or(math::Quat{}));
        return;
    }
    const auto dot = property.find('.');
    const Json* comp = api::component(w, e, property.substr(0, dot));
    if (!comp) return;
    Json copy = *comp;
    if (Json* target = at_path(copy, property.substr(dot + 1))) {
        *target = *v;
        api::set_component(w, e, property.substr(0, dot), std::move(copy));
    }
}

} // namespace

std::optional<double> ease(std::string_view name, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const auto in_out = [t](auto in) { return t < 0.5 ? in(2 * t) / 2 : 1 - in(2 - 2 * t) / 2; };
    const auto quad = [](double x) { return x * x; };
    const auto cubic = [](double x) { return x * x * x; };
    const auto sine = [](double x) { return 1 - std::cos(x * kPi / 2); };
    const auto back = [](double x) { return 2.70158 * x * x * x - 1.70158 * x * x; };
    const auto elastic = [](double x) {
        return x <= 0 ? 0.0 : x >= 1 ? 1.0 : -std::pow(2.0, 10 * x - 10) * std::sin((x * 10 - 10.75) * (2 * kPi / 3));
    };
    const auto bounce = [](double x) { return 1 - bounce_out(1 - x); };
    const auto out = [t](auto in) { return 1 - in(1 - t); };
    if (name == "linear") return t;
    if (name == "ease_in" || name == "quad_in") return quad(t);
    if (name == "ease_out" || name == "quad_out") return out(quad);
    if (name == "ease_in_out" || name == "quad_in_out") return in_out(quad);
    if (name == "cubic_in") return cubic(t);
    if (name == "cubic_out") return out(cubic);
    if (name == "cubic_in_out") return in_out(cubic);
    if (name == "sine_in") return sine(t);
    if (name == "sine_out") return out(sine);
    if (name == "sine_in_out") return in_out(sine);
    if (name == "back_in") return back(t);
    if (name == "back_out") return out(back);
    if (name == "back_in_out") return in_out(back);
    if (name == "elastic_in") return elastic(t);
    if (name == "elastic_out") return out(elastic);
    if (name == "elastic_in_out") return in_out(elastic);
    if (name == "bounce_in") return bounce(t);
    if (name == "bounce_out") return bounce_out(t);
    if (name == "bounce_in_out") return in_out(bounce);
    return std::nullopt;
}

std::variant<std::uint64_t, std::string> Tweens::start(World& w, const Guid& entity, std::string property, Json to, double seconds,
                                                      std::string easing, TweenOptions options) {
    if (!w.find(entity)) return std::string("the entity doesn't exist");
    if (!ease(easing, 0.0)) return std::format("unknown easing '{}' (linear, ease_out, cubic_in_out, back_out, bounce_out...)", easing);
    const auto now = read(w, entity, property);
    if (!now)
        return std::format("'{}' isn't something this entity has (position, euler, rotation, scale, world_position, or "
                           "\"component.field\" holding a number, numbers or a colour)", property);
    if (!blend(*now, to, 0.0) && !(property == "rotation" && vec_of(to)))
        return std::format("'{}' is {} now; it can't be tweened to {}", property, now->dump(), to.dump());
    // The same entity and property: the new tween takes over.
    std::erase_if(tweens_, [&](const auto& t) { return t.second.entity == entity && t.second.property == property; });
    Tween t;
    t.entity = entity;
    t.property = std::move(property);
    t.to = std::move(to);
    t.seconds = std::max(0.0, seconds);
    t.delay = std::max(0.0, options.delay);
    t.ease = std::move(easing);
    t.loops_left = options.loops == 0 ? 1 : options.loops;
    t.yoyo = options.yoyo;
    t.real = options.real;
    const std::uint64_t id = next_++;
    tweens_[id] = std::move(t);
    return id;
}

void Tweens::step(World& w, double dt, double real_dt) {
    std::vector<std::uint64_t> done;
    for (auto& [id, t] : tweens_) {
        if (!w.find(t.entity)) {
            done.push_back(id);  // its entity went: it stops, quietly
            continue;
        }
        double left = t.real ? real_dt : dt;
        if (left <= 0.0 && t.started) continue;
        if (t.delay > 0.0) {
            const double used = std::min(t.delay, left);
            t.delay -= used;
            left -= used;
            if (t.delay > 0.0) continue;
        }
        if (!t.started) {  // its starting point is wherever the value is when it starts
            const auto now = read(w, t.entity, t.property);
            if (!now) {
                done.push_back(id);
                continue;
            }
            t.from = *now;
            t.started = true;
        }
        t.elapsed += left;
        // A play (or several, if a frame is longer than a short loop) may finish this frame. A hair of slack, as for
        // timers: thirty steps of 1/60 s add up to 0.49999999999999994, and a 0.5 s tween mustn't take a frame more.
        while (t.elapsed + 1e-9 >= t.seconds) {
            write(w, t.entity, t.property, t.backwards ? t.to : t.from, t.backwards ? t.from : t.to, 1.0);
            if (t.loops_left > 0 && --t.loops_left == 0) break;
            t.elapsed -= t.seconds;
            if (t.yoyo) t.backwards = !t.backwards;
            if (t.seconds <= 0.0) break;
        }
        if (t.loops_left == 0) {
            done.push_back(id);
            continue;
        }
        const double k = *ease(t.ease, t.seconds > 0.0 ? t.elapsed / t.seconds : 1.0);
        write(w, t.entity, t.property, t.backwards ? t.to : t.from, t.backwards ? t.from : t.to, k);
    }
    for (const auto id : done) tweens_.erase(id);
}

} // namespace noor::runtime
