#include "noor/game/game.h"

#include "noor/asset/gltf_import.h"
#include "noor/asset/json.h"
#include "noor/audio/audio.h"
#include "noor/foundation/log.h"
#include "noor/physics/physics.h"
#include "noor/runtime/behaviours.h"
#include "noor/scripting/csharp_scripts.h"
#include "noor/scripting/lua_scripts.h"

#include <algorithm>
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

math::Vec3 color_of(const Json& primitive) {
    math::Vec3 color{0.55f, 0.52f, 0.62f};
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

SceneView collect_view(const scene::Scene& scene, const AssetPath& asset_path, const std::unordered_map<Guid, math::Mat4>* render_poses) {
    SceneView view;
    const auto& entities = scene.entities;
    // World matrices, with render poses in place of the simulated ones (and children following).
    std::unordered_map<Guid, std::size_t> index;
    std::vector<std::optional<math::Mat4>> memo;
    const bool blended = render_poses && !render_poses->empty();
    if (blended) {
        for (std::size_t i = 0; i < entities.size(); ++i) index.emplace(entities[i].id, i);
        memo.resize(entities.size());
    }
    std::function<math::Mat4(std::size_t, int)> world_of = [&](std::size_t i, int depth) -> math::Mat4 {
        if (!blended) return scene.world_matrix(i);
        if (memo[i]) return *memo[i];
        math::Mat4 m;
        if (const auto it = render_poses->find(entities[i].id); it != render_poses->end()) {
            m = it->second;
        } else {
            m = entities[i].transform.matrix();
            const auto parent = entities[i].parent ? index.find(*entities[i].parent) : index.end();
            if (parent != index.end() && depth < 256) m = world_of(parent->second, depth + 1) * m;  // depth: cycle guard
        }
        memo[i] = m;
        return m;
    };
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        const bool camera = !view.camera && e.has("camera") && e.components["camera"].is_object();
        const bool primitive = e.has("primitive") && e.components["primitive"].is_object();
        const bool mesh = e.has("mesh");
        if (!camera && !primitive && !mesh) continue;
        const math::Mat4 world = world_of(i, 0);
        if (camera) {
            const auto& cam = e.components["camera"];
            view.camera = SceneView::Camera{world, std::clamp(number(cam, "fov", 70.0f), 10.0f, 150.0f),
                                            std::max(0.001f, number(cam, "near", 0.05f)), e.id};
        }
        if (primitive) {
            const auto& p = e.components["primitive"];
            const std::string shape = p.value("shape", std::string("box"));
            SceneView::Primitive prim{SceneView::Primitive::Shape::Box, world, 0.5f, 2.0f, color_of(p), e.id};
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
        if (mesh && asset_path) {
            const auto& m = e.components["mesh"];
            const auto guid = asset::guid_field(m, "asset");
            if (!guid) continue;
            if (auto path = asset_path(*guid)) view.meshes.push_back({std::move(*path), m.value("mesh_index", -1), world, e.id});
        }
    }
    return view;
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

} // namespace noor::game
