// CSG kernel spike (Milestone 6, part 1, ADR-016): brush shapes are closed solids of the right
// volume, and the cases that break CSG kernels work: a doorway (with a coplanar floor edge),
// rotated and non-uniformly scaled brushes, touching boxes, a brush inside another, 100 cuts in one
// wall. Materials and UVs must follow source faces through the cuts.
#include "noor/csg/csg.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace noor;
using csg::Brush;
using csg::Operation;
using csg::Shape;
using math::Vec3;

namespace {

Brush make(Shape shape, Vec3 size, Vec3 at = {0, 0, 0}, Operation op = Operation::Add, std::uint32_t material = 0) {
    Brush b;
    b.shape = shape;
    b.size = size;
    b.operation = op;
    b.transform = math::translation(at);
    b.surface.material = material;
    return b;
}

csg::Model build(std::vector<Brush> brushes) {
    auto m = csg::build_model(brushes);
    REQUIRE_MESSAGE(m, (m ? "" : m.error().describe()));
    CHECK_MESSAGE(m->warnings.empty(), (m->warnings.empty() ? "" : m->warnings[0]));
    return std::move(*m);
}

double polygon_area(std::uint32_t n, double r) { return 0.5 * n * r * r * std::sin(2.0 * math::kPi / n); }

Vec3 centroid(const csg::Model& m, std::size_t tri) {
    const Vec3 a = m.vertices[m.indices[tri * 3]].position, b = m.vertices[m.indices[tri * 3 + 1]].position,
               c = m.vertices[m.indices[tri * 3 + 2]].position;
    return (a + b + c) * (1.0f / 3.0f);
}

} // namespace

TEST_CASE("csg: every brush shape is a closed solid with the volume it should have") {
    CHECK(build({make(Shape::Box, {2, 3, 4})}).volume == doctest::Approx(24.0).epsilon(1e-4));
    CHECK(build({make(Shape::Wedge, {2, 2, 2})}).volume == doctest::Approx(4.0).epsilon(1e-4));
    Brush cyl = make(Shape::Cylinder, {2, 2, 2});
    cyl.sides = 64;
    CHECK(build({cyl}).volume == doctest::Approx(polygon_area(64, 1.0) * 2.0).epsilon(1e-4));
    Brush cone = make(Shape::Cone, {2, 3, 2});
    cone.sides = 32;
    CHECK(build({cone}).volume == doctest::Approx(polygon_area(32, 1.0) * 3.0 / 3.0).epsilon(1e-4));
    Brush ball = make(Shape::Sphere, {2, 2, 2});
    ball.sides = 48;
    CHECK(build({ball}).volume == doctest::Approx(4.0 / 3.0 * math::kPi).epsilon(0.02));
    Brush stairs = make(Shape::Stairs, {1, 1, 1});
    stairs.steps = 4;
    CHECK(build({stairs}).volume == doctest::Approx(0.625).epsilon(1e-4));  // (1+2+3+4)/4 steps of 1/4 x 1/4
    Brush arch = make(Shape::Arch, {1, 2, 0.2f});
    arch.sides = 64;
    // A 1 x 1.5 rectangle plus a half circle of radius 0.5 (as a 32-segment half polygon), 0.2 deep.
    CHECK(build({arch}).volume == doctest::Approx((1.5 + polygon_area(64, 0.5) / 2.0) * 0.2).epsilon(1e-4));
    for (const Shape s : {Shape::Box, Shape::Wedge, Shape::Cylinder, Shape::Cone, Shape::Sphere, Shape::Stairs, Shape::Arch})
        for (std::uint32_t f = 0; f < csg::face_count(s); ++f) CHECK(csg::face_name(s, f) != "?");
}

TEST_CASE("csg: a doorway cut into a wall keeps the wall's surface, the new faces get the cutter's") {
    // A brick wall (material 1, 2 repeats per metre) and a door (material 2) whose bottom is
    // coplanar with the wall's: the classic case that leaves slivers in weak kernels.
    Brush wall = make(Shape::Box, {4, 3, 0.3f}, {0, 0, 0}, Operation::Add, 1);
    wall.surface.scale = {2, 2};
    const Brush door = make(Shape::Box, {1, 2, 1}, {0, -0.5f, 0}, Operation::Subtract, 2);
    const auto model = build({wall, door});
    CHECK(model.volume == doctest::Approx(4 * 3 * 0.3 - 1 * 2 * 0.3).epsilon(1e-4));

    std::size_t wall_front = 0, jambs = 0;
    for (std::size_t t = 0; t < model.triangle_count(); ++t) {
        const auto src = model.triangle_source[t];
        if (src.brush == 0 && src.face == 4) ++wall_front;
        if (src.brush == 1) {
            ++jambs;
            // The door's faces inside the opening: material 2.
            const auto sub = std::find_if(model.submeshes.begin(), model.submeshes.end(), [&](const auto& s) {
                return t * 3 >= s.first_index && t * 3 < s.first_index + s.index_count;
            });
            REQUIRE(sub != model.submeshes.end());
            CHECK(sub->material == 2);
            // Every exposed door face lies inside the wall's thickness.
            CHECK(std::abs(centroid(model, t).z) <= 0.15f + 1e-4f);
        }
    }
    CHECK(wall_front > 2);
    CHECK(jambs >= 6);  // two sides and the top, each a quad (the bottom is open)
    CHECK(model.submeshes.size() == 2);

    // UVs are anchored in model space: on the wall's front (+Z) face u = x, v = -y, times the scale,
    // with or without the door.
    for (std::size_t i = 0; i < model.indices.size(); ++i) {
        const auto src = model.triangle_source[i / 3];
        if (src.brush != 0 || src.face != 4) continue;
        const auto& v = model.vertices[model.indices[i]];
        CHECK(v.uv.x == doctest::Approx(v.position.x * 2).epsilon(1e-5));
        CHECK(v.uv.y == doctest::Approx(-v.position.y * 2).epsilon(1e-5));
        CHECK(v.normal.z == doctest::Approx(1.0f));
        CHECK(std::abs(v.tangent.w) == doctest::Approx(1.0f));
    }
}

TEST_CASE("csg: rotated and non-uniformly scaled brushes cut exactly") {
    const math::Mat4 turn = math::rotation({0, 1, 0}, math::radians(37.0f));
    Brush wall = make(Shape::Box, {6, 3, 0.3f});
    wall.transform = turn;
    // A unit box stretched to 0.5 x 2 x 3, turned with the wall: a 0.5 x 2 hole through 0.3 m.
    Brush window = make(Shape::Box, {1, 1, 1}, {}, Operation::Subtract);
    window.transform = turn * math::scale({0.5f, 2.0f, 3.0f});
    CHECK(build({wall, window}).volume == doctest::Approx(6 * 3 * 0.3 - 0.5 * 2 * 0.3).epsilon(1e-4));

    // Tilted 20 degrees the other way: the hole is a slanted prism; its volume is the window's
    // cross-section times the thickness divided by cos(20 degrees) along the slant.
    Brush slanted = make(Shape::Box, {0.5f, 0.5f, 3}, {}, Operation::Subtract);
    slanted.transform = math::rotation({1, 0, 0}, math::radians(20.0f));
    const auto m = build({make(Shape::Box, {6, 3, 0.3f}), slanted});
    CHECK(m.volume == doctest::Approx(6 * 3 * 0.3 - 0.5 * 0.5 * 0.3 / std::cos(math::radians(20.0f))).epsilon(1e-3));

    // A mirrored brush (negative scale) is still a solid, not an inside-out one.
    Brush mirrored = make(Shape::Wedge, {2, 2, 2});
    mirrored.transform = math::scale({-1, 1, 1});
    CHECK(build({mirrored}).volume == doctest::Approx(4.0).epsilon(1e-4));
}

TEST_CASE("csg: touching boxes merge without an inside wall, a brush inside another makes a hollow") {
    const auto merged = build({make(Shape::Box, {1, 1, 1}), make(Shape::Box, {1, 1, 1}, {1, 0, 0})});
    CHECK(merged.volume == doctest::Approx(2.0).epsilon(1e-4));
    for (std::size_t t = 0; t < merged.triangle_count(); ++t)
        CHECK_MESSAGE(std::abs(centroid(merged, t).x - 0.5f) > 1e-3f, "a face was left between the two boxes");

    const auto hollow = build({make(Shape::Box, {2, 2, 2}), make(Shape::Box, {1, 1, 1}, {}, Operation::Subtract, 3)});
    CHECK(hollow.volume == doctest::Approx(7.0).epsilon(1e-4));
    CHECK(std::any_of(hollow.triangle_source.begin(), hollow.triangle_source.end(), [](const auto& s) { return s.brush == 1; }));

    const auto lens = build({make(Shape::Box, {2, 2, 2}), make(Shape::Sphere, {2.4f, 2.4f, 2.4f}, {}, Operation::Intersect)});
    CHECK(lens.volume > 4.0);  // more than the ball's inscribed...
    CHECK(lens.volume < 8.0);  // ...less than the box

    // Nothing to cut from yet: an empty model, not an error.
    auto empty = csg::build_model(std::vector<Brush>{make(Shape::Box, {1, 1, 1}, {}, Operation::Subtract)});
    REQUIRE(empty);
    CHECK(empty->triangle_count() == 0);
}

TEST_CASE("csg: a wall with 100 windows, built twice, is the same to the byte") {
    std::vector<Brush> brushes{make(Shape::Box, {10, 3, 0.3f}, {}, Operation::Add, 1)};
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 20; ++col)
            brushes.push_back(make(Shape::Box, {0.2f, 0.2f, 1}, {-4.75f + col * 0.5f, -1.0f + row * 0.5f, 0}, Operation::Subtract, 2));
    const auto start = std::chrono::steady_clock::now();
    const auto first = build(brushes);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("101 brushes: " << ms << " ms, " << first.triangle_count() << " triangles, " << first.vertices.size() << " vertices");
    CHECK(first.volume == doctest::Approx(10 * 3 * 0.3 - 100 * 0.2 * 0.2 * 0.3).epsilon(1e-4));
    const auto second = build(brushes);
    REQUIRE(second.vertices.size() == first.vertices.size());
    CHECK(second.indices == first.indices);
    CHECK(std::memcmp(second.vertices.data(), first.vertices.data(), first.vertices.size() * sizeof(csg::Vertex)) == 0);
}
