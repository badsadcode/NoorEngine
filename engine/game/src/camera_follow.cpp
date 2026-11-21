#include "noor/game/camera_follow.h"

#include "noor/runtime/script_api.h"

#include <algorithm>
#include <cmath>

namespace noor::game {

using asset::Json;
namespace api = runtime::api;

math::Vec2 follow_step(math::Vec2 c, math::Vec2 t, const FollowSettings& s, double dt) noexcept {
    // Where it wants to be: just enough to bring the target back inside the dead zone.
    math::Vec2 want = c;
    const float hx = s.dead_zone.x * 0.5f, hy = s.dead_zone.y * 0.5f;
    if (t.x > c.x + hx) want.x = t.x - hx;
    else if (t.x < c.x - hx) want.x = t.x + hx;
    if (t.y > c.y + hy) want.y = t.y - hy;
    else if (t.y < c.y - hy) want.y = t.y + hy;
    // Smoothly (an exponential approach, the same whatever the frame rate) or at once.
    math::Vec2 out = want;
    if (s.smoothing > 0.0f) {
        const float k = 1.0f - static_cast<float>(std::exp(-dt / static_cast<double>(s.smoothing)));
        out = {c.x + (want.x - c.x) * k, c.y + (want.y - c.y) * k};
    }
    // The view's edges inside the bounds (a level narrower than the view: centred on it).
    if (s.bounded) {
        const auto clamp_axis = [](float v, float lo, float hi, float half) {
            return hi - lo <= 2.0f * half ? (lo + hi) * 0.5f : std::clamp(v, lo + half, hi - half);
        };
        out.x = clamp_axis(out.x, s.bounds.x, s.bounds.z, s.half_view.x);
        out.y = clamp_axis(out.y, s.bounds.y, s.bounds.w, s.half_view.y);
    }
    return out;
}

void CameraFollowSystem::late_update(runtime::World& world, double dt) {
    for (const auto& e : world.scene().entities) {
        const auto it = e.components.find("camera_follow");
        if (it == e.components.end() || !it->is_object()) continue;
        const Json& c = *it;
        // The target: by id, else by name.
        std::optional<Guid> target;
        if (c.contains("target") && c["target"].is_string()) {
            const std::string t = c["target"].get<std::string>();
            if (auto id = Guid::parse(t); id && world.find(*id)) target = *id;
            else target = api::find(world, t);
        }
        if (!target) continue;
        // Where the target is drawn (physics' smoothed pose if it has one), plus the offset.
        math::Vec3 tp;
        if (const auto pose = world.render_poses().find(*target); pose != world.render_poses().end()) tp = pose->second.transform_point({0, 0, 0});
        else if (const auto p = api::world_position(world, *target)) tp = *p;
        else continue;
        math::Vec2 offset{0, 0};
        if (const auto o = c.find("offset"); o != c.end() && o->is_array() && o->size() == 2 && (*o)[0].is_number() && (*o)[1].is_number())
            offset = {(*o)[0].get<float>(), (*o)[1].get<float>()};
        FollowSettings s;
        if (const auto z = c.find("dead_zone"); z != c.end() && z->is_array() && z->size() == 2 && (*z)[0].is_number() && (*z)[1].is_number())
            s.dead_zone = {std::max(0.0f, (*z)[0].get<float>()), std::max(0.0f, (*z)[1].get<float>())};
        if (c.contains("smoothing") && c["smoothing"].is_number()) s.smoothing = std::max(0.0f, c["smoothing"].get<float>());
        if (const auto b = c.find("bounds"); b != c.end() && b->is_array() && b->size() == 4) {
            bool ok = true;
            for (const auto& v : *b) ok = ok && v.is_number();
            if (ok) {
                s.bounded = true;
                s.bounds = {(*b)[0].get<float>(), (*b)[1].get<float>(), (*b)[2].get<float>(), (*b)[3].get<float>()};
                const Json* cam = e.components.contains("camera") ? &e.components.at("camera") : nullptr;
                const float height = cam && cam->is_object() && cam->contains("height") && (*cam)["height"].is_number() ? (*cam)["height"].get<float>() : 10.0f;
                s.half_view = {height * 0.5f * aspect_, height * 0.5f};
            }
        }
        const auto here = api::world_position(world, e.id);
        const auto rot = api::world_rotation(world, e.id);
        if (!here || !rot) continue;
        const math::Vec2 next = follow_step({here->x, here->y}, {tp.x + offset.x, tp.y + offset.y}, s, dt);
        api::set_world_pose(world, e.id, {next.x, next.y, here->z}, *rot);
    }
}

} // namespace noor::game
