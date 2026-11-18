#include "noor/game/weather.h"

#include <algorithm>
#include <cmath>

namespace noor::game {

namespace {

float number(const asset::Json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

} // namespace

float weather_zone_weight(const asset::Json& zone, const math::Mat4& world, math::Vec3 viewer) {
    if (!zone.is_object()) return 0.0f;
    if (zone.contains("weight") && zone["weight"].is_number()) return std::clamp(zone["weight"].get<float>(), 0.0f, 1.0f);
    const math::Vec3 origin = world.transform_point({0, 0, 0});
    const math::Vec3 rel = viewer - origin;
    float outside = 0.0f;  // metres outside the zone (0 inside)
    if (zone.value("shape", std::string("box")) == "sphere") {
        outside = math::length(rel) - std::max(0.0f, number(zone, "radius", 20.0f));
    } else {
        // The box's own axes (the entity's rotation, without its scale): the size is in metres.
        float half[3] = {20.0f, 10.0f, 20.0f};
        if (zone.contains("size") && zone["size"].is_array() && zone["size"].size() == 3)
            for (int i = 0; i < 3; ++i)
                if (zone["size"][static_cast<std::size_t>(i)].is_number()) half[i] = std::max(0.0f, zone["size"][static_cast<std::size_t>(i)].get<float>()) * 0.5f;
        const math::Vec3 axes[3] = {math::transform_vector(world, {1, 0, 0}), math::transform_vector(world, {0, 1, 0}),
                                    math::transform_vector(world, {0, 0, 1})};
        float sum = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float len = math::length(axes[i]);
            const float along = len > 1e-6f ? math::dot(rel, axes[i] * (1.0f / len)) : 0.0f;
            const float beyond = std::max(0.0f, std::abs(along) - half[i]);
            sum += beyond * beyond;
        }
        outside = std::sqrt(sum);
    }
    if (outside <= 0.0f) return 1.0f;
    const float blend = std::max(1e-3f, number(zone, "blend", 15.0f));
    const float t = std::clamp(outside / blend, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);  // smoothstep: no kink where the blend starts or ends
}

} // namespace noor::game
