#pragma once
// Free-fly camera controller (editor/debug style). Pure math: it doesn't read input itself,
// so it works with any input source and is unit-testable.
//
// Conventions (ADR-022): yaw 0 looks down −Z; positive yaw turns left (counter-clockwise
// seen from above); positive pitch looks up.

#include "noor/foundation/math.h"

#include <algorithm>
#include <cmath>

namespace noor::render {

struct FlyCameraInput {
    // Desired movement in camera space: x right, y up (world up), z forward. Each in [-1, 1].
    math::Vec3 move;
    float look_dx = 0.0f;  // mouse movement in pixels, right is positive
    float look_dy = 0.0f;  // mouse movement in pixels, down is positive
    bool boost = false;    // move faster (e.g. Shift)
};

class FlyCamera {
public:
    math::Vec3 position{0.0f, 1.8f, 5.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float speed = 5.0f;              // metres per second
    float boost_multiplier = 4.0f;
    float sensitivity = 0.0025f;     // radians per pixel

    static constexpr float kMaxPitch = math::radians(89.0f);

    [[nodiscard]] math::Vec3 forward() const noexcept {
        const float cp = std::cos(pitch);
        return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
    }
    [[nodiscard]] math::Vec3 right() const noexcept { return {std::cos(yaw), 0.0f, -std::sin(yaw)}; }

    void update(const FlyCameraInput& in, float dt) noexcept {
        yaw -= in.look_dx * sensitivity;
        pitch = std::clamp(pitch - in.look_dy * sensitivity, -kMaxPitch, kMaxPitch);
        yaw = std::remainder(yaw, 2.0f * math::kPi);

        // Horizontal movement follows the view direction flattened onto the ground plane,
        // so looking down doesn't make "forward" dive into the floor.
        const math::Vec3 flat_forward = math::normalize({-std::sin(yaw), 0.0f, -std::cos(yaw)});
        const math::Vec3 dir = right() * in.move.x + math::Vec3{0, 1, 0} * in.move.y + flat_forward * in.move.z;
        const float len = math::length(dir);
        if (len > 1e-6f) {
            const float step = speed * (in.boost ? boost_multiplier : 1.0f) * dt;
            position = position + dir * (step / std::max(len, 1.0f));  // diagonal isn't faster
        }
    }

    // Points the camera at a target (sets yaw/pitch, keeps position).
    void look_at(math::Vec3 target) noexcept {
        const math::Vec3 d = math::normalize(target - position);
        yaw = std::atan2(-d.x, -d.z);
        pitch = std::clamp(std::asin(std::clamp(d.y, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
    }

    [[nodiscard]] math::Mat4 view() const noexcept {
        return math::look_at(position, position + forward(), {0.0f, 1.0f, 0.0f});
    }
};

} // namespace noor::render
