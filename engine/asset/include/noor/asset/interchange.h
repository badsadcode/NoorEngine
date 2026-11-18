#pragma once
// Reading Noor Interchange manifests (docs/interchange/NOOR_INTERCHANGE.md, v0.4): the
// `<name>.noor.json` next to a model, which says what the asset is, where it came from (generator,
// recipe, seed), its levels of detail (`<name>.lod<n>.glb`, with the distances they're for), its
// collision, family and wind data.
//
// Manifests are untrusted input like the GLBs: paths in them must stay inside the asset's folder,
// and every problem is reported as an issue on a field rather than a crash or a silent default.
// The engine keeps everything it reads (and the raw JSON); it uses the levels and distances now,
// and keeps wind data for later (Milestone 7, part 5).

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace noor::asset {

// The newest Noor Interchange version this engine knows. Newer minor versions still load (their
// fields are optional by the spec's rules); a newer major version doesn't.
inline constexpr int kInterchangeMajor = 0;
inline constexpr int kInterchangeMinor = 4;

struct InterchangeFile {
    std::string path;    // relative to the manifest's folder, '/' separators
    std::string role;    // "model", "lod", "recipe", "preview", "validation", ...
    int lod = -1;        // for role "lod"
    std::string sha256;  // empty if the manifest doesn't say
};

struct InterchangeLevel {
    int level = 0;
    std::string file;                   // from the files list (LOD0: the model)
    std::uint64_t triangles = 0;        // as the manifest says
    std::optional<double> max_distance; // metres, for a 60-degree vertical field of view; none: no limit
    bool billboard = false;
};

struct InterchangeManifest {
    std::string version;  // "0.4"
    Guid id;
    std::string name;
    std::string kind;     // "vegetation.tree", "rock", "building", ...
    struct Generator {
        std::string tool, version, commit;
        std::string determinism = "same_platform";
    } generator;
    struct Recipe {
        std::string file;
        std::optional<std::uint64_t> seed;  // unsigned 64-bit, never parsed as a float
        std::string sha256;
    } recipe;
    std::string created;
    std::optional<math::Vec3> bounds_min, bounds_max;
    std::vector<InterchangeFile> files;
    std::vector<InterchangeLevel> lods;  // sorted by level
    std::string collision;               // "capsule", "mesh", "convex", "none", ... ("" if not said)
    struct Family {
        std::string name, variant;
        int count = 0;
    };
    std::optional<Family> family;
    Json wind;        // kept as written (v0.4); not used yet
    std::vector<std::string> tags;
    struct License {
        std::string spdx, author, attribution;
    } license;
    Json validation;  // as written
    Json provenance;  // as written
    std::vector<std::string> tool_blocks;  // names of tool-specific blocks ("noorform", "badbuilding", ...)
    Json raw;         // the whole manifest
};

struct ManifestIssue {
    std::string field;    // "recipe.seed", "files[2]", ...
    std::string message;  // plain words
    bool error = false;   // false: a warning (the asset still imports)
};

struct ManifestRead {
    InterchangeManifest manifest;
    std::vector<ManifestIssue> issues;
    [[nodiscard]] bool has_errors() const;
};

// The manifest for a model: "<dir>/<name>.glb" -> "<dir>/<name>.noor.json".
[[nodiscard]] std::filesystem::path manifest_path_for_model(const std::filesystem::path& model);

// Reads and checks a manifest: required fields, the seed as an unsigned integer, the space (+Y up,
// right-handed, metres), files present (and their SHA-256 when given), paths that stay in the
// folder, levels in order with growing distances. Fails only when the file can't be read or isn't
// a Noor Interchange manifest at all; everything else is an issue.
[[nodiscard]] Result<ManifestRead> read_manifest(const std::filesystem::path& path);

// One level's file, opened: what's really in it, compared with the manifest.
struct LevelCheck {
    int level = 0;
    std::string file;
    bool exists = false;
    std::uint64_t bytes = 0;
    std::uint64_t triangles = 0;            // rendered triangles (collision excluded)
    std::uint64_t manifest_triangles = 0;
    std::optional<double> max_distance;     // the manifest's
    bool billboard = false;
    int materials = 0;
    int images = 0;
    int collision_nodes = 0;
    std::vector<std::string> parts;         // extras.noor.part values found, in node order, unique
    std::vector<std::string> issues;        // plain words; empty when the file agrees with the manifest
};

// Opens every level listed in the manifest (LOD0 = the model file) and compares it with the
// manifest: the file is there and imports, the triangle count matches, the level's root node says
// the same level, distance and billboard flag (extras.noor), collision only where it belongs.
[[nodiscard]] std::vector<LevelCheck> check_levels(const ManifestRead& read, const std::filesystem::path& folder);

// As JSON, for the editor and tools: the manifest's fields ("id", "name", "kind", "generator", "recipe"
// with "seed" as an unsigned integer, "created", "bounds", "lods", "collision", "family", "wind",
// "tags", "license", "validation", "provenance", "tool_blocks", "version") and "issues":
// [{"field", "message", "error"}]. Levels: [{"level", "file", "exists", "bytes", "triangles",
// "manifest_triangles", "max_distance" (null: no limit), "billboard", "materials", "images",
// "collision_nodes", "parts", "issues"}].
[[nodiscard]] Json to_json(const ManifestRead& read);
[[nodiscard]] Json to_json(const std::vector<LevelCheck>& levels);

} // namespace noor::asset
