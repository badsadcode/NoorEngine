#pragma once
// The game module: what it takes to run a NoorEngine game, shared by the editor's Play and by
// noor_player, so a game behaves the same in both (ADR-028).
//
//   make_world      a runtime::World for a scene, with every engine system a game uses
//   collect_view    what a running scene shows: models, primitive shapes, the game camera, lighting
//   SceneRenderer   draws a SceneView with the renderer (noor_player; the editor viewport has its own
//                   drawing with picking and gizmos)

#include "noor/asset/material.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"
#include "noor/render/renderer.h"
#include "noor/terrain/terrain.h"
#include "noor/runtime/world.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
    // Their surface: "material": <.noormat asset id>, or material fields right on the component
    // (the .noormat names: "base_color_map", "normal_map", "roughness", "metallic", "uv_scale", …).
    // "color" tints either (with a material file, only when it is given).
    struct Primitive {
        enum class Shape : std::uint8_t { Box, Sphere, Capsule } shape;
        math::Mat4 world;  // box: +-0.5 cube scaled to size; sphere: radius 0.5 scaled; capsule: unscaled
        float radius = 0.5f, height = 2.0f;  // capsule only
        math::Vec3 color;  // linear
        Guid entity;
        std::filesystem::path material_file;  // the `material` asset's file, if any
        asset::Material material;             // otherwise: the component's own fields
    };
    // The first entity with a `camera` component ({"fov": 70, "near": 0.05}).
    struct Camera {
        math::Mat4 world;
        float fov_degrees = 70.0f;
        float near_plane = 0.05f;
        Guid entity;
    };
    std::vector<Mesh> meshes;
    // CSG models (`csg_model` entities): drawn from their cooked files (noor/game/cooked_csg.h).
    struct Csg {
        Guid entity;
        math::Mat4 world;
        std::uint64_t key = 0;  // what the scene says the model is now (a different cooked key: stale)
    };
    std::vector<Csg> csg;
    // Terrains (`terrain` components, noor/terrain/terrain.h): generated from their settings.
    struct Terrain {
        Guid entity;
        math::Vec3 position;  // the terrain's centre (rotation and scale are ignored)
        terrain::Settings settings;
        std::filesystem::path material_file;  // empty: the default material
    };
    std::vector<Terrain> terrains;
    std::vector<Primitive> primitives;
    std::optional<Camera> camera;
    // The sun: the first entity with a directional `light` ({"type": "directional", "color":
    // "#FFF4E5", "illuminance": 100000}), shining along the entity's forward (−Z). Until
    // environments (Milestone 5, part 5) it also carries its sky: "sky_color", "sky_luminance" (nits).
    // Exposure and
    // tonemapping from the first `camera` ({"ev100": 15, "tonemapper": "neutral" | "aces" | "none"}).
    // Without them: render::Lighting's defaults (a clear day). The sun's "shadows" (true) and
    // "shadow_distance" (80 m) set its shadows.
    // Point and spot lights: {"type": "point" | "spot", "color": "#FFD9A0", "intensity": 800 (lumens),
    // "range": 10 (m), "angle": 30 (spot: half-angle of the cone, degrees), "inner_angle": 22}.
    // A spot shines along its entity's forward (−Z).
    render::Lighting lighting;
    bool has_sun = false;  // false: the default sun is used
    // Where lights are (the editor draws markers there).
    struct LightMarker {
        math::Vec3 position;
        math::Vec3 color;
        Guid entity;
    };
    std::vector<LightMarker> light_markers;

    // The environment that lights the scene from all around and shows as the sky: the first entity
    // with an `environment` component ({"hdri": <.hdr asset id>, "intensity": 8000 (nits per HDRI
    // unit), "rotation": 0 (degrees), "background": true}); without one, a procedural sky made from
    // the sun light's sky_color / sky_luminance (or the defaults).
    struct Environment {
        std::filesystem::path hdri;   // empty: procedural
        math::Vec3 zenith, horizon, ground;  // procedural sky colours (relative)
    };
    Environment environment;
};

// Finds a model asset's file from the `mesh` component's asset id.
using AssetPath = std::function<std::optional<std::filesystem::path>(const Guid& asset)>;

// `scene` must be expanded (no prefab instances left), e.g. World::scene(). `render_poses`
// (World::render_poses()) replaces the world matrices of entities physics moves with where to draw
// them this frame; their children (a player's camera) follow.
[[nodiscard]] SceneView collect_view(const scene::Scene& scene, const AssetPath& asset_path,
                                     const std::unordered_map<Guid, math::Mat4>* render_poses = nullptr);

// An AssetPath as an asset::AssetFileResolver (materials resolve textures and parents with it).
[[nodiscard]] asset::AssetFileResolver file_resolver(const AssetPath& asset_path);

// An AssetPath that looks ids up in a registry.
[[nodiscard]] AssetPath registry_asset_path(const asset::Project& project, const asset::AssetRegistry& registry);

// The view matrix for a camera entity's world matrix (looks down its −Z, keeps the horizon level).
[[nodiscard]] math::Mat4 camera_view(const math::Mat4& camera_world);

} // namespace noor::game
