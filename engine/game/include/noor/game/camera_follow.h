#pragma once
// Cameras that follow (2D foundations, part 6): a built-in behaviour for the classic 2D camera, usable in 3D too.
//
//   "camera_follow": {"target": "<entity name or id>", "offset": [0, 1],
//                     "dead_zone": [2, 1.5],          // metres: the target moves freely inside this box first
//                     "smoothing": 0.15,              // seconds to close most of the gap (0: at once)
//                     "bounds": [x0, y0, x1, y1]}     // the view's edges stay inside (needs an orthographic camera)
//
// It works in the camera's XY plane (2D's), runs after physics each frame (so it follows where the target is drawn),
// and only moves the camera entity. The view's width for the bounds comes from the camera's height and the display's
// shape (game/display.h).

#include "noor/runtime/world.h"

namespace noor::game {

class CameraFollowSystem final : public runtime::System {
public:
    explicit CameraFollowSystem(float aspect = 16.0f / 9.0f) : aspect_(aspect) {}
    [[nodiscard]] std::string_view name() const override { return "camera_follow"; }
    void late_update(runtime::World& world, double dt) override;

private:
    float aspect_;
};

// The step itself (pure, for tests): where a camera at `camera` goes this frame, following `target`.
struct FollowSettings {
    math::Vec2 dead_zone{0, 0};
    float smoothing = 0.0f;
    bool bounded = false;
    math::Vec4 bounds{0, 0, 0, 0};  // x0, y0, x1, y1
    math::Vec2 half_view{0, 0};     // half the view's width and height (bounds)
};
[[nodiscard]] math::Vec2 follow_step(math::Vec2 camera, math::Vec2 target, const FollowSettings& s, double dt) noexcept;

// Parallax (2D part 6, the `parallax` component, applied where scenes are drawn): how far a layer moves for a viewer at
// `viewer`: (viewer - anchor) x (1 - factor). Factor 1: none (the world); 0: all of it (stays with the camera).
[[nodiscard]] constexpr math::Vec3 parallax_offset(math::Vec2 viewer, math::Vec2 anchor, math::Vec2 factor) noexcept {
    return {(viewer.x - anchor.x) * (1.0f - factor.x), (viewer.y - anchor.y) * (1.0f - factor.y), 0.0f};
}

} // namespace noor::game
