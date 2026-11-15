// Picking and gizmo maths used by the editor viewport (engine/editor_api/src/viewport_tools.*).
#include <ostream>
#include <doctest/doctest.h>

#include "viewport_tools.h"

#include <cmath>

using namespace noor::editor;
using noor::math::Vec3;

namespace {

bool near(Vec3 a, Vec3 b, float eps = 1e-3f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

// Camera at (0, 0, 10) looking down -Z.
CameraBasis front_camera() { return {{0, 0, 10}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0}, noor::math::radians(50.0f)}; }

} // namespace

TEST_CASE("viewport tools: the centre pixel's ray goes straight ahead; screen up is world up") {
    const Ray centre = screen_ray(front_camera(), 101, 101, 50, 50);
    CHECK(near(centre.origin, {0, 0, 10}));
    CHECK(near(centre.dir, {0, 0, -1}));
    const Ray top = screen_ray(front_camera(), 101, 101, 50, 0);
    CHECK(top.dir.y > 0.3f);
    const Ray right = screen_ray(front_camera(), 101, 101, 100, 50);
    CHECK(right.dir.x > 0.3f);
}

TEST_CASE("viewport tools: ray/triangle hits inside, misses outside, either winding") {
    const Ray r{{0.2f, 0.2f, 5}, {0, 0, -1}};
    const auto t = ray_triangle(r, {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
    REQUIRE(t);
    CHECK(*t == doctest::Approx(5.0f));
    CHECK(ray_triangle(r, {0, 0, 0}, {0, 1, 0}, {1, 0, 0}));  // back face
    CHECK_FALSE(ray_triangle({{0.8f, 0.8f, 5}, {0, 0, -1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}));
    CHECK_FALSE(ray_triangle({{0.2f, 0.2f, -5}, {0, 0, -1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}));  // behind
}

TEST_CASE("viewport tools: ray/box and object-space rays keep the world distance") {
    CHECK(ray_aabb({{0, 0, 10}, {0, 0, -1}}, {-1, -1, -1}, {1, 1, 1}).value() == doctest::Approx(9.0f));
    CHECK_FALSE(ray_aabb({{3, 0, 10}, {0, 0, -1}}, {-1, -1, -1}, {1, 1, 1}));
    // A box scaled x2 and moved to z = -5: its front face (z = -3) is 13 m away in world terms.
    const auto world = noor::math::translation({0, 0, -5}) * noor::math::scale({2, 2, 2});
    const Ray local = to_object_space({{0, 0, 10}, {0, 0, -1}}, world);
    CHECK(ray_aabb(local, {-1, -1, -1}, {1, 1, 1}).value() == doctest::Approx(13.0f));
}

TEST_CASE("viewport tools: gizmo handles are picked where they are drawn") {
    const Vec3 c{0, 0, 0};
    // Looking from the front, the X arrow runs to the right of the centre.
    const float px = 500.0f + 0.06f / std::tan(noor::math::radians(25.0f)) * 500.0f;  // 0.6 m right at 10 m
    const Ray at_x = screen_ray(front_camera(), 1000, 1000, px, 500);
    CHECK(pick_handle(at_x, GizmoMode::Move, c, 1.0f) == Handle::MoveX);
    const Ray at_y{{0, 0.6f, 10}, {0, 0, -1}};
    CHECK(pick_handle(at_y, GizmoMode::Move, c, 1.0f) == Handle::MoveY);
    CHECK(pick_handle({{3, 3, 10}, {0, 0, -1}}, GizmoMode::Move, c, 1.0f) == Handle::None);
    // From above, the XZ square is at (0.2..0.45, 0.2..0.45).
    CHECK(pick_handle({{0.3f, 10, 0.3f}, {0, -1, 0}}, GizmoMode::Move, c, 1.0f) == Handle::MoveXZ);
    // The Z ring (around the view axis here) is a circle of radius 0.85 facing us. (Not at
    // (0.85, 0): the edge-on Y ring passes through that point too.)
    CHECK(pick_handle({{0.6f, 0.6f, 10}, {0, 0, -1}}, GizmoMode::Rotate, c, 1.0f) == Handle::RotateZ);
    CHECK(pick_handle({{0.0f, 0, 10}, {0, 0, -1}}, GizmoMode::Rotate, c, 1.0f) != Handle::RotateZ);
}

TEST_CASE("viewport tools: dragging the X arrow moves along X only") {
    const CameraBasis cam{{4, 3, 10}, noor::math::normalize(Vec3{-4, -3, -10}), {}, {}, noor::math::radians(50.0f)};
    const Vec3 start_point{0.5f, 0, 0};
    const Ray start{cam.eye, noor::math::normalize(start_point - cam.eye)};
    auto drag = GizmoDrag::begin(Handle::MoveX, start, {0, 0, 0}, cam.forward);
    REQUIRE(drag);
    const Vec3 later_point{2.5f, 0.0f, 0.0f};
    const Vec3 d = drag->translation({cam.eye, noor::math::normalize(later_point - cam.eye)});
    CHECK(near(d, {2.0f, 0, 0}));
    // XZ plane drag keeps Y at zero.
    auto plane = GizmoDrag::begin(Handle::MoveXZ, {{0.3f, 10, 0.3f}, {0, -1, 0}}, {0, 0, 0}, {0, -1, 0});
    REQUIRE(plane);
    CHECK(near(plane->translation({{1.3f, 10, -0.7f}, {0, -1, 0}}), {1.0f, 0, -1.0f}));
}

TEST_CASE("viewport tools: rotation drags accumulate past 180 degrees with the right-hand sign") {
    // Looking down -Y at the Y ring: moving the pointer from +X towards -Z is a positive
    // (counter-clockwise from above) rotation about +Y.
    auto drag = GizmoDrag::begin(Handle::RotateY, {{1, 10, 0}, {0, -1, 0}}, {0, 0, 0}, {0, -1, 0});
    REQUIRE(drag);
    CHECK(drag->angle({{0, 10, -1}, {0, -1, 0}}) == doctest::Approx(noor::math::kPi / 2).epsilon(1e-3));
    CHECK(drag->angle({{-1, 10, 0}, {0, -1, 0}}) == doctest::Approx(noor::math::kPi).epsilon(1e-3));
    CHECK(drag->angle({{0, 10, 1}, {0, -1, 0}}) == doctest::Approx(1.5f * noor::math::kPi).epsilon(1e-3));
    // Confirm the sign against the engine's own rotation: +90° about Y takes +X to -Z.
    const Vec3 x_turned = noor::math::rotation({0, 1, 0}, noor::math::kPi / 2).transform_point({1, 0, 0});
    CHECK(near(x_turned, {0, 0, -1}));
}

TEST_CASE("viewport tools: world_to_screen inverts screen_ray") {
    const CameraBasis cam{{4, 3, 10}, noor::math::normalize(Vec3{-4, -3, -10}), {}, {}, noor::math::radians(50.0f)};
    CameraBasis c = cam;
    c.right = noor::math::normalize(noor::math::cross(c.forward, {0, 1, 0}));
    c.up = noor::math::cross(c.right, c.forward);
    const auto sp = world_to_screen(c, 1280, 720, {1.5f, 0.5f, -2});
    REQUIRE(sp);
    const Ray back = screen_ray(c, 1280, 720, sp->x, sp->y);
    const Vec3 p = back.origin + back.dir * sp->distance;
    CHECK(near(p, {1.5f, 0.5f, -2}, 1e-3f));
    CHECK_FALSE(world_to_screen(c, 1280, 720, c.eye - c.forward));  // behind the camera
}

TEST_CASE("editor camera: look turns the view around the eye; orbit circles the target") {
    EditorCamera cam;
    const Vec3 eye0 = cam.eye();
    const auto b0 = cam.basis();
    cam.look(50, 0);  // mouse right: turn right
    CHECK(near(cam.eye(), eye0, 1e-3f));
    CHECK(noor::math::dot(cam.basis().forward, b0.right) > 0.1f);
    cam.look(0, -40);  // mouse up: look up
    CHECK(cam.basis().forward.y > b0.forward.y);
    CHECK(near(cam.eye(), eye0, 1e-3f));

    EditorCamera orbiting;
    const Vec3 target0 = orbiting.target;
    orbiting.orbit(80, 20);
    CHECK(near(orbiting.target, target0));
    CHECK(noor::math::length(orbiting.eye() - orbiting.target) == doctest::Approx(orbiting.distance));
}

TEST_CASE("editor camera: fly moves at fly_speed, Shift is faster, a stalled frame doesn't leap") {
    EditorCamera cam;
    cam.fly_speed = 10.0f;
    const auto b = cam.basis();
    const Vec3 e0 = cam.eye();
    cam.fly({0, 0, 1}, 0.05f, false);
    CHECK(near(cam.eye() - e0, b.forward * 0.5f, 1e-3f));
    const Vec3 e1 = cam.eye();
    cam.fly({0, 1, 0}, 0.05f, true);  // E: straight up in world space, x4
    CHECK(near(cam.eye() - e1, {0, 2.0f, 0}, 1e-3f));
    const Vec3 e2 = cam.eye();
    cam.fly({1, 0, 0}, 3.0f, false);  // a 3 s hitch counts as 0.1 s
    CHECK(noor::math::length(cam.eye() - e2) == doctest::Approx(1.0f).epsilon(1e-3));
    cam.change_fly_speed(1);
    CHECK(cam.fly_speed == doctest::Approx(12.5f));
}

TEST_CASE("viewport tools: snap rounds to the step") {
    CHECK(snap(1.26f, 0.25f) == doctest::Approx(1.25f));
    CHECK(snap(-0.8f, 1.0f) == doctest::Approx(-1.0f));
    CHECK(snap(0.37f, 0.0f) == doctest::Approx(0.37f));  // step 0: off
}

TEST_CASE("viewport tools: scale handles follow the entity's rotation and scale by grab distance") {
    const Vec3 c{0, 0, 0};
    // Turned 90 degrees about Y: the entity's X axis points along world -Z.
    const auto turned = noor::math::Quat::from_axis_angle({0, 1, 0}, noor::math::kPi / 2);
    CHECK(pick_handle({{0, 10, -0.6f}, {0, -1, 0}}, GizmoMode::Scale, c, 1.0f, turned) == Handle::ScaleX);
    CHECK(pick_handle({{0.6f, 10, 0}, {0, -1, 0}}, GizmoMode::Scale, c, 1.0f, turned) != Handle::ScaleX);
    CHECK(pick_handle({{0.02f, 0.03f, 10}, {0, 0, -1}}, GizmoMode::Scale, c, 1.0f) == Handle::ScaleAll);

    // Grab the X handle at 0.9 m and pull it to 1.8 m: twice as wide.
    const Vec3 eye{0.9f, 5, 8};
    auto drag = GizmoDrag::begin(Handle::ScaleX, {eye, noor::math::normalize(Vec3{0.9f, 0, 0} - eye)}, c,
                                 noor::math::normalize(Vec3{0, -5, -8}), 1.0f);
    REQUIRE(drag);
    const Vec3 f = drag->scale({eye, noor::math::normalize(Vec3{1.8f, 0, 0} - eye)});
    CHECK(f.x == doctest::Approx(2.0f).epsilon(1e-3));
    CHECK(f.y == doctest::Approx(1.0f));
    CHECK(f.z == doctest::Approx(1.0f));
}
