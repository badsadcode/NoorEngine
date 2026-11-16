#pragma once
// The game module: what it takes to run a NoorEngine game, shared by the editor's Play and by
// noor_player, so a game behaves the same in both (ADR-028).
//
//   make_world      a runtime::World for a scene, with every engine system a game uses
//   collect_view    what a running scene shows: models, primitive shapes, the game camera
//   SceneRenderer   draws a SceneView with the renderer (noor_player; the editor viewport has its own
//                   drawing with picking and gizmos)

#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"
#include "noor/runtime/world.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace noor::game {

struct WorldOptions {
    const void* script_host = nullptr;  // noor_rt_host* for C# scripts; without one they report it
    bool audio_device = true;           // false: sounds are mixed silently (tests)
};

// The playing world for `scene` (prefabs are expanded here): Lua and C# scripts, physics, audio and
// the built-in behaviours, in that order, with the project's Settings/input.json.
[[nodiscard]] Result<std::unique_ptr<runtime::World>> make_world(const asset::Project& project,
                                                                 const asset::AssetRegistry& registry,
                                                                 scene::PrefabLibrary& prefabs,
                                                                 const scene::Scene& scene,
                                                                 const WorldOptions& options = {});

// What a scene shows, with world matrices resolved.
struct SceneView {
    struct Mesh {
        std::filesystem::path asset_path;  // the GLB
        int mesh_index = -1;
        math::Mat4 world;
        Guid entity;
    };
    // Simple shapes from `primitive` components (sizes baked into `world`, unit shapes below).
    struct Primitive {
        enum class Shape : std::uint8_t { Box, Sphere, Capsule } shape;
        math::Mat4 world;  // box: +-0.5 cube scaled to size; sphere: radius 0.5 scaled; capsule: unscaled
        float radius = 0.5f, height = 2.0f;  // capsule only
        math::Vec3 color;  // linear
        Guid entity;
    };
    // The first entity with a `camera` component ({"fov": 70, "near": 0.05}).
    struct Camera {
        math::Mat4 world;
        float fov_degrees = 70.0f;
        float near_plane = 0.05f;
        Guid entity;
    };
    std::vector<Mesh> meshes;
    std::vector<Primitive> primitives;
    std::optional<Camera> camera;
};

// Finds a model asset's file from the `mesh` component's asset id.
using AssetPath = std::function<std::optional<std::filesystem::path>(const Guid& asset)>;

// `scene` must be expanded (no prefab instances left), e.g. World::scene().
[[nodiscard]] SceneView collect_view(const scene::Scene& scene, const AssetPath& asset_path);

// An AssetPath that looks ids up in a registry.
[[nodiscard]] AssetPath registry_asset_path(const asset::Project& project, const asset::AssetRegistry& registry);

// The view matrix for a camera entity's world matrix (looks down its −Z, keeps the horizon level).
[[nodiscard]] math::Mat4 camera_view(const math::Mat4& camera_world);

} // namespace noor::game
