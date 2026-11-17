#pragma once
// CSG brushes (ADR-016, Milestone 6): shapes with exact parameters, combined in order by
// union / subtract / intersect into one model mesh. Every output triangle knows the brush face it
// came from, so materials and UVs follow faces through cuts: a doorway cut into a brick wall keeps
// the wall's bricks, and the new faces (the jambs) take the cutting brush's settings.
//
// UVs are projected from the model's space (box projection on the face's main axis), so they line
// up across a cut and don't move when the model moves. Everything here is CPU-side and runs in the
// editor and when cooking; games load the cooked result (noor_csg is never linked into them).
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace noor::csg {

enum class Shape : std::uint8_t { Box, Wedge, Cylinder, Cone, Sphere, Stairs, Arch };
enum class Operation : std::uint8_t { Add, Subtract, Intersect };

[[nodiscard]] std::string_view shape_name(Shape shape) noexcept;
[[nodiscard]] std::string_view operation_name(Operation op) noexcept;

// How one face is surfaced: a material slot of the model and its UV mapping. UVs come from the
// position (metres) on the two axes across the face's main direction:
// uv = rotate(position, rotation) * scale + offset, so scale is "texture repeats per metre".
// Align::World projects along the model's axes (textures stay put when brushes move: cuts line up
// with their surroundings); Align::Brush along the brush's own axes (textures turn with a rotated
// brush: a wall at 37 degrees keeps straight rows of bricks).
enum class Align : std::uint8_t { World, Brush };
struct Surface {
    std::uint32_t material = 0;
    Align align = Align::World;
    math::Vec2 scale{1.0f, 1.0f};
    math::Vec2 offset{0.0f, 0.0f};
    float rotation_degrees = 0.0f;
    friend bool operator==(const Surface&, const Surface&) = default;
};

// One brush. `size` is the full extent of the shape's box before the transform (centred on the
// origin). Logical faces per shape (face_count, face_name):
//   Box      6: +X, -X, +Y (top), -Y (bottom), +Z (front), -Z (back)
//   Wedge    5: slope, bottom, back (-Z), +X side, -X side      (high at the back, low at the front)
//   Cylinder 3: side, top, bottom          (axis Y, `sides` segments)
//   Cone     2: side, bottom               (apex up)
//   Sphere   1: surface                    (`sides` around, sides / 2 rings; an ellipsoid when scaled)
//   Stairs   5: treads, risers, sides, back, bottom   (`steps` steps rising towards -Z)
//   Arch     4: front (+Z), back (-Z), straight sides and bottom, curved top   (a doorway shape:
//              a rectangle with a half circle on top, `sides` segments in the curve)
struct Brush {
    Shape shape = Shape::Box;
    Operation operation = Operation::Add;
    math::Vec3 size{1.0f, 1.0f, 1.0f};
    std::uint32_t sides = 16;
    std::uint32_t steps = 4;
    math::Mat4 transform = math::Mat4::identity();  // brush -> model space
    Surface surface;                  // every face, unless...
    std::vector<Surface> faces;       // ...a face has its own (index = logical face; may be shorter)

    [[nodiscard]] const Surface& surface_of(std::uint32_t face) const noexcept {
        return face < faces.size() ? faces[face] : surface;
    }
};

[[nodiscard]] std::uint32_t face_count(Shape shape) noexcept;
[[nodiscard]] std::string_view face_name(Shape shape, std::uint32_t face) noexcept;

// A brush's closed triangle mesh in model space, before any boolean (shared vertices, triangles
// tagged with their logical face). Exposed for tests and for drawing brush outlines.
struct BrushMesh {
    std::vector<math::Vec3> positions;
    std::vector<std::uint32_t> indices;      // 3 per triangle, counter-clockwise seen from outside
    std::vector<std::uint32_t> triangle_face;  // logical face per triangle
};
[[nodiscard]] BrushMesh brush_mesh(const Brush& brush);

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec2 uv;
    math::Vec4 tangent;  // xyz along +u, w = handedness (like glTF)
};

// The combined model: triangles grouped by material slot, and within a slot by brush.
struct Model {
    struct Submesh {
        std::uint32_t material = 0;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
    };
    struct Source {
        std::uint32_t brush = 0;
        std::uint32_t face = 0;
    };
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Submesh> submeshes;
    std::vector<Source> triangle_source;  // per triangle (in index order): where it came from
    std::vector<std::string> warnings;    // brushes skipped, degenerate input...
    double volume = 0.0;                  // cubic metres, from the boolean result

    [[nodiscard]] std::size_t triangle_count() const noexcept { return indices.size() / 3; }
};

// Combines the brushes in order: the first Add starts the solid, each next brush is added,
// subtracted or intersected with the result so far. An empty result is valid (zero triangles).
[[nodiscard]] Result<Model> build_model(std::span<const Brush> brushes);

} // namespace noor::csg
