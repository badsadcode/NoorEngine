#pragma once
// glTF 2.0 / GLB import (cgltf), including Noor Interchange data (extras.noor) on nodes.
//
// Imported files are untrusted: the file is size-limited, validated by cgltf, and every index
// is range-checked before use. Geometry is triangle lists only (other modes are skipped with a
// warning). Each primitive becomes a submesh with its glTF material (ADR-030): the base colour
// factor is baked into the vertex colours (so tools without materials still see colours); the
// material carries the textures (embedded images) and the other channels.

#include "noor/asset/json.h"
#include "noor/asset/material.h"
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace noor::asset {

// Same layout as render::Vertex, so it can be uploaded as is.
struct MeshVertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec3 color{1, 1, 1};       // linear (base colour factor x vertex colour)
    math::Vec2 uv{0, 0};             // TEXCOORD_0
    math::Vec4 tangent{1, 0, 0, 1};  // TANGENT, or computed from the UVs (w: bitangent handedness)
};

// A run of a mesh's indices drawn with one material (one glTF primitive).
struct Submesh {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material = -1;  // index into ImportedModel::materials, -1: the default material
};

// The most joints a skin may have (the renderer's palette per draw).
inline constexpr std::size_t kMaxSkinJoints = 256;

// How a vertex follows a skeleton (Milestone 8C, ADR-036): up to four joints (indices into its skin's joints) and
// their weights, which add up to 1. The strongest four of glTF's JOINTS_0/1 and WEIGHTS_0/1, renormalised.
struct SkinWeights {
    std::array<std::uint16_t, 4> joints{};
    math::Vec4 weights{1, 0, 0, 0};
};

struct ImportedMesh {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<SkinWeights> skin;       // one per vertex for a skinned mesh, else empty
    std::vector<std::uint32_t> indices;  // triangle list
    std::vector<Submesh> submeshes;      // cover all indices, in order
    [[nodiscard]] std::uint64_t triangles() const noexcept { return indices.size() / 3; }
};

struct ImportedNode {
    std::string name;
    int parent = -1;  // index into ImportedModel::nodes, -1 for roots
    math::Vec3 translation{0, 0, 0};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};
    int mesh = -1;  // index into ImportedModel::meshes
    int skin = -1;  // index into ImportedModel::skins: the mesh is skinned by it (the node's own transform is then ignored)
    Json noor;      // the node's extras.noor block (Noor Interchange), or null

    [[nodiscard]] bool is_collision() const { return noor.is_object() && noor.contains("collision"); }
    [[nodiscard]] bool is_marker() const { return noor.is_object() && noor.contains("marker"); }
};

// An image stored in the GLB (PNG or JPEG bytes), for materials' textures.
struct ImportedImage {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

struct ImportedMaterial {
    std::string name;
    // Textures point at this model's file with embedded_image set. base_color's rgb is 1: the
    // factor is in the vertex colours (alpha stays here).
    Material material;
};

// A skeleton as glTF gives it: its joints (nodes) and, for each, the matrix that takes the mesh from where it was
// bound into that joint's space. A vertex is drawn at sum(weight x world(joint) x inverse_bind(joint) x position).
struct ImportedSkin {
    std::string name;
    std::vector<int> joints;               // indices into ImportedModel::nodes
    std::vector<math::Mat4> inverse_bind;  // one per joint (identity where the file gives none)
};

struct ImportedModel {
    std::vector<ImportedNode> nodes;  // parents always come before their children
    std::vector<ImportedSkin> skins;
    std::vector<ImportedMesh> meshes;
    std::vector<ImportedMaterial> materials;
    std::vector<ImportedImage> images;
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
