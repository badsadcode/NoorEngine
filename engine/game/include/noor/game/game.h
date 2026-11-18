#pragma once
// The game module: what it takes to run a NoorEngine game, shared by the editor's Play and by
// noor_player, so a game behaves the same in both (ADR-028).
//
//   make_world      a runtime::World for a scene, with every engine system a game uses
//   collect_view    what a running scene shows: models, primitive shapes, the game camera, lighting
//   SceneRenderer   draws a SceneView with the renderer (noor_player; the editor viewport has its own
//                   drawing with picking and gizmos)

#include "noor/asset/atmosphere.h"
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

#include <array>
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
    // The editor's live heights for a terrain (its sculpting may not be saved yet), from its component
    // and settings; null (or no callback): generated from the settings and files, like a game.
    std::function<std::shared_ptr<const terrain::Heights>(const asset::Json& component, const terrain::Settings& settings)> terrain_heights;
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
        Guid model_root;  // the placed model it belongs to (an ancestor with a `model` component of the same asset); nil if none
    };
    // Placed models (entities with a `model` component): levels of detail switch them as a whole
    // (noor/game/model_lod.h).
    struct Model {
        Guid entity;
        std::filesystem::path asset_path;
        math::Mat4 world;
    };
    std::vector<Model> models;
    // Scattered instances (`scatter` components, noor/game/scatter.h): whole models drawn from their
    // files at each instance (no entities), grouped by model file, levels of detail per instance.
    struct Scatter {
        Guid entity;
        struct Group {
            std::filesystem::path asset_path;
            std::shared_ptr<const std::vector<math::Mat4>> worlds;  // shared with the placement cache: not copied per frame
        };
        std::vector<Group> groups;
        std::size_t count = 0;
        std::string warning;  // why some or all are missing (an output that isn't cooked yet)
        float draw_distance = 0.0f;  // "draw_distance": instances further from the camera aren't drawn (grass); 0: no limit
        bool cast_shadows = true;    // "shadows": false for grass and other small things
    };
    std::vector<Scatter> scatters;
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
        // Material layers (Milestone 6, part 7): up to four, blended by the paint weights. Layer 0 is
        // `layers[0].material` or the component's `material`; empty: the default material.
        std::array<std::filesystem::path, 4> layer_files;
        math::Vec4 tiling{1, 1, 1, 1};        // per layer: multiplies the terrain's UVs (metres)
        std::optional<Guid> sculpt;           // its sculpt layer asset, if it has one
        std::optional<Guid> paint;            // its paint layer asset, if it has one
        // The editor: live heights (sculpting not saved yet) and a version per tile (row by row) that
        // changes when that tile's heights do. Null: generate from the settings.
        std::shared_ptr<const terrain::Heights> heights;
        std::vector<std::uint64_t> tile_versions;
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
    // "sky": "hdri" (the default with an HDRI), "gradient" (the procedural sky, even with an HDRI set) or
    // "atmosphere" (Milestone 8A, ADR-033), which builds the sky from the sun instead: a physically
    // based atmosphere ("turbidity": 1 clear .. 10 thick haze, "ground_albedo": 0.3, "altitude": 0 m).
    // The sun's "illuminance" then means outside the atmosphere and its "color" the sun's own colour
    // (white unless given): the air between tints and dims it (red at sunset), the sky's brightness
    // follows the sun's, and the sun's disc is drawn in the sky.
    // "time_of_day": hours (any sky; Milestone 8A part 2) places the sun instead of the light's rotation:
    // "latitude" (50), "day_of_year" (172, midsummer), "north" (degrees north is turned from -Z about +Y).
    // Fog (Milestone 8A part 3, any sky; noor/render/fog.h): "fog_density" (per metre; 0 or absent: none),
    // "fog_height" (m: full density below), "fog_falloff" (per metre above it), "fog_anisotropy" (glow
    // towards the sun, 0..0.95), "fog_color" (its albedo). "aerial_scale" (atmosphere sky): how far the
    // air makes things look (1 physical, more for small worlds, 0 off).
    // Clouds (Milestone 8A part 4, atmosphere sky; noor/render/clouds.h): "cloud_coverage" (0..1; present = the
    // layer is on), "cloud_density" (optical thickness of a full cloud), "cloud_altitude" (m), "cloud_scale"
    // (m, feature size), "cloud_wind_speed" (m/s), "cloud_wind_direction" (degrees, 0 = towards -Z), "cloud_seed";
    // a second layer with "cloud2_*". The game's time (render::Lighting::time) drifts them.
    // Night and space (part 5): with "time_of_day" the atmosphere sky has a moon ("moon": true, "moon_phase": 0
    // new .. 0.5 full, else from the day of the year), which lights the scene when it outshines the sun, and
    // stars ("stars": true, "star_brightness": 1, "star_seed": 1) that turn with the night. "sky": "space": no air,
    // a black sky with stars. Entities with a `celestial_body` ({"angular_size": degrees across, "color": albedo,
    // "rings": {"inner": 1.3, "outer": 2.4, "color", "tilt": 25}}) are planets and moons in the sky, towards the
    // entity's forward (-Z), rings across its +Y leaned towards the viewer by "tilt" degrees. Directional lights after the first are extra suns (no shadows).
    // A camera's "ev100": "auto" meters the light on the ground (render/exposure.h).
    // Weather zones (part 7): entities with a `weather_zone` blend their environment over this one by where the
    // scene's first camera is (noor/game/weather.h).
    struct Environment {
        std::filesystem::path hdri;   // empty: procedural
        math::Vec3 zenith, horizon, ground;  // procedural sky colours (relative)
        bool atmosphere = false;      // sky "atmosphere" (hdri is then empty)
        asset::AtmosphereSettings atmosphere_settings;
        math::Vec3 to_sun{0, 1, 0};   // atmosphere: towards the sun (unit)
        float aerial_scale = 1.0f;    // atmosphere: render::AtmosphereDesc::aerial_distance_scale
        bool space = false;           // sky "space": no air, a black sky (part 5)
    };
    Environment environment;

    // Weather zones (Milestone 8A, part 7; noor/game/weather.h): every zone and how much of it applied this time.
    struct WeatherWeight {
        Guid zone;
        float weight;
    };
    std::vector<WeatherWeight> weather;
};

// Finds a model asset's file from the `mesh` component's asset id.
using AssetPath = std::function<std::optional<std::filesystem::path>(const Guid& asset)>;

// `scene` must be expanded (no prefab instances left), e.g. World::scene(). `render_poses`
// (World::render_poses()) replaces the world matrices of entities physics moves with where to draw
// them this frame; their children (a player's camera) follow.
[[nodiscard]] SceneView collect_view(const scene::Scene& scene, const AssetPath& asset_path,
                                     const std::unordered_map<Guid, math::Mat4>* render_poses = nullptr,
                                     const std::filesystem::path& project_root = {});

// Where a procedural graph's outputs are cooked for games (Milestone 7): <root>/.noor/cooked/graphs/<graph id>/,
// written by the editor (on Save, and when the graph changes) and by noor_cli build/cook.
[[nodiscard]] std::filesystem::path cooked_graph_folder(const std::filesystem::path& root, const Guid& graph);

// A terrain component's settings; a graph base ("base": "graph") gets its cooked height field and that
// field's tiles and cell size. A field that isn't there yet is reported when the heights are built.
[[nodiscard]] terrain::Settings terrain_settings(const asset::Json& component, const asset::AssetFileResolver& resolve, const std::filesystem::path& project_root);

// A terrain tile as a draw item: a splat of its material layers (`material` resolves a layer's file).
[[nodiscard]] render::DrawItem terrain_draw(const SceneView::Terrain& terrain, render::MeshHandle tile,
                                            const std::function<render::MaterialHandle(const std::filesystem::path&)>& material);

// An AssetPath as an asset::AssetFileResolver (materials resolve textures and parents with it).
[[nodiscard]] asset::AssetFileResolver file_resolver(const AssetPath& asset_path);

// An AssetPath that looks ids up in a registry.
[[nodiscard]] AssetPath registry_asset_path(const asset::Project& project, const asset::AssetRegistry& registry);

// The view matrix for a camera entity's world matrix (looks down its −Z, keeps the horizon level).
[[nodiscard]] math::Mat4 camera_view(const math::Mat4& camera_world);

} // namespace noor::game
