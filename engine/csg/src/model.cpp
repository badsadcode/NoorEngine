// Brushes combined into one model (ADR-016): booleans by Manifold, then materials, normals, UVs
// and tangents from each output triangle's source face.
//
// Manifold keeps two things through every boolean that make this work: each input mesh's
// "original id" (we map it back to the brush) and a per-triangle face id (our logical face).
#include "noor/csg/csg.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <manifold/manifold.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <tuple>
#include <unordered_map>

namespace noor::csg {
namespace {

using math::Vec2;
using math::Vec3;

// Faces whose normals are smoothed across their triangles (the rest are flat, with hard edges).
bool smooth(Shape shape, std::uint32_t face) {
    return (shape == Shape::Cylinder && face == 0) || (shape == Shape::Cone && face == 0) || shape == Shape::Sphere ||
           (shape == Shape::Arch && face == 3);
}

manifold::MeshGL to_mesh_gl(const BrushMesh& mesh) {
    manifold::MeshGL gl;
    gl.numProp = 3;
    gl.vertProperties.reserve(mesh.positions.size() * 3);
    for (const Vec3& p : mesh.positions) gl.vertProperties.insert(gl.vertProperties.end(), {p.x, p.y, p.z});
    gl.triVerts = mesh.indices;
    gl.faceID = mesh.triangle_face;
    return gl;
}

// Box projection: the two axes across the face's main direction, oriented so the texture reads
// the right way round seen from outside (+v down the picture, like glTF).
struct Frame {
    Vec3 u, v;  // gradients of u and v in model space (unit vectors)
};
Frame projection_frame(Vec3 n) {
    const float ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
    if (ax >= ay && ax >= az) return {{0, 0, n.x > 0 ? -1.0f : 1.0f}, {0, -1, 0}};
    if (az >= ay) return {{n.z > 0 ? 1.0f : -1.0f, 0, 0}, {0, -1, 0}};
    return {{1, 0, 0}, {0, 0, n.y > 0 ? 1.0f : -1.0f}};
}

Vec2 project(Vec3 p, const Frame& f, const Surface& s, float c, float sn) {
    const float u = math::dot(p, f.u), v = math::dot(p, f.v);
    const float ru = c * u - sn * v, rv = sn * u + c * v;
    return {ru * s.scale.x + s.offset.x, rv * s.scale.y + s.offset.y};
}

} // namespace

Result<Model> build_model(std::span<const Brush> brushes) {
    Model out;
    std::unordered_map<int, std::uint32_t> brush_of_original;  // Manifold original id -> brush index
    manifold::Manifold solid;
    bool started = false;
    for (std::uint32_t i = 0; i < brushes.size(); ++i) {
        const Brush& brush = brushes[i];
        const BrushMesh mesh = brush_mesh(brush);
        manifold::Manifold piece = manifold::Manifold(to_mesh_gl(mesh)).AsOriginal();
        if (piece.Status() != manifold::Manifold::Error::NoError) {
            out.warnings.push_back(std::format("brush {} ({}) skipped: not a closed solid (Manifold error {})", i,
                                               shape_name(brush.shape), static_cast<int>(piece.Status())));
            continue;
        }
        brush_of_original[piece.OriginalID()] = i;
        if (!started) {
            // Nothing to subtract from or intersect with yet.
            if (brush.operation != Operation::Add) continue;
            solid = piece;
            started = true;
            continue;
        }
        switch (brush.operation) {
        case Operation::Add: solid = solid + piece; break;
        case Operation::Subtract: solid = solid - piece; break;
        case Operation::Intersect: solid = solid ^ piece; break;
        }
    }
    if (!started) return out;
    if (solid.Status() != manifold::Manifold::Error::NoError)
        return fail(ErrorCode::InvalidArgument, std::format("CSG failed (Manifold error {})", static_cast<int>(solid.Status())));
    out.volume = solid.Volume();

    const manifold::MeshGL gl = solid.GetMeshGL();
    const std::size_t tri_count = gl.NumTri();
    const auto position = [&](std::uint32_t v) {
        return Vec3{gl.vertProperties[v * gl.numProp], gl.vertProperties[v * gl.numProp + 1], gl.vertProperties[v * gl.numProp + 2]};
    };

    // Each triangle's source: its run's original id -> brush, its face id -> logical face.
    std::vector<Model::Source> source(tri_count);
    for (std::size_t r = 0; r < gl.runOriginalID.size(); ++r) {
        const auto it = brush_of_original.find(static_cast<int>(gl.runOriginalID[r]));
        const std::uint32_t brush = it == brush_of_original.end() ? 0u : it->second;
        for (std::size_t t = gl.runIndex[r] / 3; t < gl.runIndex[r + 1] / 3; ++t)
            source[t] = {brush, t < gl.faceID.size() ? gl.faceID[t] : 0u};
    }

    // Triangle normals; smoothed faces average them per (vertex, brush, face), weighted by area.
    std::vector<Vec3> tri_normal(tri_count);
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, Vec3> smooth_normal;
    for (std::size_t t = 0; t < tri_count; ++t) {
        const Vec3 a = position(gl.triVerts[t * 3]), b = position(gl.triVerts[t * 3 + 1]), c = position(gl.triVerts[t * 3 + 2]);
        const Vec3 n = math::cross(b - a, c - a);  // length = 2 x area
        tri_normal[t] = math::normalize(n);
        const Brush& br = brushes[source[t].brush];
        if (smooth(br.shape, source[t].face))
            for (int k = 0; k < 3; ++k) {
                Vec3& acc = smooth_normal[{gl.triVerts[t * 3 + k], source[t].brush, source[t].face}];
                acc = acc + n;
            }
    }

    // Output vertices: one per (Manifold vertex, brush, face), so faces keep hard edges and their
    // own UVs. Triangles grouped by material.
    std::vector<std::size_t> order(tri_count);
    for (std::size_t t = 0; t < tri_count; ++t) order[t] = t;
    const auto material_of = [&](std::size_t t) { return brushes[source[t].brush].surface_of(source[t].face).material; };
    // By material, then by brush: each brush's triangles are one run per material (the editor draws
    // and picks them per brush).
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return std::pair{material_of(a), source[a].brush} < std::pair{material_of(b), source[b].brush};
    });

    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, std::uint32_t>, std::uint32_t> vertex_of;
    for (const std::size_t t : order) {
        const Model::Source src = source[t];
        const Brush& br = brushes[src.brush];
        const Surface& surface = br.surface_of(src.face);
        const bool is_smooth = smooth(br.shape, src.face);
        Frame frame = projection_frame(tri_normal[t]);
        if (surface.align == Align::Brush) {
            // The brush's axes (rotation only: a stretched brush doesn't stretch its texture).
            const math::Mat4& m = br.transform;
            const Vec3 ax = math::normalize(Vec3{m.m[0].x, m.m[0].y, m.m[0].z}), ay = math::normalize(Vec3{m.m[1].x, m.m[1].y, m.m[1].z}),
                       az = math::normalize(Vec3{m.m[2].x, m.m[2].y, m.m[2].z});
            const Vec3 local{math::dot(tri_normal[t], ax), math::dot(tri_normal[t], ay), math::dot(tri_normal[t], az)};
            const Frame f = projection_frame(local);
            frame = {ax * f.u.x + ay * f.u.y + az * f.u.z, ax * f.v.x + ay * f.v.y + az * f.v.z};
        }
        const float rad = math::radians(surface.rotation_degrees);
        const float c = std::cos(rad), sn = std::sin(rad);
        // The axis this triangle's UVs came from: part of the vertex key, so box-projected curved
        // faces get a clean seam where the axis changes instead of stretched UVs.
        const auto quant = [](float x) { return static_cast<std::uint32_t>(std::lround(x * 100.0f) + 200); };
        const std::uint32_t axis = (quant(frame.u.x) << 20) | (quant(frame.u.y) << 10) | quant(frame.u.z);
        if (out.submeshes.empty() || out.submeshes.back().material != surface.material)
            out.submeshes.push_back({surface.material, static_cast<std::uint32_t>(out.indices.size()), 0});
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t mv = gl.triVerts[t * 3 + k];
            // Flat faces share vertices between triangles in the same plane direction (the normal,
            // rounded, is part of the key); smooth faces share them across the whole face.
            const auto q = [](float x) { return static_cast<std::uint64_t>(std::lround(x * 1000.0f) + 2000); };
            const std::uint64_t flat_key = is_smooth ? 0u : (q(tri_normal[t].x) << 32) | (q(tri_normal[t].y) << 16) | q(tri_normal[t].z);
            const auto key = std::tuple{mv, src.brush * 64u + src.face, flat_key, axis};
            auto it = vertex_of.find(key);
            if (it == vertex_of.end()) {
                Vertex v;
                v.position = position(mv);
                v.normal = is_smooth ? math::normalize(smooth_normal[{mv, src.brush, src.face}]) : tri_normal[t];
                v.uv = project(v.position, frame, surface, c, sn);
                // Tangent: the direction of +u (after rotation) along the surface; w from where +v points.
                const Vec3 du = frame.u * c - frame.v * sn, dv = frame.u * sn + frame.v * c;
                Vec3 tangent = du - v.normal * math::dot(du, v.normal);
                tangent = math::dot(tangent, tangent) > 1e-12f ? math::normalize(tangent) : Vec3{1, 0, 0};
                const float w = math::dot(math::cross(v.normal, tangent), dv) < 0.0f ? -1.0f : 1.0f;
                v.tangent = {tangent.x, tangent.y, tangent.z, w};
                it = vertex_of.emplace(key, static_cast<std::uint32_t>(out.vertices.size())).first;
                out.vertices.push_back(v);
            }
            out.indices.push_back(it->second);
        }
        out.submeshes.back().index_count += 3;
        out.triangle_source.push_back(src);
    }
    return out;
}

} // namespace noor::csg
