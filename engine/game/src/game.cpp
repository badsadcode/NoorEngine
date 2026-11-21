#include "noor/game/game.h"
#include "noor/game/tilemap.h"
#include "noor/game/camera_follow.h"
#include "noor/game/timeline.h"
#include "noor/game/display.h"

#include "noor/asset/environment_asset.h"
#include "noor/asset/water_asset.h"
#include "noor/game/water.h"
#include "noor/game/weather.h"
#include "noor/fx/particle_system.h"
#include "noor/game/animator.h"
#include "noor/asset/model_import.h"
#include "noor/game/sprite_animation.h"
#include "noor/game/particle_ground.h"
#include "noor/runtime/script_api.h"
#include "noor/render/exposure.h"
#include "noor/game/scatter.h"
#include "noor/game/cooked_csg.h"

#include "noor/asset/gltf_import.h"
#include "noor/asset/json.h"
#include "noor/audio/audio.h"
#include "noor/foundation/log.h"
#include "noor/physics/physics.h"
#include "noor/runtime/behaviours.h"
#include "noor/scripting/csharp_scripts.h"
#include "noor/scripting/lua_scripts.h"

#include <algorithm>
#include <format>
#include <tuple>
#include <map>
#include <cmath>
#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace noor::game {

namespace stdfs = std::filesystem;
using asset::Json;

Result<std::unique_ptr<runtime::World>> make_world(const asset::Project& project, const asset::AssetRegistry& registry,
                                                   scene::PrefabLibrary& prefabs, const scene::Scene& scene,
                                                   const WorldOptions& options) {
    // The project's bindings, and the player's own over them (part 6e).
    const auto project_input = project.root() / "Settings" / "input.json";
    const auto overrides = options.user_dir.empty() ? stdfs::path() : options.user_dir / "input.json";
    auto base_map = runtime::InputMap::load(project_input);
    if (!base_map) return std::unexpected(base_map.error());
    auto input_map = runtime::InputMap::load(project_input, overrides);
    if (!input_map) {
        NOOR_LOG_WARN("Game", "{}: {} (the game's own bindings are used)", overrides.generic_string(), input_map.error().describe());
        input_map = *base_map;
    }
    auto world = std::make_unique<runtime::World>(scene::expand_prefabs(scene, prefabs.resolver()).scene, std::move(*input_map));
    world->input().set_base(std::move(*base_map));
    world->set_input_overrides(overrides);
    // Spawning prefabs while playing (gameplay foundations, part 1): by id, or by path through the registry. The library
    // and registry outlive the world (the caller owns them).
    world->set_prefabs(prefabs.resolver(), [&registry](std::string_view path) -> std::optional<Guid> {
        const auto* rec = registry.find_path(path);
        return rec ? std::optional(rec->guid) : std::nullopt;
    });

    world->add_system(std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{project.root()}));
    world->add_system(std::make_unique<scripting::CSharpScriptSystem>(static_cast<const noor_rt_host*>(options.script_host)));

    // Mesh colliders read triangles from the project's GLB files (each file loaded once per world).
    // The callbacks keep pointers to `project` and `registry`: they must outlive the world.
    physics::PhysicsSystem::Options physics_options;
    auto cache = std::make_shared<std::unordered_map<std::string, std::shared_ptr<asset::ImportedModel>>>();
    physics_options.mesh_triangles = [&project, &registry, cache](const Json& ref) -> std::optional<physics::Triangles> {
        const auto guid = asset::guid_field(ref, "asset");
        const auto* rec = guid ? registry.find(*guid) : nullptr;
        if (!rec) return std::nullopt;
        auto path = project.resolve(rec->path);
        if (!path) return std::nullopt;
        auto& model = (*cache)[path->generic_string()];
        if (!model) {
            auto imported = asset::import_model(*path);
            if (!imported) return std::nullopt;
            model = std::make_shared<asset::ImportedModel>(std::move(*imported));
        }
        const int index = ref.value("mesh_index", -1);
        if (index < 0 || index >= static_cast<int>(model->meshes.size())) return std::nullopt;
        physics::Triangles t;
        for (const auto& v : model->meshes[static_cast<std::size_t>(index)].vertices) t.positions.push_back(v.position);
        t.indices = model->meshes[static_cast<std::size_t>(index)].indices;
        return t;
    };
    // CSG models collide with their cooked triangles (written by the editor and noor_cli).
    physics_options.csg_triangles = [&project](const Guid& model) -> std::optional<physics::Triangles> {
        auto cooked = read_cooked_csg(cooked_csg_path(project.root(), model));
        if (!cooked) return std::nullopt;
        physics::Triangles t;
        t.positions.reserve(cooked->vertices.size());
        for (const auto& v : cooked->vertices) t.positions.push_back(v.position);
        t.indices = std::move(cooked->indices);
        return t;
    };
    // Terrains collide as height fields, generated from their settings (shared with the renderer).
    physics_options.terrain_tiles = [&project, &registry, live = options.terrain_heights](const Json& component) {
        std::vector<physics::HeightTile> tiles;
        const auto settings = terrain_settings(component, file_resolver(registry_asset_path(project, registry)), project.root());
        Result<std::shared_ptr<const terrain::Heights>> heights = live ? live(component, settings) : nullptr;
        if (!*heights) heights = terrain::Heights::shared(settings);
        if (!heights) {
            NOOR_LOG_ERROR("Game", "Terrain: {}", heights.error().describe());
            return tiles;
        }
        const auto& h = **heights;
        for (std::uint32_t tz = 0; tz < settings.tiles_z; ++tz)
            for (std::uint32_t tx = 0; tx < settings.tiles_x; ++tx) {
                const math::Vec2 o = h.origin();
                const float step = static_cast<float>(terrain::kTileCells) * h.cell_size();
                tiles.push_back({{o.x + static_cast<float>(tx) * step, 0.0f, o.y + static_cast<float>(tz) * step}, h.cell_size(), terrain::kTileSamples,
                                 h.tile(tx, tz)});
            }
        return tiles;
    };
    // Scattered instances collide by their models' collision nodes (Milestone 7, part 8; Noor Interchange
    // 3.5): a tree's `capsule` trunk, a rock's `convex` pieces, at every instance, standing on the same
    // ground the renderer puts them on. Models without capsule or convex collision nodes don't collide.
    physics_options.scatter_shapes = [&project, &registry, cache, live = options.terrain_heights](const scene::Entity& entity,
                                                                                                const runtime::World& w) {
        std::vector<physics::StaticShape> out;
        const auto& comp = entity.components["scatter"];
        const auto items = scatter_items(comp, project.root());
        std::shared_ptr<const terrain::Heights> ground;
        math::Vec3 ground_at{};
        if (const auto gid = asset::guid_field(comp, "ground")) {
            for (std::size_t i = 0; i < w.scene().entities.size(); ++i) {
                const auto& t = w.scene().entities[i];
                if (t.id != *gid || !t.has("terrain") || !t.components["terrain"].is_object()) continue;
                const auto settings = terrain_settings(t.components["terrain"], file_resolver(registry_asset_path(project, registry)), project.root());
                Result<std::shared_ptr<const terrain::Heights>> h = live ? live(t.components["terrain"], settings) : nullptr;
                if (!*h) h = terrain::Heights::shared(settings);
                if (h) ground = *h;
                ground_at = w.scene().world_matrix(i).transform_point({0, 0, 0});
            }
        }
        math::Mat4 world_m = math::Mat4::identity();
        for (std::size_t i = 0; i < w.scene().entities.size(); ++i)
            if (w.scene().entities[i].id == entity.id) world_m = w.scene().world_matrix(i);
        struct Shapes {
            bool known = false;
            std::vector<physics::StaticShape> local;  // in the model's space
        };
        std::map<Guid, Shapes> by_asset;
        for (const auto& placed : place_scatter(items, world_m, ground.get(), ground_at)) {
            Shapes& t = by_asset[placed.asset];
            if (!t.known) {
                t.known = true;
                const auto* rec = registry.find(placed.asset);
                auto path = rec ? project.resolve(rec->path) : Result<stdfs::path>(fail(ErrorCode::NotFound, "unknown asset"));
                if (path) {
                    auto& model = (*cache)[path->generic_string()];
                    if (!model)
                        if (auto imported = asset::import_model(*path)) model = std::make_shared<asset::ImportedModel>(std::move(*imported));
                    if (model)
                        for (std::size_t n = 0; n < model->nodes.size(); ++n) {
                            const auto& node = model->nodes[n];
                            if (!node.is_collision()) continue;
                            const std::string kind = node.noor.value("collision", std::string());
                            const math::Mat4 nm = model->world_matrix(n);
                            if (kind == "capsule") {
                                t.local.push_back({physics::StaticShape::Kind::Capsule, nm.transform_point({0, 0, 0}), node.noor.value("radius", 0.3f),
                                                   node.noor.value("height", 2.0f), {}});
                            } else if (kind == "convex" && node.mesh >= 0 && node.mesh < static_cast<int>(model->meshes.size())) {
                                physics::StaticShape hull{physics::StaticShape::Kind::Convex, {}, 0, 0, {}};
                                for (const auto& v : model->meshes[static_cast<std::size_t>(node.mesh)].vertices) hull.points.push_back(nm.transform_point(v.position));
                                t.local.push_back(std::move(hull));
                            }
                        }
                }
            }
            const float s = math::length(math::Vec3{placed.world.m[0].x, placed.world.m[0].y, placed.world.m[0].z});
            for (const auto& local : t.local) {
                if (local.kind == physics::StaticShape::Kind::Capsule) {
                    out.push_back({local.kind, placed.world.transform_point(local.centre), local.radius * s, local.height * s, {}});
                } else {
                    physics::StaticShape hull{local.kind, {}, 0, 0, {}};
                    hull.points.reserve(local.points.size());
                    for (const auto& q : local.points) hull.points.push_back(placed.world.transform_point(q));
                    out.push_back(std::move(hull));
                }
            }
        }
        return out;
    };
    // Tilemaps collide by their solid and one-way tiles, merged into boxes (2D part 4); rebuilt when the rows change.
    physics_options.tilemap_key = [](const Json& c) -> std::uint64_t {
        std::uint64_t h = asset::tile_rows_hash(c.contains("rows") ? c["rows"] : Json());
        for (const char* k : {"tileset", "layout", "cell", "collision"})
            if (c.contains(k)) h ^= std::hash<std::string>{}(c[k].dump()) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        return h;
    };
    physics_options.tilemap_shapes = [&project, &registry](const scene::Entity& entity, const runtime::World& w) {
        std::vector<physics::StaticShape> out;
        const Json& c = entity.components["tilemap"];
        const auto id = asset::guid_field(c, "tileset");
        const auto file = id ? registry_asset_path(project, registry)(*id) : std::nullopt;
        if (!file) return out;
        auto tileset = asset::load_tileset_file(*file);
        if (!tileset) {
            NOOR_LOG_ERROR("Game", "'{}': {}", entity.name, tileset.error().message);
            return out;
        }
        math::Mat4 world_m = math::Mat4::identity();
        for (std::size_t i = 0; i < w.scene().entities.size(); ++i)
            if (w.scene().entities[i].id == entity.id) world_m = w.scene().world_matrix(i);
        for (const auto& b : tilemap_boxes(*tile_grid(c), *tileset, tilemap_settings(c), world_m)) {
            physics::StaticShape s;
            s.kind = physics::StaticShape::Kind::Box;
            s.centre = b.centre;
            s.half = b.half;
            s.rotation = b.rotation;
            s.one_way = b.one_way;
            out.push_back(std::move(s));
        }
        return out;
    };
    world->add_system(std::make_unique<physics::PhysicsSystem>(std::move(physics_options)));

    // A sound clip is an asset id or a project-relative path.
    audio::AudioSystem::Options audio_options;
    audio_options.use_device = options.audio_device;
    audio_options.resolve_clip = [&project, &registry](std::string_view clip) -> std::optional<stdfs::path> {
        if (const auto guid = Guid::parse(clip)) {
            const auto* rec = registry.find(*guid);
            if (!rec) return std::nullopt;
            auto p = project.resolve(rec->path);
            return p ? std::optional(*p) : std::nullopt;
        }
        auto p = project.resolve(clip);
        return p && stdfs::exists(*p) ? std::optional(*p) : std::nullopt;
    };
    world->add_system(std::make_unique<audio::AudioSystem>(std::move(audio_options)));
    // Effect particles (Milestone 8B): emitters may point at .noorfx assets, and the wind may come from an environment asset.
    fx::ParticleSystem::Options fx_options;
    fx_options.resolve_asset = [&project, &registry](const Guid& id) -> std::optional<stdfs::path> {
        const auto* rec = registry.find(id);
        if (!rec) return std::nullopt;
        auto p = project.resolve(rec->path);
        return p ? std::optional(*p) : std::nullopt;
    };
    // Colliding particles (part 7) stand on the scene's terrains: the same heights as drawing and physics (live ones
    // while sculpting in the editor). Rebuilt only when a terrain's component or place changes.
    // Water (8D part 3) reads the same terrains for its depth maps.
    struct GroundCache {
        std::unordered_map<Guid, std::pair<std::string, ParticleGround::Piece>> pieces;
        ParticleGround ground;
    };
    const auto refresh_ground = [&project, &registry, live = options.terrain_heights,
                                 cache = std::make_shared<GroundCache>()](runtime::World& w) -> const ParticleGround& {
        bool changed = false;
        std::unordered_set<Guid> seen;
        for (const auto& e : w.scene().entities) {
            if (!e.has("terrain") || !e.components.at("terrain").is_object()) continue;
            seen.insert(e.id);
            const Json& component = e.components.at("terrain");
            const auto m = runtime::api::world_matrix(w, e.id);
            const math::Vec3 pos = m ? m->transform_point({0, 0, 0}) : math::Vec3{0, 0, 0};
            const std::string key = std::format("{} {} {} {}", component.dump(), pos.x, pos.y, pos.z);
            auto& slot = cache->pieces[e.id];
            if (slot.first == key && slot.second.heights) continue;
            const auto settings = terrain_settings(component, file_resolver(registry_asset_path(project, registry)), project.root());
            Result<std::shared_ptr<const terrain::Heights>> heights = live ? live(component, settings) : nullptr;
            if (!*heights) heights = terrain::Heights::shared(settings);
            slot = {key, {pos, heights ? *heights : nullptr}};
            changed = true;
        }
        changed |= std::erase_if(cache->pieces, [&](const auto& kv) { return !seen.contains(kv.first); }) > 0;
        if (changed) {
            std::vector<ParticleGround::Piece> list;
            for (const auto& [id, piece] : cache->pieces) list.push_back(piece.second);
            std::ranges::sort(list, [](const auto& a2, const auto& b2) {
                return std::tie(a2.position.x, a2.position.z) < std::tie(b2.position.x, b2.position.z);
            });
            cache->ground.set(std::move(list));
        }
        return cache->ground;
    };
    fx_options.ground = [refresh_ground](runtime::World& w) { return refresh_ground(w).query(); };
    // Their wind is the one the weather has: the scene's environment with its weather zones blended in (a storm
    // rolling in blows the smoke about, not just the rain).
    fx_options.wind = [asset_path = registry_asset_path(project, registry)](runtime::World& w) { return effect_wind(w, asset_path); };
    world->add_system(std::make_unique<fx::ParticleSystem>(std::move(fx_options)));
    // Water for gameplay (Milestone 8D, part 2): the bodies and their waves, answering water_height for scripts.
    world->add_system(std::make_unique<WaterSystem>(registry_asset_path(project, registry),
                                                    [refresh_ground](runtime::World& w) { return refresh_ground(w).pieces(); }));
    // Sprite animations played by scripts: their end events (Milestone 8B, part 6).
    world->add_system(std::make_unique<SpriteAnimationSystem>([&project, &registry](const Guid& id) -> std::optional<stdfs::path> {
        const auto* rec = registry.find(id);
        if (!rec) return std::nullopt;
        auto p = project.resolve(rec->path);
        return p ? std::optional(*p) : std::nullopt;
    }));
    // Skeletal animation (Milestone 8C, part 2): models' animators pose their bones, after scripts have had their say.
    world->add_system(std::make_unique<AnimatorSystem>([&project, &registry](const Guid& id) -> std::optional<stdfs::path> {
        const auto* rec = registry.find(id);
        if (!rec) return std::nullopt;
        auto p = project.resolve(rec->path);
        return p ? std::optional(*p) : std::nullopt;
    }));
    // Timelines (gameplay foundations, part 5): animation players animate any property, after scripts' update.
    world->add_system(std::make_unique<TimelineSystem>([&project, &registry](const Guid& id) -> std::optional<stdfs::path> {
        const auto* rec = registry.find(id);
        if (!rec) return std::nullopt;
        auto p = project.resolve(rec->path);
        return p ? std::optional(*p) : std::nullopt;
    }));
    world->add_system(std::make_unique<runtime::BehavioursSystem>());
    // Last: cameras follow where things are drawn this frame (after physics' render poses and scripts).
    world->add_system(std::make_unique<CameraFollowSystem>(load_display_settings(project.root()).aspect()));
    return world;
}

namespace {

// Logs a warning once per distinct message (a broken reference is read every frame).
void warn_once(const std::string& message) {
    static std::mutex mutex;
    static std::unordered_set<std::string> said;
    std::scoped_lock lock(mutex);
    if (said.insert(message).second) NOOR_LOG_WARN("Game", "{}", message);
}

// A scene's environment component with its asset (Milestone 8A, part 6): {"asset": id} reads the .noorenv
// (cached by file and time, so edits to it show at once), and the component's other fields override it.
Json resolved_environment(const Json& component, const AssetPath& asset_path) {
    const auto id = component.contains("asset") ? asset::guid_field(component, "asset") : std::nullopt;
    if (!id || !asset_path) return component;
    const auto file = asset_path(*id);
    if (!file) {
        warn_once(std::format("environment asset {} not found: the component's own fields only", id->to_string()));
        return component;
    }
    struct Cached {
        std::filesystem::file_time_type time;
        Json environment;
    };
    static std::mutex mutex;
    static std::unordered_map<std::string, Cached> cache;
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(*file, ec);
    const std::string key = file->generic_string();
    {
        std::scoped_lock lock(mutex);
        if (const auto it = cache.find(key); it != cache.end() && it->second.time == time) return asset::merge_environment(it->second.environment, component);
    }
    auto loaded = asset::load_environment_file(*file);
    if (!loaded) {
        warn_once(std::format("environment asset {}: {}", file->filename().string(), loaded.error().describe()));
        return component;
    }
    Json environment = (*loaded)["environment"];
    std::scoped_lock lock(mutex);
    cache[key] = {time, environment};
    return asset::merge_environment(environment, component);
}

float number(const Json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

// The atmosphere an environment's settings ask for (the scene-facing knobs over Earth's defaults).
asset::AtmosphereSettings atmosphere_settings_of(const Json& en) {
    asset::AtmosphereSettings as;
    as.turbidity = std::clamp(number(en, "turbidity", as.turbidity), 0.0f, 20.0f);
    as.altitude_m = std::clamp(number(en, "altitude", as.altitude_m), 0.0f, 90000.0f);
    const float albedo = std::clamp(number(en, "ground_albedo", as.ground_albedo.x), 0.0f, 1.0f);
    as.ground_albedo = {albedo, albedo, albedo};
    return as;
}

math::Vec3 color_of(const Json& primitive, math::Vec3 color = {0.55f, 0.52f, 0.62f}) {
    if (!primitive.contains("color") || !primitive["color"].is_string()) return color;
    const std::string hex = primitive["color"].get<std::string>();
    if (hex.size() != 7 || hex[0] != '#') return color;
    const auto channel = [&](int i) {
        const float c = static_cast<float>(std::stoi(hex.substr(static_cast<std::size_t>(1 + 2 * i), 2), nullptr, 16)) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);  // sRGB -> linear
    };
    try {
        return {channel(0), channel(1), channel(2)};
    } catch (const std::exception&) {
        return color;
    }
}

math::Vec3 effect_wind_of(runtime::World& world, const AssetPath& asset_path) {
    const auto& entities = world.scene().entities;
    const scene::Entity* env_entity = nullptr;
    math::Vec3 viewer{0, 0, 0};  // the first camera judges the zones, as for the sky
    bool have_viewer = false;
    for (const auto& e : entities) {
        if (!env_entity && e.has("environment") && e.components.at("environment").is_object()) env_entity = &e;
        if (!have_viewer && e.has("camera") && e.components.at("camera").is_object())
            if (const auto m = runtime::api::world_matrix(world, e.id)) {
                viewer = m->transform_point({0, 0, 0});
                have_viewer = true;
            }
    }
    if (!env_entity) return {0, 0, 0};
    Json en = resolved_environment(env_entity->components.at("environment"), asset_path);  // cached by file and time
    struct Applied {
        float priority, weight;
        const Json* zone;
    };
    std::vector<Applied> zones;
    for (const auto& e : entities) {
        if (!e.has("weather_zone") || !e.components.at("weather_zone").is_object()) continue;
        const Json& z = e.components.at("weather_zone");
        const auto m = runtime::api::world_matrix(world, e.id);
        const float w = weather_zone_weight(z, m ? *m : math::Mat4::identity(), viewer);
        if (w > 0.0f) zones.push_back({number(z, "priority", 0.0f), w, &z});
    }
    std::ranges::stable_sort(zones, [](const Applied& a, const Applied& b) { return a.priority < b.priority; });
    for (const Applied& a : zones) {
        const Json& z = *a.zone;
        Json settings = Json::object();
        if (z.contains("environment") && z["environment"].is_string()) settings = resolved_environment(Json{{"asset", z["environment"]}}, asset_path);
        else if (z.contains("environment") && z["environment"].is_object()) settings = z["environment"];
        en = asset::blend_environments(en, settings, a.weight);
    }
    const float speed = std::max(0.0f, number(en, "wind_speed", 0.0f));
    const float angle = number(en, "wind_direction", 0.0f) * 3.14159265f / 180.0f;
    return {fx::det_sin(angle) * speed, 0.0f, -fx::det_cos(angle) * speed};  // towards the direction (0 = -Z), as the clouds
}

} // namespace

math::Vec3 effect_wind(runtime::World& world, const AssetPath& asset_path) { return effect_wind_of(world, asset_path); }

SceneView collect_view(const scene::Scene& scene, const AssetPath& asset_path, const std::unordered_map<Guid, math::Mat4>* render_poses,
                       const std::filesystem::path& project_root) {
    SceneView view;
    std::vector<std::pair<WaterBody, asset::WaterLook>> pending_water;  // water bodies, waved once the wind is known
    Json water_environment;  // the environment as blended (its underwater settings, 8D part 4)
    const auto& entities = scene.entities;
    bool has_environment = false;
    float env_intensity = 8000.0f;
    bool sun_color_given = false;
    bool auto_exposure = false;
    std::optional<Json> environment_ev100;  // an environment's own exposure wins over the camera's (part 6)
    std::vector<render::CelestialBody> bodies;  // celestial_body entities (part 5)
    struct NightSky {
        bool moon = true, stars = true;
        std::optional<float> moon_phase;
        float star_brightness = 1.0f;
        std::uint32_t star_seed = 1;
    } night;
    struct TimeOfDay {
        float hours, latitude, day, north;
    };
    std::optional<TimeOfDay> time_of_day;
    // World matrices, with render poses in place of the simulated ones (and children following).
    // Each is computed once and kept (a parent's matrix is reused by all its children): the scene's
    // own world_matrix() searches for every parent, which is quadratic and took 190 ms a frame for a
    // 2,000-tree forest (Milestone 7, part 6).
    std::unordered_map<Guid, std::size_t> index;
    index.reserve(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i) index.emplace(entities[i].id, i);
    std::vector<std::optional<math::Mat4>> memo(entities.size());
    const bool blended = render_poses && !render_poses->empty();
    // Parallax layers (2D part 6): "parallax": {"factor": [fx, fy], "anchor": [ax, ay]} moves an entity (and so its
    // children) by (viewer - anchor) x (1 - factor) in X and Y: factor 0 stays with the camera (a far sky), 1 is the
    // world itself, between is depth. The viewer is found first, without parallax.
    bool parallax_ready = false;
    math::Vec3 viewer{0, 0, 0};
    const auto parallax_shift = [&](const scene::Entity& e) -> math::Vec3 {
        const Json& p = e.components.at("parallax");
        math::Vec2 f{1, 1}, a{0, 0};
        if (const auto v = p.find("factor"); v != p.end() && v->is_array() && v->size() == 2 && (*v)[0].is_number() && (*v)[1].is_number())
            f = {(*v)[0].get<float>(), (*v)[1].get<float>()};
        else if (v != p.end() && v->is_number())
            f = {v->get<float>(), v->get<float>()};
        if (const auto v = p.find("anchor"); v != p.end() && v->is_array() && v->size() == 2 && (*v)[0].is_number() && (*v)[1].is_number())
            a = {(*v)[0].get<float>(), (*v)[1].get<float>()};
        return parallax_offset({viewer.x, viewer.y}, a, f);
    };
    std::function<math::Mat4(std::size_t, int)> world_of = [&](std::size_t i, int depth) -> math::Mat4 {
        if (memo[i]) return *memo[i];
        math::Mat4 m;
        const math::Mat4* pose = nullptr;
        if (blended) {
            if (const auto it = render_poses->find(entities[i].id); it != render_poses->end()) pose = &it->second;
        }
        if (pose) {
            m = *pose;
        } else {
            m = entities[i].transform.matrix();
            const auto parent = entities[i].parent ? index.find(*entities[i].parent) : index.end();
            if (parent != index.end() && depth < 256) m = world_of(parent->second, depth + 1) * m;  // depth: cycle guard
        }
        if (parallax_ready && entities[i].has("parallax") && entities[i].components["parallax"].is_object()) {
            const math::Vec3 s = parallax_shift(entities[i]);
            m.m[3].x += s.x;
            m.m[3].y += s.y;
        }
        memo[i] = m;
        return m;
    };
    // The placed model an entity belongs to: the nearest ancestor (or itself) with a `model` component.
    std::vector<int> model_root(entities.size(), -2);  // -2: not known yet, -1: none
    std::function<int(std::size_t, int)> root_walk = [&](std::size_t i, int depth) -> int {
        if (model_root[i] != -2) return model_root[i];
        int r = -1;
        if (entities[i].has("model") && entities[i].components["model"].is_object()) {
            r = static_cast<int>(i);
        } else if (const auto p = entities[i].parent ? index.find(*entities[i].parent) : index.end(); p != index.end() && depth < 256) {
            r = root_walk(p->second, depth + 1);
        }
        model_root[i] = r;
        return r;
    };
    const auto model_root_of = [&](std::size_t i) { return root_walk(i, 0); };
    // Asset files by id, resolved once per call: a forest has thousands of meshes but few assets, and
    // resolving a path (checks, normalising) every frame for each was most of a 2,000-tree frame.
    std::unordered_map<Guid, std::optional<std::filesystem::path>> files;
    const auto asset_file = [&](const Guid& id) -> const std::optional<std::filesystem::path>& {
        auto it = files.find(id);
        if (it == files.end()) it = files.emplace(id, asset_path(id)).first;
        return it->second;
    };
    std::unordered_map<std::size_t, std::optional<Guid>> root_asset;  // a model root's asset id, parsed once
    std::vector<std::size_t> scatter_entities;  // handled after the loop (they may stand on a terrain listed later)
    // Weather zones (Milestone 8A, part 7): where the viewer (the scene's first camera) is decides how much of each
    // zone's environment blends over the scene's own.
    struct Zone {
        std::size_t index;
        float priority;
        float weight;
    };
    std::vector<Zone> zones;
    std::vector<std::size_t> zone_entities;  // every zone, on or not (their atmospheres are built up front)
    for (std::size_t i = 0; i < entities.size(); ++i)
        if (entities[i].has("camera") && entities[i].components["camera"].is_object()) {
            viewer = world_of(i, 0).transform_point({0, 0, 0});
            break;
        }
    if (std::ranges::any_of(entities, [](const scene::Entity& e) { return e.has("parallax"); })) {
        std::ranges::fill(memo, std::nullopt);  // the viewer's chain was resolved without parallax
        parallax_ready = true;
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i].has("weather_zone") || !entities[i].components["weather_zone"].is_object()) continue;
        const auto& z = entities[i].components["weather_zone"];
        const float w = weather_zone_weight(z, world_of(i, 0), viewer);
        view.weather.push_back({entities[i].id, w});
        zone_entities.push_back(i);
        if (w > 0.0f) zones.push_back({i, number(z, "priority", 0.0f), w});
    }
    std::ranges::stable_sort(zones, [](const Zone& a, const Zone& b) { return a.priority < b.priority; });
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        const bool camera = !view.camera && e.has("camera") && e.components["camera"].is_object();
        const bool primitive = e.has("primitive") && e.components["primitive"].is_object();
        const bool mesh = e.has("mesh");
        const bool is_light = e.has("light") && e.components["light"].is_object();
        const std::string light_type = is_light ? e.components["light"].value("type", std::string("directional")) : "";
        const bool sun = is_light && !view.has_sun && light_type == "directional";
        const bool local = is_light && (light_type == "point" || light_type == "spot");
        const bool extra_sun = is_light && !sun && view.has_sun && light_type == "directional";  // a second sun (part 5)
        const bool body = e.has("celestial_body") && e.components["celestial_body"].is_object();
        const bool env = !has_environment && e.has("environment") && e.components["environment"].is_object();
        const bool model = e.has("model") && e.components["model"].is_object();
        const bool scatter = e.has("scatter") && e.components["scatter"].is_object();
        const bool sprite = e.has("sprite") && e.components["sprite"].is_object();
        if (e.has("water") && e.components["water"].is_object()) {
            // Water (8D): its look (preset, asset, then its own fields), its footprint from size and scale; its waves once
            // the wind is known (below).
            const Json& wc = e.components["water"];
            const asset::WaterLook look = water_look(wc, asset_path);
            pending_water.push_back({water_body(wc, world_of(i, 0), {}), look});
        }
        if (scatter) scatter_entities.push_back(i);
        if (!camera && !primitive && !sprite && !mesh && !model && !sun && !local && !extra_sun && !body && !env && !e.has("csg_model") &&
            !e.has("terrain") && !e.has("tilemap"))
            continue;
        if (env) {
            Json en = resolved_environment(e.components["environment"], asset_path);
            const auto zone_settings = [&](std::size_t zone_index) {
                const auto& z = entities[zone_index].components["weather_zone"];
                if (z.contains("environment") && z["environment"].is_string())
                    return resolved_environment(Json{{"asset", z["environment"]}}, asset_path);
                if (z.contains("environment") && z["environment"].is_object()) return z["environment"];
                return Json(Json::object());
            };
            // The atmosphere tables every blend here can need, built now (in parallel) rather than one per frame
            // while the camera walks in: from the scene's own settings to each zone's, fully on.
            if (!zone_entities.empty()) {
                std::vector<asset::AtmosphereSettings> ends{atmosphere_settings_of(en)};
                for (std::size_t z : zone_entities) ends.push_back(atmosphere_settings_of(asset::blend_environments(en, zone_settings(z), 1.0f)));
                asset::Atmosphere::prewarm(ends);
            }
            for (const Zone& zone : zones)  // weather zones over the scene's own environment
                en = asset::blend_environments(en, zone_settings(zone.index), zone.weight);
            water_environment = en;
            if (en.contains("ev100") && (en["ev100"].is_number() || en["ev100"].is_string())) environment_ev100 = en["ev100"];
            const std::string sky_mode = en.contains("sky") && en["sky"].is_string() ? en["sky"].get<std::string>() : std::string();
            render::FogParams& fog = view.lighting.fog;
            fog.density = std::clamp(number(en, "fog_density", 0.0f), 0.0f, 1.0f);
            fog.height = number(en, "fog_height", fog.height);
            fog.falloff = std::clamp(number(en, "fog_falloff", fog.falloff), 0.0f, 10.0f);
            fog.anisotropy = std::clamp(number(en, "fog_anisotropy", fog.anisotropy), 0.0f, 0.95f);
            if (en.contains("fog_color")) {
                Json tint = Json::object();
                tint["color"] = en["fog_color"];
                fog.albedo = color_of(tint, fog.albedo);
            }
            view.environment.aerial_scale = std::clamp(number(en, "aerial_scale", 1.0f), 0.0f, 1000.0f);
            // Rain, snow and dust (Milestone 8B, part 3): intensities 0..1, blown by the ground wind (the same wind
            // the particle system reads: "wind_speed" m/s, "wind_direction" degrees towards which it blows).
            {
                auto& w = view.lighting.weather;
                w.rain = std::clamp(number(en, "rain", 0.0f), 0.0f, 1.0f);
                w.snow = std::clamp(number(en, "snow", 0.0f), 0.0f, 1.0f);
                w.dust = std::clamp(number(en, "dust", 0.0f), 0.0f, 1.0f);
                const float speed = std::max(0.0f, number(en, "wind_speed", 0.0f));
                const float a = math::radians(number(en, "wind_direction", 0.0f));
                w.wind = {std::sin(a) * speed, 0.0f, -std::cos(a) * speed};
                w.seed = static_cast<std::uint32_t>(std::max(0.0f, number(en, "weather_seed", 1.0f)));
            }
            // Cloud layers: "cloud_*" (the first) and "cloud2_*" (the second); a layer is on when it has a coverage.
            for (std::size_t layer_index = 0; layer_index < render::kCloudLayers; ++layer_index) {
                const std::string prefix = layer_index == 0 ? "cloud_" : "cloud2_";
                render::CloudLayer& layer = view.lighting.clouds[layer_index];
                if (!en.contains(prefix + "coverage")) continue;
                layer.enabled = true;
                layer.coverage = std::clamp(number(en, (prefix + "coverage").c_str(), layer.coverage), 0.0f, 1.0f);
                layer.density = std::clamp(number(en, (prefix + "density").c_str(), layer.density), 0.0f, 100.0f);
                layer.altitude_km = std::clamp(number(en, (prefix + "altitude").c_str(), layer.altitude_km * 1000.0f) * 0.001f, 0.05f, 50.0f);
                layer.scale_km = std::clamp(number(en, (prefix + "scale").c_str(), layer.scale_km * 1000.0f) * 0.001f, 0.05f, 100.0f);
                layer.wind_speed = std::clamp(number(en, (prefix + "wind_speed").c_str(), layer.wind_speed), 0.0f, 200.0f);
                layer.wind_direction = number(en, (prefix + "wind_direction").c_str(), layer.wind_direction);
                layer.seed = static_cast<std::uint32_t>(std::max(0.0f, number(en, (prefix + "seed").c_str(), static_cast<float>(layer.seed))));
                // Part 7b (ADR-034): "cloud_type": "volume" makes the layer a slab "cloud_thickness" metres thick.
                const std::string type_key = prefix + "type";
                layer.volume = en.contains(type_key) && en[type_key].is_string() && en[type_key].get<std::string>() == "volume";
                layer.thickness_km = std::clamp(number(en, (prefix + "thickness").c_str(), layer.thickness_km * 1000.0f) * 0.001f, 0.05f, 10.0f);
            }
            if (en.contains("time_of_day") && en["time_of_day"].is_number())
                time_of_day = TimeOfDay{std::fmod(std::fmod(number(en, "time_of_day", 12.0f), 24.0f) + 24.0f, 24.0f),
                                        std::clamp(number(en, "latitude", 50.0f), -90.0f, 90.0f),
                                        std::clamp(number(en, "day_of_year", 172.0f), 1.0f, 366.0f), number(en, "north", 0.0f)};
            night.moon = !en.contains("moon") || !en["moon"].is_boolean() || en["moon"].get<bool>();
            night.stars = !en.contains("stars") || !en["stars"].is_boolean() || en["stars"].get<bool>();
            if (en.contains("moon_phase") && en["moon_phase"].is_number()) night.moon_phase = en["moon_phase"].get<float>();
            night.star_brightness = std::clamp(number(en, "star_brightness", 1.0f), 0.0f, 1000.0f);
            night.star_seed = static_cast<std::uint32_t>(std::max(0.0f, number(en, "star_seed", 1.0f)));
            if (sky_mode == "space") view.environment.space = true;
            if (sky_mode == "atmosphere") {
                view.environment.atmosphere = true;
                view.environment.atmosphere_settings = atmosphere_settings_of(en);
            } else if (const auto id = asset::guid_field(en, "hdri"); id && asset_path && sky_mode != "gradient") {
                if (auto file = asset_path(*id)) view.environment.hdri = std::move(*file);
            }
            env_intensity = std::clamp(number(en, "intensity", 8000.0f), 0.0f, 1.0e7f);
            view.lighting.environment_rotation = math::radians(number(en, "rotation", 0.0f));
            if (en.contains("background") && en["background"].is_boolean()) view.lighting.sky_background = en["background"].get<bool>();
            has_environment = true;
        }
        const math::Mat4 world = world_of(i, 0);
        if (camera) {
            const auto& cam = e.components["camera"];
            view.camera = SceneView::Camera{world, std::clamp(number(cam, "fov", 70.0f), 10.0f, 150.0f),
                                            std::max(0.001f, number(cam, "near", 0.05f)), e.id};
            if (cam.value("projection", std::string("perspective")) == "orthographic") {
                view.camera->ortho_height = std::clamp(number(cam, "height", 10.0f), 0.01f, 1.0e5f);
                view.camera->far_plane = std::clamp(number(cam, "far", 1000.0f), view.camera->near_plane * 2.0f, 1.0e6f);
                view.camera->pixel_snap = cam.value("pixel_snap", false);
            }
            view.lighting.ev100 = std::clamp(number(cam, "ev100", view.lighting.ev100), -10.0f, 25.0f);
            auto_exposure = cam.contains("ev100") && cam["ev100"].is_string() && cam["ev100"].get<std::string>() == "auto";
            const std::string tm = cam.value("tonemapper", std::string("neutral"));
            view.lighting.tonemapper = tm == "aces" ? render::Tonemapper::Aces
                                       : tm == "none" ? render::Tonemapper::None
                                                      : render::Tonemapper::Neutral;
        }
        if (sun) {
            const auto& l = e.components["light"];
            const math::Vec3 dir = math::transform_vector(world, {0, 0, -1});
            if (math::length(dir) > 1e-6f) view.lighting.sun_direction = math::normalize(dir);
            view.lighting.sun_color = color_of(l, view.lighting.sun_color);
            sun_color_given = l.contains("color");
            view.lighting.sun_illuminance = std::clamp(number(l, "illuminance", view.lighting.sun_illuminance), 0.0f, 1.0e6f);
            // The sky that goes with this sun, until environments arrive (Milestone 5, part 5).
            if (l.contains("sky_color")) {
                Json sky = Json::object();
                sky["color"] = l["sky_color"];
                view.lighting.sky_color = color_of(sky, view.lighting.sky_color);
            }
            view.lighting.sky_luminance = std::clamp(number(l, "sky_luminance", view.lighting.sky_luminance), 0.0f, 1.0e5f);
            if (l.contains("shadows") && l["shadows"].is_boolean()) view.lighting.sun_shadows = l["shadows"].get<bool>();
            view.lighting.shadow_distance = std::clamp(number(l, "shadow_distance", view.lighting.shadow_distance), 5.0f, 2000.0f);
            view.has_sun = true;
        }
        if (extra_sun) {
            const auto& l = e.components["light"];
            render::LocalLight light;
            light.type = render::LocalLight::Type::Directional;
            const math::Vec3 dir = math::transform_vector(world, {0, 0, -1});
            light.direction = math::length(dir) > 1e-6f ? math::normalize(dir) : math::Vec3{0, -1, 0};
            light.color = color_of(l, {1, 1, 1});
            light.lux = std::clamp(number(l, "illuminance", 100000.0f), 0.0f, 1.0e6f);
            view.lighting.lights.push_back(light);
            view.light_markers.push_back({world.transform_point({0, 0, 0}), light.color, e.id});
        }
        if (body) {
            const auto& b = e.components["celestial_body"];
            render::CelestialBody cb;
            const math::Vec3 forward = math::transform_vector(world, {0, 0, -1}), up = math::transform_vector(world, {0, 1, 0});
            cb.direction = math::length(forward) > 1e-6f ? math::normalize(forward) : math::Vec3{0, 1, 0};
            cb.angular_radius = math::radians(std::clamp(number(b, "angular_size", 2.0f), 0.01f, 170.0f)) * 0.5f;
            cb.albedo = color_of(b, {0.5f, 0.5f, 0.5f});
            cb.ring_axis = math::length(up) > 1e-6f ? math::normalize(up) : math::Vec3{0, 1, 0};
            if (b.contains("rings") && b["rings"].is_object()) {
                const auto& r = b["rings"];
                // The entity's +Y is square to the line of sight (its forward), which would show the rings edge-on:
                // "tilt" leans their axis towards the viewer (0 edge-on, 90 face-on).
                const float tilt = math::radians(std::clamp(number(r, "tilt", 25.0f), 0.0f, 90.0f));
                cb.ring_axis = math::normalize(cb.ring_axis * std::cos(tilt) - cb.direction * std::sin(tilt));
                cb.ring_inner = std::clamp(number(r, "inner", 1.3f), 1.0f, 20.0f);
                cb.ring_outer = std::clamp(number(r, "outer", 2.3f), cb.ring_inner, 20.0f);
                cb.ring_color = color_of(r, cb.ring_color);
            }
            bodies.push_back(cb);
        }
        if (local) {
            const auto& l = e.components["light"];
            render::LocalLight light;
            light.type = light_type == "spot" ? render::LocalLight::Type::Spot : render::LocalLight::Type::Point;
            light.position = world.transform_point({0, 0, 0});
            const math::Vec3 dir = math::transform_vector(world, {0, 0, -1});
            if (math::length(dir) > 1e-6f) light.direction = math::normalize(dir);
            light.color = color_of(l, {1, 1, 1});
            light.lumens = std::clamp(number(l, "intensity", 800.0f), 0.0f, 1.0e7f);
            light.range = std::clamp(number(l, "range", 10.0f), 0.01f, 1000.0f);
            const float outer = std::clamp(number(l, "angle", 30.0f), 1.0f, 89.0f);
            light.outer_angle = math::radians(outer);
            light.inner_angle = math::radians(std::clamp(number(l, "inner_angle", outer * 0.75f), 0.0f, outer - 0.1f));
            view.lighting.lights.push_back(light);
            view.light_markers.push_back({light.position, light.color, e.id});
        }
        if (primitive) {
            const auto& p = e.components["primitive"];
            const std::string shape = p.value("shape", std::string("box"));
            SceneView::Primitive prim{SceneView::Primitive::Shape::Box, world, 0.5f, 2.0f, color_of(p), e.id, {}, {}};
            if (const auto mat = asset::guid_field(p, "material"); mat && asset_path) {
                if (auto file = asset_path(*mat)) prim.material_file = std::move(*file);
                if (!p.contains("color")) prim.color = {1, 1, 1};  // the material's own colours, untinted
            } else {
                asset::apply_material_fields(p, prim.material, file_resolver(asset_path), nullptr);
            }
            if (shape == "sphere") {
                prim.shape = SceneView::Primitive::Shape::Sphere;
                const float r = number(p, "radius", 0.5f);
                prim.world = world * math::scale({r * 2, r * 2, r * 2});
            } else if (shape == "capsule") {
                prim.shape = SceneView::Primitive::Shape::Capsule;
                prim.radius = number(p, "radius", 0.3f);
                prim.height = number(p, "height", 1.8f);
            } else {
                math::Vec3 size{1, 1, 1};
                if (p.contains("size") && p["size"].is_array() && p["size"].size() == 3 && p["size"][0].is_number() &&
                    p["size"][1].is_number() && p["size"][2].is_number())
                    size = {p["size"][0].get<float>(), p["size"][1].get<float>(), p["size"][2].get<float>()};
                if (shape == "plane") size.y = 0.02f;
                prim.world = world * math::scale(size);
            }
            view.primitives.push_back(prim);
        }
        if (sprite && asset_path) {
            const auto& sp = e.components["sprite"];
            if (const auto id = asset::guid_field(sp, "sheet"))
                if (auto file = asset_path(*id)) view.sprites.push_back({e.id, std::move(*file), sp, world});
        }
        if (asset_path && e.has("tilemap") && e.components["tilemap"].is_object()) {  // 2D part 4
            const auto& tm = e.components["tilemap"];
            if (const auto id = asset::guid_field(tm, "tileset"))
                if (auto file = asset_path(*id)) {
                    Json settings = Json::object();
                    for (auto it = tm.begin(); it != tm.end(); ++it)
                        if (it.key() != "rows") settings[it.key()] = it.value();
                    view.tilemaps.push_back({e.id, std::move(*file), std::move(settings), tile_grid(tm), world});
                }
        }
        if (e.has("terrain") && e.components["terrain"].is_object()) {
            const auto& t = e.components["terrain"];
            SceneView::Terrain terrain{e.id, world.transform_point({0, 0, 0}), terrain_settings(t, file_resolver(asset_path), project_root)};
            // Material layers: "layers": [{"material": id, "tiling": n}, ...] (up to four); without
            // them, "material" is the only layer.
            auto file_of = [&](const Json& j, const char* key) -> std::filesystem::path {
                const auto id = asset::guid_field(j, key);
                if (!id || !asset_path) return {};
                auto file = asset_path(*id);
                return file ? *file : std::filesystem::path{};
            };
            terrain.layer_files[0] = file_of(t, "material");
            if (t.contains("layers") && t["layers"].is_array()) {
                for (std::size_t li = 0; li < std::min<std::size_t>(4, t["layers"].size()); ++li) {
                    const Json& layer = t["layers"][li];
                    if (!layer.is_object()) continue;
                    if (layer.contains("material") && !layer["material"].is_null()) terrain.layer_files[li] = file_of(layer, "material");
                    if (layer.contains("tiling") && layer["tiling"].is_number()) {
                        const float tiling = std::clamp(layer["tiling"].get<float>(), 0.001f, 1000.0f);
                        (li == 0 ? terrain.tiling.x : li == 1 ? terrain.tiling.y : li == 2 ? terrain.tiling.z : terrain.tiling.w) = tiling;
                    }
                }
            }
            terrain.sculpt = asset::guid_field(t, "sculpt");
            terrain.paint = asset::guid_field(t, "paint");
            view.terrains.push_back(std::move(terrain));
        }
        if (e.has("csg_model")) {
            const auto model_index = static_cast<std::size_t>(&e - entities.data());
            view.csg.push_back({e.id, world, csg_model_key(scene, model_index)});
        }
        if (model && asset_path) {
            if (const auto guid = asset::guid_field(e.components["model"], "asset"))
                if (const auto& path = asset_file(*guid)) view.models.push_back({e.id, *path, world});
        }
        if (mesh && asset_path) {
            const auto& m = e.components["mesh"];
            const auto guid = asset::guid_field(m, "asset");
            if (!guid) continue;
            if (const auto& path = asset_file(*guid)) {
                const int root = model_root_of(i);
                bool same = false;
                if (root >= 0) {
                    const auto r = static_cast<std::size_t>(root);
                    auto it = root_asset.find(r);
                    if (it == root_asset.end()) it = root_asset.emplace(r, asset::guid_field(entities[r].components["model"], "asset")).first;
                    same = it->second == guid;
                }
                SceneView::Mesh out{*path, m.value("mesh_index", -1), world, e.id, same ? entities[static_cast<std::size_t>(root)].id : Guid{}};
                // Skinned (8C): its joints' entities, posed wherever they are now (render poses included).
                if (m.contains("joints") && m["joints"].is_array() && m.contains("skin") && m["skin"].is_number_integer()) {
                    out.skin = m["skin"].get<int>();
                    out.joints.reserve(m["joints"].size());
                    for (const auto& j : m["joints"]) {
                        const auto id = Guid::parse(j.is_string() ? j.get<std::string>() : std::string());
                        const auto it = id ? index.find(*id) : index.end();
                        if (it == index.end()) {  // a joint was deleted: drawn as it was bound
                            out.joints.clear();
                            out.skin = -1;
                            break;
                        }
                        out.joints.push_back(world_of(it->second, 0));
                    }
                }
                view.meshes.push_back(std::move(out));
            }
        }
    }
    // Scattered instances (Milestone 7, part 7): the cooked output with its hand-made layer, standing on
    // their terrain if they name one.
    for (const std::size_t si : scatter_entities) {
        const auto& e = entities[si];
        const auto& comp = e.components["scatter"];
        SceneView::Scatter sv;
        sv.entity = e.id;
        sv.draw_distance = std::max(0.0f, number(comp, "draw_distance", 0.0f));
        sv.cast_shadows = comp.value("shadows", true);
        const math::Mat4 world = world_of(si, 0);
        std::shared_ptr<const terrain::Heights> ground;
        math::Vec3 ground_at{};
        if (const auto gid = asset::guid_field(comp, "ground")) {
            for (const auto& t : view.terrains) {
                if (t.entity != *gid) continue;
                if (t.heights) {
                    ground = t.heights;
                } else if (auto h = terrain::Heights::shared(t.settings)) {
                    ground = *h;
                }
                ground_at = t.position;
            }
        }
        const auto placement = place_scatter_cached(e.id, comp, world, project_root, ground, ground_at, &sv.warning);
        for (const auto& group : placement->groups) {
            const auto& path = asset_path ? asset_file(group.asset) : std::optional<std::filesystem::path>{};
            if (!path) continue;
            sv.groups.push_back({*path, group.worlds});
            sv.count += group.worlds->size();
        }
        view.scatters.push_back(std::move(sv));
    }
    // The time of day places the sun (over the sun light's own direction): north is -Z, turned by "north".
    if (time_of_day) {
        const math::Vec3 s = asset::solar_direction(time_of_day->hours, time_of_day->latitude, time_of_day->day);
        const float a = math::radians(time_of_day->north), c = std::cos(a), sn = std::sin(a);
        const math::Vec3 to_sun{c * s.x + sn * s.z, s.y, -sn * s.x + c * s.z};
        view.lighting.sun_direction = to_sun * -1.0f;
    }
    // The environment: the atmosphere, an HDRI, or a procedural sky from the sun's sky settings (or the defaults).
    if (view.environment.atmosphere) {
        const math::Vec3 to_sun = math::normalize(view.lighting.sun_direction * -1.0f);
        view.environment.to_sun = to_sun;
        // The sun's light after the air it crosses: its own colour (white unless given) times the
        // transmittance; the sky's radiance is relative to the sun's illuminance outside the atmosphere.
        const math::Vec3 own = sun_color_given ? view.lighting.sun_color : math::Vec3{1, 1, 1};
        // A bracket, not the exact settings: a weather blend moves them every frame (see Atmosphere::Bracket).
        const auto atmosphere = asset::Atmosphere::bracket(view.environment.atmosphere_settings);
        const math::Vec3 t = atmosphere.sun_transmittance(to_sun);
        view.lighting.sun_color = {own.x * t.x, own.y * t.y, own.z * t.z};
        view.lighting.environment_intensity = view.lighting.sun_illuminance;
        view.lighting.environment_rotation = 0.0f;  // built in world space, around the real sun
        view.lighting.sun_disc = true;
        // Part 5: the sky's sun disc, stars and the moon.
        render::SkyObjects& sky = view.lighting.sky;
        const float sun_lux = view.lighting.sun_illuminance;
        sky.sun_direction = to_sun;
        sky.sun_outer = own * sun_lux;
        sky.sun_seen = view.lighting.sun_color * sun_lux;
        const float observer_km = view.environment.atmosphere_settings.altitude_m * 0.001f;
        sky.zenith_transmittance = atmosphere.transmittance(observer_km, 1.0f);
        sky.stars = night.stars;
        sky.star_brightness = night.star_brightness;
        sky.star_seed = night.star_seed;
        if (time_of_day) {
            sky.latitude = time_of_day->latitude;
            sky.sidereal_hours = time_of_day->hours + time_of_day->day * (24.0f / 365.25f);  // a little further each day
        }
        if (time_of_day && night.moon) {
            const float phase = night.moon_phase ? *night.moon_phase : asset::default_moon_phase(time_of_day->day);
            const math::Vec3 m = asset::lunar_direction(time_of_day->hours, time_of_day->latitude, time_of_day->day, phase);
            const float a = math::radians(time_of_day->north), c = std::cos(a), sn = std::sin(a);
            const math::Vec3 to_moon{c * m.x + sn * m.z, m.y, -sn * m.x + c * m.z};
            render::CelestialBody moon;
            moon.direction = to_moon;
            moon.angular_radius = asset::kMoonAngularRadius;
            moon.albedo = math::Vec3{1.0f, 0.97f, 0.92f} * asset::kMoonAlbedo;
            bodies.insert(bodies.begin(), moon);
            // Its light: the angle sun-moon-earth (the sun is far: pi minus the angle between them in our sky).
            const float phase_angle = math::kPi - std::acos(std::clamp(math::dot(to_sun, to_moon), -1.0f, 1.0f));
            const float moon_lux = asset::moon_illuminance(sun_lux, phase_angle);
            sky.moon_direction = to_moon;
            sky.moon_relative = sun_lux > 0.0f ? moon_lux / sun_lux : 0.0f;
            // When the moon outshines the sun on the ground, it becomes the light that casts shadows.
            const math::Vec3 t_moon = atmosphere.sun_transmittance(to_moon);
            const auto luminance = [](math::Vec3 v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; };
            if (to_moon.y > 0.0f && moon_lux * luminance(t_moon) > sun_lux * luminance(view.lighting.sun_color)) {
                view.lighting.sun_direction = to_moon * -1.0f;
                view.lighting.sun_color = {own.x * t_moon.x, own.y * t_moon.y * 0.97f, own.z * t_moon.z * 0.92f};  // a touch warm
                view.lighting.sun_illuminance = moon_lux;
            }
        }
    } else if (view.environment.space) {
        // No air: a black sky with stars; the sun as it is.
        const math::Vec3 to_sun = math::normalize(view.lighting.sun_direction * -1.0f);
        if (!sun_color_given) view.lighting.sun_color = {1, 1, 1};
        render::SkyObjects& sky = view.lighting.sky;
        sky.space = true;
        sky.sun_direction = to_sun;
        sky.sun_outer = sky.sun_seen = view.lighting.sun_color * view.lighting.sun_illuminance;
        sky.stars = night.stars;
        sky.star_brightness = night.star_brightness;
        sky.star_seed = night.star_seed;
        sky.latitude = 90.0f;
        view.lighting.environment_intensity = 0.0f;
        view.lighting.environment_rotation = 0.0f;
        view.lighting.sun_disc = true;
    } else if (view.environment.hdri.empty()) {
        const math::Vec3 sky = view.lighting.sky_color;
        view.environment.zenith = sky;
        view.environment.horizon = (sky + math::Vec3{1, 1, 1}) * 0.55f;  // paler and brighter near the horizon
        view.environment.ground = view.lighting.ground_color;
        view.lighting.environment_intensity = view.lighting.sky_luminance;
    } else {
        view.lighting.environment_intensity = env_intensity;
        // The photo has its own sun: no default sun on top of it (add a directional light to get
        // sharp sun shadows, then match its direction to the sun in the photo).
        if (!view.has_sun) view.lighting.sun_illuminance = 0.0f;
    }
    // Planets and moons light themselves from the real sun (outside any air): for skies other than the atmosphere
    // and space, that's the sun light as given.
    render::SkyObjects& sky_objects = view.lighting.sky;
    if (!view.environment.atmosphere && !view.environment.space) {
        sky_objects.sun_direction = math::normalize(view.lighting.sun_direction * -1.0f);
        sky_objects.sun_outer = sky_objects.sun_seen = view.lighting.sun_color * view.lighting.sun_illuminance;
    }
    sky_objects.bodies = std::move(bodies);
    if (sky_objects.bodies.size() > render::kMaxCelestialBodies) sky_objects.bodies.resize(render::kMaxCelestialBodies);
    if (environment_ev100) {
        if (environment_ev100->is_number()) {
            view.lighting.ev100 = std::clamp(environment_ev100->get<float>(), -10.0f, 25.0f);
            auto_exposure = false;
        } else if (environment_ev100->get<std::string>() == "auto") {
            auto_exposure = true;
        }
    }
    // Automatic exposure (part 5): meter the light falling on level ground, like an incident light meter.
    if (auto_exposure) {
        const auto luminance = [](math::Vec3 v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; };
        // Clouds take their share of the direct light (the covered fraction lets through e^-density) and send part
        // of it down diffused (render::cloud_diffuse_transmittance: the same model the clouds are shaded with).
        float cloud_t = 1.0f, diffused = 1.0f;
        for (const auto& layer : view.lighting.clouds)
            if (layer.enabled) {
                cloud_t *= 1.0f - layer.coverage + layer.coverage * std::exp(-layer.density);
                diffused *= render::cloud_diffuse_transmittance(layer.density);
            }
        const float light_up = std::max(0.0f, -view.lighting.sun_direction.y);
        const float direct = view.lighting.sun_illuminance * luminance(view.lighting.sun_color) * light_up;
        // Clouds don't destroy the light they block: part of it comes down through them, diffused, which is why an
        // overcast day is still bright.
        float lux = direct * cloud_t + direct * (1.0f - cloud_t) * diffused;
        for (const auto& l : view.lighting.lights)
            if (l.type == render::LocalLight::Type::Directional) lux += l.lux * luminance(l.color) * std::max(0.0f, -l.direction.y);
        if (view.environment.atmosphere) {
            const auto atmosphere = asset::Atmosphere::bracket(view.environment.atmosphere_settings);
            const float sun_lux = luminance(sky_objects.sun_outer);
            lux += atmosphere.sky_illuminance(std::asin(std::clamp(view.environment.to_sun.y, -1.0f, 1.0f))) * sun_lux;
            if (sky_objects.moon_relative > 0.0f)
                lux += atmosphere.sky_illuminance(std::asin(std::clamp(sky_objects.moon_direction.y, -1.0f, 1.0f))) *
                       sky_objects.moon_relative * sun_lux;
            lux += 0.002f;  // starlight and airglow: a moonless night isn't pitch black
        } else if (!view.environment.space) {
            // A rough figure for the other skies: the sky's radiance over a hemisphere.
            lux += view.lighting.environment_intensity * math::kPi * (view.environment.hdri.empty() ? luminance(view.lighting.sky_color) : 0.5f);
        }
        view.lighting.ev100 = render::auto_ev100(lux);
    }
    // The water's waves in the ground wind (the same wind as the weather above, and as WaterSystem's for gameplay).
    const float water_wind = math::length(view.lighting.weather.wind);
    for (auto& [body, look] : pending_water) {
        body.waves = make_waves(look, water_wind);
        if (body.river) body.waves.height = 0.0f;  // rivers run, they don't swell (8D part 6)
        view.water.push_back(water_draw(body, look));
        view.water.back().rain = view.lighting.weather.rain;  // the weather on the surface (8D part 5): drawing only
        view.water.back().wind_speed = water_wind;
        if (const auto under = asset::underwater_look(water_environment, look))
            view.water.back().underwater = render::WaterDraw::Underwater{under->absorption, under->scatter, under->caustics, under->shafts};
    }
    if (!view.water.empty()) set_water_depths(view.water, view.terrains);
    return view;
}

void set_water_depths(std::span<render::WaterDraw> draws, const std::vector<SceneView::Terrain>& terrains) {
    if (draws.empty()) return;
    std::vector<ParticleGround::Piece> ground;
    for (const auto& t : terrains) {
        std::shared_ptr<const terrain::Heights> h = t.heights;
        if (!h)
            if (auto shared = terrain::Heights::shared(t.settings)) h = *shared;
        if (h) ground.push_back({t.position, h});
    }
    std::ranges::sort(ground, [](const auto& a, const auto& b) { return std::tie(a.position.x, a.position.z) < std::tie(b.position.x, b.position.z); });
    set_water_depths(draws, ground);
}

asset::AssetFileResolver file_resolver(const AssetPath& asset_path) {
    return [asset_path](const Guid& id) -> std::optional<stdfs::path> { return asset_path ? asset_path(id) : std::nullopt; };
}

AssetPath registry_asset_path(const asset::Project& project, const asset::AssetRegistry& registry) {
    return [&project, &registry](const Guid& id) -> std::optional<stdfs::path> {
        const auto* rec = registry.find(id);
        if (!rec) return std::nullopt;
        auto path = project.resolve(rec->path);
        return path ? std::optional(*path) : std::nullopt;
    };
}

math::Mat4 camera_view(const math::Mat4& m) {
    const math::Vec3 eye = m.transform_point({0, 0, 0});
    const math::Vec3 fwd = math::normalize(math::transform_vector(m, {0, 0, -1}));
    math::Vec3 right = math::cross(fwd, {0, 1, 0});
    right = math::length(right) < 1e-4f ? math::normalize(math::transform_vector(m, {1, 0, 0})) : math::normalize(right);
    return math::look_at(eye, eye + fwd, math::cross(right, fwd));
}

render::DrawItem terrain_draw(const SceneView::Terrain& terrain, render::MeshHandle tile,
                              const std::function<render::MaterialHandle(const std::filesystem::path&)>& material) {
    auto handle = [&](const std::filesystem::path& file) { return file.empty() ? render::MaterialHandle{} : material(file); };
    render::DrawItem item{tile, math::translation(terrain.position)};
    item.material = handle(terrain.layer_files[0]);
    item.splat = true;  // always: the vertex colours are weights (unpainted: all layer 0)
    for (std::size_t i = 0; i < 3; ++i) item.splat_layers[i] = handle(terrain.layer_files[i + 1]);
    item.splat_tiling = terrain.tiling;
    return item;
}

std::filesystem::path cooked_graph_folder(const std::filesystem::path& root, const Guid& graph) {
    return root / ".noor" / "cooked" / "graphs" / graph.to_string();
}

terrain::Settings terrain_settings(const Json& component, const asset::AssetFileResolver& resolve, const std::filesystem::path& project_root) {
    auto s = terrain::Settings::from_json(component, resolve);
    if (s.base == terrain::Settings::Base::Field && s.graph && !project_root.empty()) {
        s.base_field = cooked_graph_folder(project_root, *s.graph) / (s.graph_output + ".noorfield");
        (void)s.adopt_field_shape();  // missing or damaged: Heights::build says so where the heights are needed
    }
    return s;
}

} // namespace noor::game
