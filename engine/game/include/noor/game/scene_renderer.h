#pragma once
// Draws a SceneView: loads each model the first time it's seen and keeps the built-in shapes.

#include "noor/game/game.h"
#include "noor/game/geometry.h"
#include "noor/game/environment_library.h"
#include "noor/game/material_library.h"
#include "noor/game/model_lod.h"
#include "noor/game/particle_draws.h"
#include "noor/game/texture_library.h"
#include "noor/render/renderer.h"

#include <map>
#include <span>
#include <string>
#include <unordered_map>

namespace noor::fx {
class ParticleSystem;
}
namespace noor::runtime {
class World;
}
#include <vector>

namespace noor::game {

class SceneRenderer {
public:
    // `texture_cache`: where decoded textures are kept (normally <project>/.noor/cache/textures);
    // `resolve`: asset ids to files (materials' textures and parents).
    explicit SceneRenderer(render::Renderer& renderer, std::filesystem::path texture_cache = {}, asset::AssetFileResolver resolve = {});
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // The draw items for `view`. Models that fail to load are reported once and skipped.
    [[nodiscard]] std::vector<render::DrawItem> draws(const SceneView& view);

    // The effect particles of a running world (Milestone 8B, part 2), one draw per emitter: its `render` settings
    // (blend, facing, lit, emission, stretch, texture and flipbook), particles in world space with their size, colour
    // and alpha from their life's curves, alpha-blended ones sorted back to front for `eye`. Emitters in entity id
    // order, so a frame doesn't depend on a hash map's order. The spans stay valid until the next call.
    [[nodiscard]] std::span<const render::ParticleDraw> particles(const fx::ParticleSystem& system, runtime::World& world, math::Vec3 eye);

    // The game camera, or (with none) one that looks at the origin from above and behind.
    [[nodiscard]] static render::Camera camera(const SceneView& view);

    // A primitive's material: its .noormat, or its own fields.
    [[nodiscard]] static render::MaterialHandle material(const SceneView::Primitive& p, MaterialLibrary& materials);

    // Reloads changed materials and textures (call a few times a second).
    void refresh() { materials_.refresh(); }

    // The environment (sky, image-based lighting) it sets each frame.
    [[nodiscard]] EnvironmentLibrary& environment() noexcept { return environment_; }
    // Where cooked CSG models are (the project or game folder; files in .noor/cooked/csg).
    void set_cooked_root(std::filesystem::path root) { cooked_root_ = std::move(root); }

private:
    render::MeshHandle upload(const Geometry& g, const char* name);

    render::Renderer& renderer_;
    render::MeshHandle box_, sphere_;
    std::map<std::pair<int, int>, render::MeshHandle> capsules_;  // by (radius, height) in mm
    struct Model {
        std::vector<render::MeshHandle> meshes;
        std::vector<std::vector<asset::Submesh>> submeshes;  // per mesh
        std::vector<render::MaterialHandle> materials;
        std::vector<std::pair<int, math::Mat4>> nodes;  // drawn nodes (mesh, world in the file): for a level of detail
    };
    std::unordered_map<std::string, std::optional<Model>> models_;  // nullopt: failed
    std::unordered_map<std::string, ModelLods> lods_;               // by model file
    const Model* model(const std::filesystem::path& file);
    void add_mesh(std::vector<render::DrawItem>& out, const Model& model, int mesh_index, const math::Mat4& world) const;
    const ModelLods& lods_for(const std::filesystem::path& file);
    TextureLibrary textures_;
    MaterialLibrary materials_;
    EnvironmentLibrary environment_;
    // Declared after the libraries: members are built in this order, and materials_ needs `resolve` before it moves here.
    struct CookedModel {
        bool loaded = false;
        std::uint64_t key = 0;
        std::filesystem::file_time_type time{};
        render::MeshHandle mesh;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;  // (first, count) per submesh
        std::vector<render::MaterialHandle> materials;               // per submesh
        bool warned = false;
    };
    std::unordered_map<Guid, CookedModel> cooked_;
    ParticleDrawStorage particle_storage_;  // this frame's particle draws (particle_draws.h)
    struct TerrainMeshes {
        std::string key;  // the settings it was built from
        std::vector<render::MeshHandle> tiles;
        bool warned = false;
    };
    std::unordered_map<Guid, TerrainMeshes> terrains_;
    std::filesystem::path cooked_root_;
    asset::AssetFileResolver resolve_;
};

} // namespace noor::game
