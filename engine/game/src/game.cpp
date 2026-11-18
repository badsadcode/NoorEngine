#include "noor/game/game.h"
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
#include <map>
#include <cmath>
#include <string>
#include <unordered_map>

namespace noor::game {

namespace stdfs = std::filesystem;
using asset::Json;

Result<std::unique_ptr<runtime::World>> make_world(const asset::Project& project, const asset::AssetRegistry& registry,
                                                   scene::PrefabLibrary& prefabs, const scene::Scene& scene,
                                                   const WorldOptions& options) {
    auto input_map = runtime::InputMap::load(project.root() / "Settings" / "input.json");
    if (!input_map) return std::unexpected(input_map.error());
    auto world = std::make_unique<runtime::World>(scene::expand_prefabs(scene, prefabs.resolver()).scene, std::move(*input_map));

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
            auto imported = asset::import_gltf(*path);
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
    // Scattered trees collide by their trunks (Milestone 7, part 8): each model's collision capsule (a
    // `capsule` collision node, Noor Interchange 3.5) at every instance, standing on the same ground the
    // renderer puts them on. Models without a capsule don't collide.
    physics_options.scatter_capsules = [&project, &registry, cache, live = options.terrain_heights](const scene::Entity& entity,
                                                                                                  const runtime::World& w) {
        std::vector<physics::StaticCapsule> out;
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
        struct Trunk {
            bool known = false;
            std::optional<physics::StaticCapsule> local;  // in the model's space
        };
        std::map<Guid, Trunk> trunks;
        for (const auto& placed : place_scatter(items, world_m, ground.get(), ground_at)) {
            Trunk& t = trunks[placed.asset];
            if (!t.known) {
                t.known = true;
                const auto* rec = registry.find(placed.asset);
                auto path = rec ? project.resolve(rec->path) : Result<stdfs::path>(fail(ErrorCode::NotFound, "unknown asset"));
                if (path) {
                    auto& model = (*cache)[path->generic_string()];
                    if (!model)
                        if (auto imported = asset::import_gltf(*path)) model = std::make_shared<asset::ImportedModel>(std::move(*imported));
                    if (model)
                        for (std::size_t n = 0; n < model->nodes.size(); ++n) {
                            const auto& node = model->nodes[n];
                            if (!node.is_collision() || node.noor.value("collision", std::string()) != "capsule") continue;
                            t.local = physics::StaticCapsule{model->world_matrix(n).transform_point({0, 0, 0}), node.noor.value("radius", 0.3f),
                                                             node.noor.value("height", 2.0f)};
                            break;
                        }
                }
            }
            if (!t.local) continue;
            const float s = math::length(math::Vec3{placed.world.m[0].x, placed.world.m[0].y, placed.world.m[0].z});
            out.push_back({placed.world.transform_point(t.local->centre), t.local->radius * s, t.local->height * s});
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
    world->add_system(std::make_unique<runtime::BehavioursSystem>());
    return world;
}

namespace {

float number(const Json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
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

} // namespace

SceneView collect_view(const scene::Scene& scene, const AssetPath& asset_path, const std::unordered_map<Guid, math::Mat4>* render_poses,
                       const std::filesystem::path& project_root) {
    SceneView view;
    const auto& entities = scene.entities;
    bool has_environment = false;
    float env_intensity = 8000.0f;
    // World matrices, with render poses in place of the simulated ones (and children following).
    // Each is computed once and kept (a parent's matrix is reused by all its children): the scene's
    // own world_matrix() searches for every parent, which is quadratic and took 190 ms a frame for a
    // 2,000-tree forest (Milestone 7, part 6).
    std::unordered_map<Guid, std::size_t> index;
    index.reserve(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i) index.emplace(entities[i].id, i);
    std::vector<std::optional<math::Mat4>> memo(entities.size());
    const bool blended = render_poses && !render_poses->empty();
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
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        const bool camera = !view.camera && e.has("camera") && e.components["camera"].is_object();
        const bool primitive = e.has("primitive") && e.components["primitive"].is_object();
        const bool mesh = e.has("mesh");
        const bool is_light = e.has("light") && e.components["light"].is_object();
        const std::string light_type = is_light ? e.components["light"].value("type", std::string("directional")) : "";
        const bool sun = is_light && !view.has_sun && light_type == "directional";
        const bool local = is_light && (light_type == "point" || light_type == "spot");
        const bool env = !has_environment && e.has("environment") && e.components["environment"].is_object();
        const bool model = e.has("model") && e.components["model"].is_object();
        const bool scatter = e.has("scatter") && e.components["scatter"].is_object();
        if (scatter) scatter_entities.push_back(i);
        if (!camera && !primitive && !mesh && !model && !sun && !local && !env && !e.has("csg_model") && !e.has("terrain")) continue;
        if (env) {
            const auto& en = e.components["environment"];
            if (const auto id = asset::guid_field(en, "hdri"); id && asset_path)
                if (auto file = asset_path(*id)) view.environment.hdri = std::move(*file);
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
            view.lighting.ev100 = std::clamp(number(cam, "ev100", view.lighting.ev100), -10.0f, 25.0f);
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
                view.meshes.push_back({*path, m.value("mesh_index", -1), world, e.id, same ? entities[static_cast<std::size_t>(root)].id : Guid{}});
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
        const auto items = scatter_items(comp, project_root, &sv.warning);
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
        std::map<Guid, std::size_t> group_of;
        for (const auto& placed : place_scatter(items, world, ground.get(), ground_at)) {
            const auto& path = asset_path ? asset_file(placed.asset) : std::optional<std::filesystem::path>{};
            if (!path) continue;
            auto [g, fresh] = group_of.try_emplace(placed.asset, sv.groups.size());
            if (fresh) sv.groups.push_back({*path, {}});
            sv.groups[g->second].worlds.push_back(placed.world);
            ++sv.count;
        }
        view.scatters.push_back(std::move(sv));
    }
    // The environment: an HDRI, or a procedural sky from the sun's sky settings (or the defaults).
    if (view.environment.hdri.empty()) {
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
    return view;
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
