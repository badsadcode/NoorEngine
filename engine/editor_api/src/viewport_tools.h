#pragma once
// Internal to noor_editor_api: the maths behind viewport picking and transform gizmos.
// Pure functions with no GPU or window dependency, so they are unit-tested directly
// (tests/api/test_viewport_tools.cpp).

#include "noor/foundation/math.h"

#include <optional>

namespace noor::editor {

struct Ray {
    math::Vec3 origin;
    math::Vec3 dir;  // not necessarily unit length (object-space rays keep the world parameter t)
};

struct CameraBasis {
    math::Vec3 eye;
    math::Vec3 forward;  // unit
    math::Vec3 right;    // unit
    math::Vec3 up;       // unit
    float fov_y = math::radians(50.0f);
};

// Ray through pixel (px, py) of a width x height viewport (origin top-left), unit direction.
[[nodiscard]] Ray screen_ray(const CameraBasis& camera, float width, float height, float px, float py);

// Inverse of screen_ray: pixel position and distance from the eye, or nothing if behind the camera.
struct ScreenPoint {
    float x, y, distance;
};
[[nodiscard]] std::optional<ScreenPoint> world_to_screen(const CameraBasis& camera, float width, float height, math::Vec3 p);

// Gizmo arrow length = this x the gizmo's distance from the camera (constant size on screen).
inline constexpr float kGizmoScreenScale = 0.16f;

// Ray in the space of an object whose world matrix is `world` (same t parameter as the world ray).
[[nodiscard]] Ray to_object_space(const Ray& world_ray, const math::Mat4& world);

// Two-sided ray/triangle intersection (Möller–Trumbore). Returns t > 0 of the hit.
[[nodiscard]] std::optional<float> ray_triangle(const Ray& ray, math::Vec3 a, math::Vec3 b, math::Vec3 c);

// Ray/box intersection (slabs). Returns the entry t (0 if the origin is inside).
[[nodiscard]] std::optional<float> ray_aabb(const Ray& ray, math::Vec3 lo, math::Vec3 hi);

// Ray/plane intersection. Returns t > 0.
[[nodiscard]] std::optional<float> ray_plane(const Ray& ray, math::Vec3 point, math::Vec3 normal);

// ---- Gizmos (world-aligned, centered on the entity's origin, `size` = arrow length in metres) ----

enum class GizmoMode { None = 0, Move = 1, Rotate = 2 };
enum class Handle { None, MoveX, MoveY, MoveZ, MoveXZ, RotateX, RotateY, RotateZ };

[[nodiscard]] math::Vec3 handle_axis(Handle h);  // unit world axis (Y for MoveXZ)
[[nodiscard]] bool is_rotate(Handle h);

// Ring radius and the move-plane square, relative to `size`.
inline constexpr float kRingRadius = 0.85f;
inline constexpr float kPlaneMin = 0.2f;
inline constexpr float kPlaneMax = 0.45f;

// Which handle the ray points at (the nearest one within a tolerance), or None.
[[nodiscard]] Handle pick_handle(const Ray& ray, GizmoMode mode, math::Vec3 center, float size);

// An ongoing drag. Start with begin_drag, then call translation()/angle() with each new ray.
class GizmoDrag {
public:
    [[nodiscard]] static std::optional<GizmoDrag> begin(Handle handle, const Ray& ray, math::Vec3 center, math::Vec3 view_dir);

    [[nodiscard]] Handle handle() const noexcept { return handle_; }
    // Move handles: world translation since the drag started (along the axis or in the XZ plane).
    [[nodiscard]] math::Vec3 translation(const Ray& ray) const;
    // Rotate handles: radians about the handle axis since the drag started (right-hand rule),
    // accumulated across frames so it can exceed ±180°.
    [[nodiscard]] float angle(const Ray& ray);

private:
    [[nodiscard]] std::optional<math::Vec3> hit(const Ray& ray) const;
    [[nodiscard]] float raw_angle(math::Vec3 p) const;

    Handle handle_ = Handle::None;
    math::Vec3 center_;
    math::Vec3 plane_normal_;
    math::Vec3 start_;  // first hit on the drag plane
    float last_raw_ = 0.0f;
    float total_ = 0.0f;
};

} // namespace noor::editor
