#include "noor/scene/behaviours.h"

#include <cmath>

namespace noor::scene {

std::size_t update_behaviours(Scene& scene, double seconds) {
    std::size_t updated = 0;
    for (auto& e : scene.entities) {
        if (!e.has("rotator")) continue;
        const auto& r = e.components["rotator"];
        math::Vec3 axis{0, 1, 0};
        double rate = 45.0;
        if (r.is_object()) {
            if (const auto a = r.find("axis"); a != r.end() && a->is_array() && a->size() == 3 && (*a)[0].is_number() &&
                                                (*a)[1].is_number() && (*a)[2].is_number()) {
                axis = {(*a)[0].get<float>(), (*a)[1].get<float>(), (*a)[2].get<float>()};
            }
            if (const auto d = r.find("degrees_per_second"); d != r.end() && d->is_number()) rate = d->get<double>();
        }
        if (math::length(axis) < 1e-6f || rate == 0.0) continue;
        const float angle = static_cast<float>(rate * seconds * 3.14159265358979323846 / 180.0);
        e.transform.rotation = math::normalize_quat(math::Quat::from_axis_angle(axis, angle) * e.transform.rotation);
        ++updated;
    }
    return updated;
}

} // namespace noor::scene
