#include "noor/asset/registry.h"
#include "noor/asset/texture.h"

#include "noor/asset/json.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/foundation/sha256.h"

#include <algorithm>
#include <format>
#include <map>
#include <regex>
#include <set>

namespace noor::asset {
namespace {
namespace stdfs = std::filesystem;

stdfs::path meta_path_for(const stdfs::path& source) {
    stdfs::path meta = source;
    meta += kMetaExtension;
    return meta;
}

// "<dir>/<name>.glb" -> "<dir>/<name>.noor.json"
stdfs::path manifest_path_for(const stdfs::path& source) {
    return source.parent_path() / (source.stem().string() + ".noor.json");
}

Result<std::string> hash_file(const stdfs::path& path) {
    auto data = fs::read_file(path);
    if (!data) return std::unexpected(data.error());
    return Sha256::hex(*data);
}

struct MetaFile {
    stdfs::path path;
    Json json;
};

Result<MetaFile> load_meta(const stdfs::path& path) {
    auto text = fs::read_file(path, 1 << 20);
    if (!text) return std::unexpected(text.error());
    auto json = parse_json(*text, path.generic_string());
    if (!json) return std::unexpected(json.error());
    if (json->value("noor_meta", 0) <= 0 || !guid_field(*json, "guid")) {
        return fail(ErrorCode::ParseError, std::format("'{}' is not a valid .noormeta file", path.generic_string()));
    }
    if (json->value("noor_meta", 0) > kMetaFormat) {
        return fail(ErrorCode::VersionMismatch, std::format("'{}' needs a newer NoorEngine", path.generic_string()));
    }
    return MetaFile{path, std::move(*json)};
}

Status save_meta(const MetaFile& meta) {
    return fs::write_file_atomic(meta.path, to_text(meta.json), /*keep_backup=*/false);
}

// GUID from a Noor Interchange manifest next to the source, if there is a valid one.
std::optional<Guid> interchange_guid(const stdfs::path& source) {
    const stdfs::path manifest = manifest_path_for(source);
    if (!stdfs::exists(manifest)) return std::nullopt;
    auto text = fs::read_file(manifest, 4 << 20);
    if (!text) return std::nullopt;
    auto json = parse_json(*text, manifest.generic_string());
    if (!json || !json->contains("noor_interchange")) return std::nullopt;
    return guid_field(*json, "id");
}

std::string lower_extension(const stdfs::path& path) {
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool is_prefab(const stdfs::path& path) { return lower_extension(path) == ".noorprefab"; }
bool is_material(const stdfs::path& path) { return lower_extension(path) == ".noormat"; }
bool is_sculpt(const stdfs::path& path) { return lower_extension(path) == ".noorsculpt"; }  // terrain sculpt layers (noor/terrain)
bool is_paint(const stdfs::path& path) { return lower_extension(path) == ".noorpaint"; }    // terrain paint layers (noor/terrain)
bool is_graph(const stdfs::path& path) { return lower_extension(path) == ".noorgraph"; }    // procedural graphs (noor/procgraph)

// Formats miniaudio decodes out of the box (ADR-011). Ogg Vorbis would need stb_vorbis: later.
bool is_audio(const stdfs::path& path) {
    const std::string ext = lower_extension(path);
    return ext == ".wav" || ext == ".flac" || ext == ".mp3";
}

// A prefab file carries its own id (ADR-024); the meta uses it so the two always agree.
std::optional<Guid> prefab_guid(const stdfs::path& source) {
    auto text = fs::read_file(source, 64ull << 20);
    if (!text) return std::nullopt;
    auto json = parse_json(*text, source.generic_string());
    if (!json || !json->contains("noor_prefab")) return std::nullopt;
    return guid_field(*json, "id");
}

} // namespace

bool AssetRegistry::is_importable(const stdfs::path& path) {
    if (is_prefab(path) || is_audio(path) || is_texture_file(path) || is_material(path) || is_sculpt(path) || is_paint(path) || is_graph(path)) return true;
    const std::string ext = lower_extension(path);
    if (ext != ".glb" && ext != ".gltf") return false;
    static const std::regex lod(R"(\.lod[0-9]+$)", std::regex::icase);
    return !std::regex_search(path.stem().string(), lod);
}

Result<ScanReport> AssetRegistry::scan(const Project& project) {
    ScanReport report;
    assets_.clear();
    by_guid_.clear();

    const stdfs::path root = project.assets_dir();
    std::error_code ec;
    if (!stdfs::is_directory(root, ec)) {
        return fail(ErrorCode::NotFound, std::format("Project has no Assets folder: {}", root.generic_string()));
    }

    // 1. Inventory (sorted, so results don't depend on directory iteration order).
    std::set<stdfs::path> sources;
    std::set<stdfs::path> metas;
    for (auto it = stdfs::recursive_directory_iterator(root, ec); !ec && it != stdfs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const stdfs::path& p = it->path();
        if (p.extension() == kMetaExtension) {
            metas.insert(p);
        } else if (is_importable(p)) {
            sources.insert(p);
        }
    }
    if (ec) {
        return fail(ErrorCode::IoError, std::format("Cannot scan '{}': {}", root.generic_string(), ec.message()));
    }
    report.assets = static_cast<int>(sources.size());

    // 2. Orphans: metas without their source. Candidates for moved files.
    std::map<stdfs::path, MetaFile> orphans;
    for (const auto& meta : metas) {
        stdfs::path source = meta;
        source.replace_extension();  // strip ".noormeta"
        if (!sources.contains(source)) {
            auto loaded = load_meta(meta);
            if (loaded) {
                orphans.emplace(meta, std::move(*loaded));
            } else {
                report.warnings.push_back(loaded.error().describe());
            }
        }
    }

    std::set<Guid> used;
    for (const auto& source : sources) {
        auto rel = project.relative(source);
        auto sha = hash_file(source);
        if (!rel || !sha) {
            report.warnings.push_back((!rel ? rel.error() : sha.error()).describe());
            continue;
        }

        MetaFile meta;
        meta.path = meta_path_for(source);
        bool write = false;

        if (stdfs::exists(meta.path)) {
            auto loaded = load_meta(meta.path);
            if (!loaded) {
                report.warnings.push_back(loaded.error().describe() + " (left untouched; asset skipped)");
                continue;
            }
            meta = std::move(*loaded);
        } else {
            // A moved/renamed file? Its old meta is an orphan with the same content hash.
            std::vector<stdfs::path> matches;
            for (const auto& [orphan_path, orphan] : orphans) {
                if (orphan.json.value("source_sha256", std::string{}) == *sha) matches.push_back(orphan_path);
            }
            if (matches.size() == 1) {
                meta = std::move(orphans.at(matches.front()));
                orphans.erase(matches.front());
                stdfs::rename(meta.path, meta_path_for(source), ec);
                if (ec) {
                    report.warnings.push_back(std::format("Cannot move meta to '{}': {}", *rel, ec.message()));
                    continue;
                }
                NOOR_LOG_INFO("Asset", "Re-attached moved asset {} -> {}", meta.path.generic_string(), *rel);
                meta.path = meta_path_for(source);
                ++report.moved;
                write = true;
            } else {
                if (matches.size() > 1) {
                    report.warnings.push_back(std::format(
                        "'{}' matches {} orphaned metas with identical content; not guessing, giving it a new id",
                        *rel, matches.size()));
                }
                const bool prefab = is_prefab(source);
                const bool audio = is_audio(source);
                const bool texture = is_texture_file(source);
                const bool material = is_material(source);
                const bool sculpt = is_sculpt(source);
                const bool paint = is_paint(source);
                const bool graph = is_graph(source);
                const auto from_manifest = prefab || audio || texture || material || sculpt || paint || graph ? std::nullopt : interchange_guid(source);
                const auto embedded = prefab ? prefab_guid(source) : from_manifest;
                meta.json = Json::object();
                meta.json["noor_meta"] = kMetaFormat;
                meta.json["guid"] = (embedded ? *embedded : Guid::generate()).to_string();
                meta.json["importer"] = prefab ? "prefab" : audio ? "audio" : texture ? "texture" : material ? "material" : sculpt ? "terrain_sculpt" : paint ? "terrain_paint" : graph ? "procgraph" : "gltf";
                meta.json["source_sha256"] = *sha;
                if (from_manifest) meta.json["interchange_manifest"] = manifest_path_for(source).filename().generic_string();
                // Textures: the first guess at their settings, from the name (ADR-029); edit them later.
                meta.json["settings"] = texture ? guess_texture_settings(source).to_json() : Json::object();
                ++report.created;
                write = true;
            }
        }

        if (meta.json.value("source_sha256", std::string{}) != *sha) {
            meta.json["source_sha256"] = *sha;
            ++report.content_changed;
            write = true;
        }

        Guid guid = *guid_field(meta.json, "guid");
        if (used.contains(guid)) {
            // Usually a copied file+meta pair. The first keeps the id; the copy gets a new one.
            const Guid fresh = Guid::generate();
            report.warnings.push_back(std::format("'{}' had duplicate id {}; assigned {}", *rel, guid.to_string(),
                                                  fresh.to_string()));
            guid = fresh;
            meta.json["guid"] = guid.to_string();
            meta.json.erase("interchange_manifest");
            write = true;
        }
        used.insert(guid);

        if (write) {
            if (auto s = save_meta(meta); !s) return std::unexpected(s.error());
        }

        by_guid_[guid] = assets_.size();
        assets_.push_back({guid, *rel, meta.json.value("importer", std::string("gltf")), *sha,
                           meta.json.contains("interchange_manifest")});
    }

    for (const auto& [path, orphan] : orphans) {
        auto rel = project.relative(path);
        report.orphaned_metas.push_back(rel ? *rel : path.generic_string());
    }
    NOOR_LOG_INFO("Asset", "Scan: {} assets, {} new, {} moved, {} changed, {} orphaned meta(s), {} warning(s)",
                  report.assets, report.created, report.moved, report.content_changed, report.orphaned_metas.size(),
                  report.warnings.size());
    for (const auto& w : report.warnings) NOOR_LOG_WARN("Asset", "{}", w);
    return report;
}

const AssetRecord* AssetRegistry::find(const Guid& guid) const {
    const auto it = by_guid_.find(guid);
    return it == by_guid_.end() ? nullptr : &assets_[it->second];
}

const AssetRecord* AssetRegistry::find_path(std::string_view project_relative) const {
    for (const auto& a : assets_) {
        if (a.path == project_relative) return &a;
    }
    return nullptr;
}

Status AssetRegistry::move_asset(const Project& project, std::string_view from, std::string_view to) {
    auto src = project.resolve(from);
    auto dst = project.resolve(to);
    if (!src) return std::unexpected(src.error());
    if (!dst) return std::unexpected(dst.error());
    std::error_code ec;
    if (!stdfs::exists(*src)) return fail(ErrorCode::NotFound, std::format("No asset at '{}'", from));
    if (stdfs::exists(*dst)) return fail(ErrorCode::AlreadyExists, std::format("'{}' already exists", to));
    stdfs::create_directories(dst->parent_path(), ec);
    stdfs::rename(*src, *dst, ec);
    if (ec) return fail(ErrorCode::IoError, std::format("Cannot move '{}' to '{}': {}", from, to, ec.message()));
    if (stdfs::exists(meta_path_for(*src))) {
        stdfs::rename(meta_path_for(*src), meta_path_for(*dst), ec);
        if (ec) return fail(ErrorCode::IoError, std::format("Moved '{}' but not its meta: {}", from, ec.message()));
    }
    auto rescanned = scan(project);
    if (!rescanned) return std::unexpected(rescanned.error());
    return {};
}

} // namespace noor::asset
