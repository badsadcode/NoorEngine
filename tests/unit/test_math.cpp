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

TEST_CASE("math: quaternion rotation matches axis-angle rotation") {
    const auto axis = Vec3{0.3f, 1.0f, 0.2f};
    const float angle = 1.1f;
    const Mat4 from_quat = to_mat4(Quat::from_axis_angle(axis, angle));
    const Mat4 direct = rotation(axis, angle);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) CHECK(near(from_quat.at(r, c), direct.at(r, c)));
}

TEST_CASE("math: quaternion product applies the right operand first") {
    const Quat yaw = Quat::from_axis_angle({0, 1, 0}, kPi / 2);   // +X -> -Z
    const Quat roll = Quat::from_axis_angle({0, 0, 1}, kPi / 2);  // +X -> +Y
    // (yaw * roll): roll first (+X -> +Y), then yaw leaves +Y alone.
    CHECK(near(to_mat4(yaw * roll).transform_point({1, 0, 0}), {0, 1, 0}));
}

TEST_CASE("math: compose is translate * rotate * scale (glTF TRS order)") {
    const Mat4 m = compose({10, 0, 0}, Quat::from_axis_angle({0, 1, 0}, kPi / 2), {2, 2, 2});
    // (1,0,0) -> scaled (2,0,0) -> rotated (0,0,-2) -> translated (10,0,-2)
    CHECK(near(m.transform_point({1, 0, 0}), {10, 0, -2}));
}

TEST_CASE("math: inverse undoes a TRS matrix") {
    const Mat4 m = compose({3, -2, 7}, Quat::from_axis_angle({0.3f, 1, 0.2f}, 1.1f), {2, 0.5f, 3});
    const Mat4 i = inverse(m);
    const Vec3 p{1.5f, -4, 2};
    const Vec3 back = i.transform_point(m.transform_point(p));
    CHECK(back.x == doctest::Approx(p.x).epsilon(1e-4));
    CHECK(back.y == doctest::Approx(p.y).epsilon(1e-4));
    CHECK(back.z == doctest::Approx(p.z).epsilon(1e-4));
    const Mat4 id = m * i;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) CHECK(id.at(r, c) == doctest::Approx(r == c ? 1.0f : 0.0f).epsilon(1e-4));
}

TEST_CASE("math: quaternion rotate matches the rotation matrix, conjugate undoes it") {
    const Quat q = Quat::from_axis_angle({1, 2, -0.5f}, 0.8f);
    const Vec3 v{0.3f, -1.2f, 2.5f};
    const Vec3 a = rotate(q, v);
    const Vec3 b = transform_vector(to_mat4(q), v);
    CHECK(a.x == doctest::Approx(b.x).epsilon(1e-5));
    CHECK(a.y == doctest::Approx(b.y).epsilon(1e-5));
    CHECK(a.z == doctest::Approx(b.z).epsilon(1e-5));
    const Vec3 c = rotate(conjugate(q), a);
    CHECK(c.x == doctest::Approx(v.x).epsilon(1e-5));
    CHECK(c.z == doctest::Approx(v.z).epsilon(1e-5));
}
