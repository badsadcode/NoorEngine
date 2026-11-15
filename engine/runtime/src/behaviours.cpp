#include "noor/runtime/behaviours.h"

#include <cmath>

namespace noor::runtime {

void BehavioursSystem::update(World& world, double dt) {
    const Input& input = world.input();
    for (auto& e : world.scene().entities) {
        if (const auto r = e.components.find("rotator"); r != e.components.end()) {
            math::Vec3 axis{0, 1, 0};
            double rate = 45.0;
            if (r->is_object()) {
                if (const auto a = r->find("axis"); a != r->end() && a->is_array() && a->size() == 3 && (*a)[0].is_number() &&
                                                    (*a)[1].is_number() && (*a)[2].is_number()) {
                    axis = {(*a)[0].get<float>(), (*a)[1].get<float>(), (*a)[2].get<float>()};
                }
                if (const auto d = r->find("degrees_per_second"); d != r->end() && d->is_number()) rate = d->get<double>();
            }
            if (math::length(axis) > 1e-6f && rate != 0.0) {
                const float angle = static_cast<float>(rate * dt * 3.14159265358979323846 / 180.0);
                e.transform.rotation = math::normalize_quat(math::Quat::from_axis_angle(axis, angle) * e.transform.rotation);
            }
        }
        if (const auto m = e.components.find("input_mover"); m != e.components.end()) {
            double speed = 5.0;
            if (m->is_object()) {
                if (const auto s = m->find("speed"); s != m->end() && s->is_number()) speed = s->get<double>();
            }
            const float step = static_cast<float>(speed * dt);
            // move_y forward = -Z (right-handed, +Y up: ADR-022).
            e.transform.position = e.transform.position + math::Vec3{input.axis("move_x") * step, 0.0f, -input.axis("move_y") * step};
        }
    }
}

} // namespace noor::runtime
