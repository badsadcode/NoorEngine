#include "noor/game/sprite_animation.h"

#include "noor/foundation/log.h"

#include <algorithm>
#include <cmath>

namespace noor::game {
namespace {

using asset::Json;
using math::Vec3;

double number(const Json& j, const char* key, double fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<double>() : fallback;
}

} // namespace

int sprite_direction(const math::Mat4& world, Vec3 eye, int count) {
    if (count <= 1) return 0;
    const Vec3 pos = world.transform_point({0, 0, 0});
    Vec3 z{eye.x - pos.x, 0.0f, eye.z - pos.z};  // towards the viewer, level
    if (math::length(z) < 1e-6f) return 0;
    z = math::normalize(z);
    const Vec3 x = math::cross(Vec3{0, 1, 0}, z);                           // the viewer's right (as the billboard's +X)
    const Vec3 forward{-world.m[2].x, -world.m[2].y, -world.m[2].z};      // the entity's -Z
    const float fz = math::dot(forward, z), fx = math::dot(forward, x);
    if (std::abs(fz) + std::abs(fx) < 1e-6f) return 0;                     // looking straight up or down
    // 0 when it faces the viewer, growing as it turns to the viewer's left (fx < 0).
    const double angle = std::atan2(-static_cast<double>(fx), static_cast<double>(fz));
    const double step = 2.0 * 3.14159265358979323846 / count;
    const long long k = std::llround(angle / step);
    return static_cast<int>(((k % count) + count) % count);
}

double sprite_time(const Json& c, double seconds) {
    if (!c.is_object()) return seconds;
    if (c.contains("paused_at") && c["paused_at"].is_number()) return c["paused_at"].get<double>();
    const double speed = number(c, "speed", 1.0);
    if (c.contains("started") && c["started"].is_number()) return std::max(0.0, (seconds - c["started"].get<double>()) * speed);
    return seconds * speed + number(c, "time_offset", 0.0);
}

int sprite_frame(const asset::SpriteSheet& sheet, const Json& c, const math::Mat4& world, Vec3 eye, double seconds) {
    if (sheet.frames.empty()) return 0;
    const int direction = sprite_direction(world, eye, sheet.directions);
    int frame = static_cast<int>(number(c, "frame", 0.0));
    if (c.is_object() && c.contains("animation") && c["animation"].is_string()) {
        frame = sheet.frame_at(c["animation"].get<std::string>(), sprite_time(c, seconds), direction);
    } else if (sheet.directions > 1) {
        frame += direction;  // a still frame of a directional sheet: its direction-0 frame, turned
    }
    return std::clamp(frame, 0, static_cast<int>(sheet.frames.size()) - 1);
}

const asset::SpriteSheet* SpriteAnimationSystem::sheet(const Guid& id) {
    auto it = sheets_.find(id);
    if (it == sheets_.end()) {
        std::optional<asset::SpriteSheet> loaded;
        if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
            if (auto s = asset::load_sprite_file(*file)) loaded = std::move(*s);
            else NOOR_LOG_WARN("Sprites", "{}", s.error().message);
        }
        it = sheets_.emplace(id, std::move(loaded)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void SpriteAnimationSystem::update(runtime::World& world, double) {
    const double now = world.time();
    for (const auto& e : world.scene().entities) {
        if (!e.has("sprite")) continue;
        const Json& c = e.components.at("sprite");
        if (!c.is_object() || !c.contains("started") || !c["started"].is_number() || c.contains("paused_at") || !c.contains("animation") ||
            !c["animation"].is_string())
            continue;
        const double started = c["started"].get<double>();
        if (const auto r = reported_.find(e.id); r != reported_.end() && r->second == started) continue;  // already told
        const auto id = asset::guid_field(c, "sheet");
        const asset::SpriteSheet* s = id ? sheet(*id) : nullptr;
        const std::string name = c["animation"].get<std::string>();
        const asset::SpriteAnimation* a = s ? s->animation(name) : nullptr;
        if (!a || a->loop || a->duration() <= 0.0) continue;
        if (sprite_time(c, now) + 1e-9 < a->duration()) continue;
        reported_[e.id] = started;
        ++ended_;
        world.post({runtime::Event::Kind::AnimationEnd, e.id, e.id, name});
    }
}

void SpriteAnimationSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) { reported_.erase(entity.id); }

} // namespace noor::game
