#pragma once
// glTF 2.0 / GLB import (cgltf), including Noor Interchange data (extras.noor) on nodes.
//
// Imported files are untrusted: the file is size-limited, validated by cgltf, and every index
// is range-checked before use. Geometry is triangle lists only (other modes are skipped with a
// warning). Materials contribute their base color (factor × vertex color); textures come later.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace noor::asset {

// Same layout as render::Vertex (position, normal, linear color), so it can be uploaded as is.
struct MeshVertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec3 color;
};

struct ImportedMesh {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;  // triangle list
    [[nodiscard]] std::uint64_t triangles() const noexcept { return indices.size() / 3; }
};

struct ImportedNode {
    std::string name;
    int parent = -1;  // index into ImportedModel::nodes, -1 for roots
    math::Vec3 translation{0, 0, 0};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};
    int mesh = -1;  // index into ImportedModel::meshes
    Json noor;      // the node's extras.noor block (Noor Interchange), or null

    [[nodiscard]] bool is_collision() const { return noor.is_object() && noor.contains("collision"); }
    [[nodiscard]] bool is_marker() const { return noor.is_object() && noor.contains("marker"); }
};

struct ImportedModel {
    std::vector<ImportedNode> nodes;  // parents always come before their children
    std::vector<ImportedMesh> meshes;
    std::vector<std::string> warnings;

    // World matrix of node `i` (parents applied).
    [[nodiscard]] math::Mat4 world_matrix(std::size_t i) const;
    // Triangles that would be rendered (excludes collision-only nodes) and collision triangles.
    [[nodiscard]] std::uint64_t render_triangles() const;
    [[nodiscard]] std::uint64_t collision_triangles() const;
};

[[nodiscard]] Result<ImportedModel> import_gltf(const std::filesystem::path& path,
                                                std::uint64_t max_file_bytes = 512ull << 20);

} // namespace noor::asset
