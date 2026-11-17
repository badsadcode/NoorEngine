// Brush shapes as closed triangle meshes with logical faces (ADR-016). Built in the brush's own
// space around the origin, then moved into model space by the brush transform.
//
// Two rules keep them valid for the boolean kernel:
// - shared vertices: a corner used by several faces is one vertex (positions are computed once and
//   looked up exactly), so the mesh is closed;
// - winding from intent: every triangle is given with the direction its face points out of the
//   solid, and is flipped if needed. Shapes don't have to get the winding right by hand.
#include "noor/csg/csg.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <tuple>

namespace noor::csg {
namespace {

using math::Vec3;

struct Builder {
    BrushMesh mesh;
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, std::uint32_t> lookup;

    std::uint32_t vertex(Vec3 p) {
        const auto key = std::tuple{std::bit_cast<std::uint32_t>(p.x), std::bit_cast<std::uint32_t>(p.y), std::bit_cast<std::uint32_t>(p.z)};
        if (const auto it = lookup.find(key); it != lookup.end()) return it->second;
        const auto index = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.push_back(p);
        lookup.emplace(key, index);
        return index;
    }

    // A triangle on the face that points `outward` (only its rough direction matters).
    void triangle(Vec3 a, Vec3 b, Vec3 c, Vec3 outward, std::uint32_t face) {
        const Vec3 n = math::cross(b - a, c - a);
        if (math::dot(n, n) < 1e-20f) return;  // degenerate: nothing to add
        std::uint32_t ia = vertex(a), ib = vertex(b), ic = vertex(c);
        if (math::dot(n, outward) < 0.0f) std::swap(ib, ic);
        mesh.indices.insert(mesh.indices.end(), {ia, ib, ic});
        mesh.triangle_face.push_back(face);
    }

    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 outward, std::uint32_t face) {
        triangle(a, b, c, outward, face);
        triangle(a, c, d, outward, face);
    }
};

void box(Builder& b, Vec3 h) {
    const auto p = [&](float sx, float sy, float sz) { return Vec3{sx * h.x, sy * h.y, sz * h.z}; };
    b.quad(p(1, -1, -1), p(1, 1, -1), p(1, 1, 1), p(1, -1, 1), {1, 0, 0}, 0);
    b.quad(p(-1, -1, -1), p(-1, -1, 1), p(-1, 1, 1), p(-1, 1, -1), {-1, 0, 0}, 1);
    b.quad(p(-1, 1, -1), p(-1, 1, 1), p(1, 1, 1), p(1, 1, -1), {0, 1, 0}, 2);
    b.quad(p(-1, -1, -1), p(1, -1, -1), p(1, -1, 1), p(-1, -1, 1), {0, -1, 0}, 3);
    b.quad(p(-1, -1, 1), p(1, -1, 1), p(1, 1, 1), p(-1, 1, 1), {0, 0, 1}, 4);
    b.quad(p(-1, -1, -1), p(-1, 1, -1), p(1, 1, -1), p(1, -1, -1), {0, 0, -1}, 5);
}

void wedge(Builder& b, Vec3 h) {
    const Vec3 fl{-h.x, -h.y, h.z}, fr{h.x, -h.y, h.z}, bl{-h.x, -h.y, -h.z}, br{h.x, -h.y, -h.z};
    const Vec3 tl{-h.x, h.y, -h.z}, tr{h.x, h.y, -h.z};
    b.quad(fl, fr, tr, tl, {0, h.z, h.y}, 0);  // slope: up and towards the front
    b.quad(fl, bl, br, fr, {0, -1, 0}, 1);
    b.quad(bl, tl, tr, br, {0, 0, -1}, 2);
    b.triangle(fr, br, tr, {1, 0, 0}, 3);
    b.triangle(fl, tl, bl, {-1, 0, 0}, 4);
}

std::vector<Vec3> ring(Vec3 h, std::uint32_t n, float y) {
    std::vector<Vec3> out(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const float a = 2.0f * math::kPi * static_cast<float>(i) / static_cast<float>(n);
        out[i] = {h.x * std::cos(a), y, h.z * std::sin(a)};
    }
    return out;
}

void cylinder(Builder& b, Vec3 h, std::uint32_t n) {
    const auto top = ring(h, n, h.y), bottom = ring(h, n, -h.y);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t j = (i + 1) % n;
        const Vec3 out{top[i].x + top[j].x, 0, top[i].z + top[j].z};
        b.quad(bottom[i], bottom[j], top[j], top[i], out, 0);
        b.triangle({0, h.y, 0}, top[i], top[j], {0, 1, 0}, 1);
        b.triangle({0, -h.y, 0}, bottom[i], bottom[j], {0, -1, 0}, 2);
    }
}

void cone(Builder& b, Vec3 h, std::uint32_t n) {
    const auto bottom = ring(h, n, -h.y);
    const Vec3 apex{0, h.y, 0};
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t j = (i + 1) % n;
        b.triangle(bottom[i], bottom[j], apex, {bottom[i].x + bottom[j].x, 0.1f, bottom[i].z + bottom[j].z}, 0);
        b.triangle({0, -h.y, 0}, bottom[i], bottom[j], {0, -1, 0}, 1);
    }
}

void sphere(Builder& b, Vec3 h, std::uint32_t n) {
    const std::uint32_t rings = std::max(2u, n / 2);
    std::vector<std::vector<Vec3>> r(rings + 1);
    for (std::uint32_t j = 0; j <= rings; ++j) {
        const float phi = math::kPi * static_cast<float>(j) / static_cast<float>(rings);
        if (j == 0 || j == rings) {
            r[j] = {Vec3{0, j == 0 ? h.y : -h.y, 0}};
            continue;
        }
        r[j].resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a = 2.0f * math::kPi * static_cast<float>(i) / static_cast<float>(n);
            r[j][i] = {h.x * std::sin(phi) * std::cos(a), h.y * std::cos(phi), h.z * std::sin(phi) * std::sin(a)};
        }
    }
    const auto out = [&](Vec3 p) { return Vec3{p.x / h.x, p.y / h.y, p.z / h.z}; };  // the ellipsoid's outward
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t k = (i + 1) % n;
        b.triangle(r[0][0], r[1][i], r[1][k], out(r[1][i] + r[1][k]), 0);
        b.triangle(r[rings][0], r[rings - 1][i], r[rings - 1][k], out(r[rings - 1][i] + r[rings - 1][k]), 0);
        for (std::uint32_t j = 1; j + 1 < rings; ++j)
            b.quad(r[j][i], r[j][k], r[j + 1][k], r[j + 1][i], out(r[j][i] + r[j + 1][k]), 0);
    }
}

// Steps rising towards -Z. Each step column's side outline includes the corner of the step in
// front of it, so the sides have no T-junctions (no vertex lying on another triangle's edge).
void stairs(Builder& b, Vec3 h, std::uint32_t steps) {
    const std::uint32_t n = std::max(1u, steps);
    std::vector<float> z(n + 1), top(n);
    for (std::uint32_t k = 0; k <= n; ++k) z[k] = h.z - 2.0f * h.z * static_cast<float>(k) / static_cast<float>(n);
    for (std::uint32_t k = 0; k < n; ++k) top[k] = -h.y + 2.0f * h.y * static_cast<float>(k + 1) / static_cast<float>(n);
    enum : std::uint32_t { Treads, Risers, Sides, Back, Bottom };
    for (std::uint32_t k = 0; k < n; ++k) {
        const float below = k == 0 ? -h.y : top[k - 1];
        b.quad({-h.x, top[k], z[k]}, {h.x, top[k], z[k]}, {h.x, top[k], z[k + 1]}, {-h.x, top[k], z[k + 1]}, {0, 1, 0}, Treads);
        b.quad({-h.x, below, z[k]}, {h.x, below, z[k]}, {h.x, top[k], z[k]}, {-h.x, top[k], z[k]}, {0, 0, 1}, Risers);
        b.quad({-h.x, -h.y, z[k]}, {-h.x, -h.y, z[k + 1]}, {h.x, -h.y, z[k + 1]}, {h.x, -h.y, z[k]}, {0, -1, 0}, Bottom);
        for (const float x : {-h.x, h.x}) {
            // Column outline (z, y), fanned from its back-top corner (never a degenerate triangle).
            const Vec3 far{x, top[k], z[k + 1]};
            std::vector<Vec3> rest{{x, top[k], z[k]}};
            if (k > 0) rest.push_back({x, top[k - 1], z[k]});
            rest.push_back({x, -h.y, z[k]});
            rest.push_back({x, -h.y, z[k + 1]});
            for (std::size_t i = 0; i + 1 < rest.size(); ++i) b.triangle(far, rest[i], rest[i + 1], {x, 0, 0}, Sides);
        }
    }
    b.quad({-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, -h.y, -h.z}, {0, 0, -1}, Back);
}

// A doorway shape: the straight part, then a half circle (radius = half the width) on top.
void arch(Builder& b, Vec3 h, std::uint32_t n) {
    const float r = h.x;
    const float spring = std::max(-h.y, h.y - r);  // where the curve starts
    const std::uint32_t segments = std::max(2u, n / 2);
    std::vector<math::Vec2> outline{{-h.x, -h.y}, {h.x, -h.y}};
    std::vector<bool> curved{false, false};  // per outline edge starting at that point
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float a = math::kPi * static_cast<float>(i) / static_cast<float>(segments);
        outline.push_back({r * std::cos(a), spring + (h.y - spring) * std::sin(a)});
        curved.push_back(i < segments);
    }
    // outline now ends at (-r, spring); the closing edge goes down the left side.
    const auto at = [&](std::size_t i, float z) { return Vec3{outline[i].x, outline[i].y, z}; };
    enum : std::uint32_t { Front, Back, Straight, Curve };
    for (std::size_t i = 1; i + 1 < outline.size(); ++i) {
        b.triangle(at(0, h.z), at(i, h.z), at(i + 1, h.z), {0, 0, 1}, Front);
        b.triangle(at(0, -h.z), at(i, -h.z), at(i + 1, -h.z), {0, 0, -1}, Back);
    }
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const std::size_t j = (i + 1) % outline.size();
        const math::Vec2 mid{(outline[i].x + outline[j].x) * 0.5f, (outline[i].y + outline[j].y) * 0.5f};
        const Vec3 out = curved[i] ? Vec3{mid.x, mid.y - spring, 0} : i == 0 ? Vec3{0, -1, 0} : Vec3{mid.x, 0, 0};
        b.quad(at(i, h.z), at(j, h.z), at(j, -h.z), at(i, -h.z), out, curved[i] ? Curve : Straight);
    }
}

} // namespace

std::string_view shape_name(Shape shape) noexcept {
    switch (shape) {
    case Shape::Box: return "box";
    case Shape::Wedge: return "wedge";
    case Shape::Cylinder: return "cylinder";
    case Shape::Cone: return "cone";
    case Shape::Sphere: return "sphere";
    case Shape::Stairs: return "stairs";
    case Shape::Arch: return "arch";
    }
    return "box";
}

std::string_view operation_name(Operation op) noexcept {
    return op == Operation::Subtract ? "subtract" : op == Operation::Intersect ? "intersect" : "add";
}

std::uint32_t face_count(Shape shape) noexcept {
    switch (shape) {
    case Shape::Box: return 6;
    case Shape::Wedge: return 5;
    case Shape::Cylinder: return 3;
    case Shape::Cone: return 2;
    case Shape::Sphere: return 1;
    case Shape::Stairs: return 5;
    case Shape::Arch: return 4;
    }
    return 1;
}

std::string_view face_name(Shape shape, std::uint32_t face) noexcept {
    static constexpr std::string_view kBox[] = {"+X", "-X", "top", "bottom", "front", "back"};
    static constexpr std::string_view kWedge[] = {"slope", "bottom", "back", "+X side", "-X side"};
    static constexpr std::string_view kCylinder[] = {"side", "top", "bottom"};
    static constexpr std::string_view kCone[] = {"side", "bottom"};
    static constexpr std::string_view kStairs[] = {"treads", "risers", "sides", "back", "bottom"};
    static constexpr std::string_view kArch[] = {"front", "back", "sides and bottom", "curve"};
    if (face >= face_count(shape)) return "?";
    switch (shape) {
    case Shape::Box: return kBox[face];
    case Shape::Wedge: return kWedge[face];
    case Shape::Cylinder: return kCylinder[face];
    case Shape::Cone: return kCone[face];
    case Shape::Sphere: return "surface";
    case Shape::Stairs: return kStairs[face];
    case Shape::Arch: return kArch[face];
    }
    return "?";
}

BrushMesh brush_mesh(const Brush& brush) {
    Builder b;
    const Vec3 h{std::max(1e-4f, std::abs(brush.size.x) * 0.5f), std::max(1e-4f, std::abs(brush.size.y) * 0.5f),
                 std::max(1e-4f, std::abs(brush.size.z) * 0.5f)};
    const std::uint32_t sides = std::clamp(brush.sides, 3u, 256u);
    switch (brush.shape) {
    case Shape::Box: box(b, h); break;
    case Shape::Wedge: wedge(b, h); break;
    case Shape::Cylinder: cylinder(b, h, sides); break;
    case Shape::Cone: cone(b, h, sides); break;
    case Shape::Sphere: sphere(b, h, std::max(4u, sides)); break;
    case Shape::Stairs: stairs(b, h, std::clamp(brush.steps, 1u, 256u)); break;
    case Shape::Arch: arch(b, h, sides); break;
    }
    BrushMesh mesh = std::move(b.mesh);
    for (Vec3& p : mesh.positions) p = brush.transform.transform_point(p);
    // A mirroring transform (negative scale) turns the faces inside out: flip them back.
    const math::Mat4& m = brush.transform;
    const Vec3 x{m.m[0].x, m.m[0].y, m.m[0].z}, y{m.m[1].x, m.m[1].y, m.m[1].z}, z{m.m[2].x, m.m[2].y, m.m[2].z};
    if (math::dot(math::cross(x, y), z) < 0.0f)
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
    return mesh;
}

} // namespace noor::csg
