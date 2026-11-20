#include "noor/asset/model_import.h"

#include "noor/asset/fbx_import.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace noor::asset {
namespace {

std::string lower_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

Result<ImportedModel> import_model(const std::filesystem::path& path, std::uint64_t max_file_bytes) {
    if (lower_extension(path) == ".fbx") return import_fbx(path, max_file_bytes);
    return import_gltf(path, max_file_bytes);
}

bool is_model_file(const std::filesystem::path& path) {
    const std::string ext = lower_extension(path);
    return ext == ".glb" || ext == ".gltf" || ext == ".fbx";
}

} // namespace noor::asset
