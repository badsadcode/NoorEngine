#pragma once
// Writing models as Noor Interchange assets (docs/interchange/NOOR_INTERCHANGE.md, v0.3): a GLB
// (meshes, PBR materials, textures embedded, a collision node) and a `<name>.noor.json` manifest.
// First user: CSG models exported from the editor (Milestone 6, part 4).
#include "noor/asset/gltf_import.h"
#include "noor/asset/json.h"
#include "noor/asset/material.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"

#include <filesystem>
#include <string>
#include <vector>

namespace noor::asset {

struct ExportModel {
    std::string name;           // node and manifest name (readable, stable)
    Guid id;                    // manifest id: stays the same when the asset is exported again
    std::string kind = "level"; // manifest kind (spec section 4)
    std::string generator_version;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    struct Part {
        std::uint32_t first_index = 0, index_count = 0;
        std::filesystem::path material;  // a .noormat; empty: a plain default material
    };
    std::vector<Part> parts;
    bool collision = true;  // also write a collision node (`<name>-colonly`, extras.noor.collision = "mesh")
};

// Writes `glb` and the manifest beside it (same base name + ".noor.json"). Material texture maps are
// embedded when they are PNG or JPEG files; each material's UV scale and offset are baked into the
// UVs. What glTF can't hold (single-channel roughness/metallic maps, other image types) is left out
// and listed in `warnings`. Returns the manifest.
[[nodiscard]] Result<Json> export_noor_glb(const ExportModel& model, const AssetFileResolver& resolve, const std::filesystem::path& glb,
                                           std::vector<std::string>* warnings = nullptr);

} // namespace noor::asset
