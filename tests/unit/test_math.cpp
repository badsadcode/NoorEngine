// Locks in the conventions of ADR-022.
#include "noor/foundation/math.h"

#include <ostream>
#include <doctest/doctest.h>

using namespace noor::math;

namespace {
bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-5f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }
} // namespace

TEST_CASE("math: cross product is right-handed (X cross Y = Z)") {
    CHECK(cross({1, 0, 0}, {0, 1, 0}) == Vec3{0, 0, 1});
}

TEST_CASE("math: matrices are column-major, translation lives in column 3") {
    const Mat4 t = translation({1, 2, 3});
    CHECK(t.m[3] == Vec4{1, 2, 3, 1});
    CHECK(t.at(0, 3) == 1.0f);
    CHECK(near(t.transform_point({0, 0, 0}), {1, 2, 3}));
}

TEST_CASE("math: identity and multiplication") {
    const Mat4 a = translation({1, 0, 0}) * scale({2, 2, 2});
    CHECK(near((Mat4::identity() * a).transform_point({1, 1, 1}), a.transform_point({1, 1, 1})));
    // T * S applies scale first: (1,1,1) -> (2,2,2) -> (3,2,2)
    CHECK(near(a.transform_point({1, 1, 1}), {3, 2, 2}));
}

TEST_CASE("math: rotation follows the right-hand rule") {
    // 90° around +Y turns +X into −Z.
    CHECK(near(rotation({0, 1, 0}, kPi / 2).transform_point({1, 0, 0}), {0, 0, -1}));
    // 90° around +Z turns +X into +Y.
    CHECK(near(rotation({0, 0, 1}, kPi / 2).transform_point({1, 0, 0}), {0, 1, 0}));
}

TEST_CASE("math: look_at puts the target on the camera's -Z axis") {
    const Mat4 view = look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(near(view.transform_point({0, 0, 0}), {0, 0, -5}));
    CHECK(near(view.transform_point({0, 1, 0}), {0, 1, -5}));  // up stays up
    CHECK(near(view.transform_point({1, 0, 0}), {1, 0, -5}));  // right stays right
}

TEST_CASE("math: reverse-Z infinite projection maps near to 1 and far toward 0") {
    const Mat4 proj = perspective_reverse_z(radians(60), 16.0f / 9.0f, 0.1f);
    CHECK(near(proj.transform_point({0, 0, -0.1f}).z, 1.0f));
    CHECK(proj.transform_point({0, 0, -1000.0f}).z < 0.001f);
    CHECK(proj.transform_point({0, 0, -1000.0f}).z > 0.0f);
    // Closer points have greater depth (compare op GREATER).
    CHECK(proj.transform_point({0, 0, -2}).z > proj.transform_point({0, 0, -3}).z);
}

TEST_CASE("math: projection flips Y for Vulkan (world up is screen up)") {
    const Mat4 proj = perspective_reverse_z(radians(90), 1.0f, 0.1f);
    // A point above the view axis ends up with negative NDC y (Vulkan y points down).
    CHECK(proj.transform_point({0, 1, -1}).y < 0.0f);
    CHECK(proj.transform_point({1, 0, -1}).x > 0.0f);
}
