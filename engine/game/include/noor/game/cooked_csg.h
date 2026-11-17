#pragma once
// Cooked CSG models (ADR-016, Milestone 6): what games load instead of brushes. The editor (on
// save and Play) and `noor_cli build` / `noor_cli cook` run the CSG kernel and write one file per
// model; noor_player and Play only read it. The kernel itself is never linked into games.
//
// Files: <project>/.noor/cooked/csg/<model entity id>.ncsg (derived data: safe to delete, rebuilt
// from the scene). Each carries the key of the brushes it was made from, so a stale file is noticed.
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/render/renderer.h"
#include "noor/scene/scene.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace noor::game {

struct CookedCsg {
    std::uint64_t key = 0;  // csg_model_key of the brushes it was cooked from
    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    struct Submesh {
        std::uint32_t first_index = 0, index_count = 0;
        std::optional<Guid> material;  // a .noormat asset; none: the default material
    };
    std::vector<Submesh> submeshes;
};

// The brushes of the CSG model at `model_index`: its direct children with a `brush` component, in order.
[[nodiscard]] std::vector<std::size_t> csg_brush_children(const scene::Scene& scene, std::size_t model_index);

// A fingerprint of everything a model's geometry depends on (the brushes' components and local
// transforms, in order). Same scene data, same key, on any machine (FNV-1a over a text recipe).
[[nodiscard]] std::uint64_t csg_model_key(const scene::Scene& scene, std::size_t model_index);

[[nodiscard]] std::filesystem::path cooked_csg_path(const std::filesystem::path& project_root, const Guid& model);

[[nodiscard]] Status write_cooked_csg(const std::filesystem::path& file, const CookedCsg& cooked);
[[nodiscard]] Result<CookedCsg> read_cooked_csg(const std::filesystem::path& file);

} // namespace noor::game
