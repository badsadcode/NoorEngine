#include "noor/render/fly_camera.h"

#include <ostream>
#include <doctest/doctest.h>

using noor::math::Vec3;
using noor::render::FlyCamera;
using noor::render::FlyCameraInput;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-4f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }
} // namespace

TEST_CASE("fly camera: default looks down -Z with +X to the right") {
    FlyCamera cam;
    CHECK(near(cam.forward(), {0, 0, -1}));
    CHECK(near(cam.right(), {1, 0, 0}));
}

TEST_CASE("fly camera: moving forward for 1 s at 5 m/s goes 5 m along -Z") {
    FlyCamera cam;
    cam.position = {0, 0, 0};
    cam.update({.move = {0, 0, 1}}, 1.0f);
    CHECK(near(cam.position, {0, 0, -5}));
}

TEST_CASE("fly camera: moving the mouse right turns right") {
    FlyCamera cam;
    cam.update({.look_dx = 100.0f}, 0.0f);
    CHECK(cam.forward().x > 0.0f);
}

TEST_CASE("fly camera: positive yaw of 90 degrees looks down -X") {
    FlyCamera cam;
    cam.yaw = noor::math::kPi / 2;
    CHECK(near(cam.forward(), {-1, 0, 0}));
}

TEST_CASE("fly camera: pitch is clamped so the camera never flips over") {
    FlyCamera cam;
    cam.update({.look_dy = -100000.0f}, 0.0f);  // mouse far up
    CHECK(cam.pitch <= FlyCamera::kMaxPitch);
    CHECK(cam.forward().y > 0.99f);
    cam.update({.look_dy = 100000.0f}, 0.0f);
    CHECK(cam.pitch >= -FlyCamera::kMaxPitch);
}

TEST_CASE("fly camera: diagonal movement is not faster and looking down doesn't dive") {
    FlyCamera cam;
    cam.position = {0, 0, 0};
    cam.pitch = -1.0f;  // looking down
    cam.update({.move = {1, 0, 1}}, 1.0f);
    CHECK(near(noor::math::length(cam.position), 5.0f));
    CHECK(near(cam.position.y, 0.0f));
}

TEST_CASE("fly camera: look_at aims at the target and the view puts it on -Z") {
    FlyCamera cam;
    cam.position = {3, 2, 4};
    cam.look_at({0, 0, 0});
    const Vec3 in_view = cam.view().transform_point({0, 0, 0});
    CHECK(near(in_view.x, 0.0f));
    CHECK(near(in_view.y, 0.0f));
    CHECK(in_view.z < 0.0f);
}
