#pragma once
// Asset registry (ADR-006): finds importable files under Assets/, gives each a stable GUID in a
// <file>.noormeta sidecar, and keeps that GUID when files are renamed or moved, even outside
// the editor (by matching file content hashes).

#include "noor/asset/project.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace noor::asset {

inline constexpr int kMetaFormat = 1;
inline constexpr std::string_view kMetaExtension = ".noormeta";

struct AssetRecord {
    Guid guid;
    std::string path;           // project-relative, '/' separators
    std::string importer;       // "gltf", "audio", "prefab", "texture"
    std::string source_sha256;  // content hash at last scan
    bool from_interchange = false;  // GUID came from a Noor Interchange manifest
};

struct ScanReport {
    int assets = 0;           // importable files found
    int created = 0;          // new metas written
    int moved = 0;            // metas re-attached to a moved/renamed file
    int content_changed = 0;  // sources whose content changed since the last scan
    std::vector<std::string> orphaned_metas;  // metas whose source is gone (kept, never deleted)
    std::vector<std::string> warnings;
};

class AssetRegistry {
public:
    // Scans Assets/ and brings every .noormeta up to date. Safe to run any number of times.
    [[nodiscard]] Result<ScanReport> scan(const Project& project);

    [[nodiscard]] const AssetRecord* find(const Guid& guid) const;
    [[nodiscard]] const AssetRecord* find_path(std::string_view project_relative) const;
    [[nodiscard]] const std::vector<AssetRecord>& assets() const noexcept { return assets_; }

    // Moves/renames an asset and its meta together (the in-editor way). Both paths project-relative.
    [[nodiscard]] Status move_asset(const Project& project, std::string_view from, std::string_view to);

    // True for files the registry manages: models (".glb", ".gltf"; not "<name>.lod<n>.glb", which
    // belong to their LOD0 file per Noor Interchange 3.5), sounds, prefabs and textures.
    [[nodiscard]] static bool is_importable(const std::filesystem::path& path);

private:
    std::vector<AssetRecord> assets_;
    std::unordered_map<Guid, std::size_t> by_guid_;
};

} // namespace noor::asset
