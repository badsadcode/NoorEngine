#pragma once
// Mesh helpers shared by the model importers (glTF, FBX): internal to noor_asset.

#include "noor/asset/gltf_import.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace noor::asset {

// Tangents from the UVs for vertices [first_vertex, end) used by the triangles from first_index on (gltf_import.cpp).
void compute_tangents(std::vector<MeshVertex>& vertices, const std::vector<std::uint32_t>& indices, std::size_t first_vertex,
                      std::size_t first_index);

} // namespace noor::asset
