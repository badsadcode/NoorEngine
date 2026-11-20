#pragma once
// A model file of any format the engine reads, by its extension: .glb and .gltf (glTF), .fbx (FBX, Milestone 8C part 7).
// Everything that loads a model asset goes through this, so a format added here works everywhere.

#include "noor/asset/gltf_import.h"

#include <filesystem>
#include <string_view>

namespace noor::asset {

[[nodiscard]] Result<ImportedModel> import_model(const std::filesystem::path& path, std::uint64_t max_file_bytes = 512ull << 20);

// Whether `path` names a model file import_model reads (by extension, case-insensitive).
[[nodiscard]] bool is_model_file(const std::filesystem::path& path);

} // namespace noor::asset
