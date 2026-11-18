#include "noor/game/scene_renderer.h"
#include "noor/game/cooked_csg.h"

#include "noor/asset/gltf_import.h"
#include "noor/foundation/log.h"

#include <algorithm>

namespace noor::game {

SceneRenderer::SceneRenderer(render::Renderer& renderer, std::filesystem::path texture_cache, asset::AssetFileResolver resolve)
    : renderer_(renderer), textures_(renderer, texture_cache), materials_(renderer, textures_, resolve),
      environment_(renderer, std::move(texture_cache)), resolve_(std::move(resolve)) {
    box_ = upload(make_box({1, 1, 1}, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}), "Primitive box");
    sphere_ = upload(make_round(0.5f, 1.0f), "Primitive sphere");
}

SceneRenderer::~SceneRenderer() {
    auto destroy = [&](render::MeshHandle h) {
        if (h.valid()) renderer_.destroy_mesh(h);
    };
    destroy(box_);
    destroy(sphere_);
    for (const auto& [id, c] : cooked_) destroy(c.mesh);
    for (const auto& [id, t] : terrains_)
        for (const auto h : t.tiles) destroy(h);
    for (const auto& [key, h] : capsules_) destroy(h);
    for (const auto& [path, model] : models_) {
        if (model)
            for (const auto h : model->meshes) destroy(h);
    }
}

render::MeshHandle SceneRenderer::upload(const Geometry& g, const char* name) {
    auto h = renderer_.create_mesh({g.first, g.second, name});
    if (!h) {
        NOOR_LOG_ERROR("Game", "Can't create mesh '{}': {}", name, h.error().describe());
        return {};
    }
    return *h;
}

const SceneRenderer::Model* SceneRenderer::model(const std::filesystem::path& file) {
    const std::string key = file.generic_string();
    auto it = models_.find(key);
    if (it == models_.end()) {
        std::optional<Model> loaded;
        if (auto model = asset::import_gltf(file)) {
            loaded.emplace();
            for (const auto& mesh : model->meshes) {
                auto h = renderer_.create_mesh({{reinterpret_cast<const render::Vertex*>(mesh.vertices.data()), mesh.vertices.size()},
                                                mesh.indices, mesh.name});
                loaded->meshes.push_back(h ? *h : render::MeshHandle{});
                loaded->submeshes.push_back(mesh.submeshes);
            }
            loaded->materials = materials_.model(file, *model);
            for (std::size_t n = 0; n < model->nodes.size(); ++n)
                if (model->nodes[n].mesh >= 0 && !model->nodes[n].is_collision()) loaded->nodes.emplace_back(model->nodes[n].mesh, model->world_matrix(n));
        } else {
            NOOR_LOG_WARN("Game", "{}", model.error().describe());
        }
        it = models_.emplace(key, std::move(loaded)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void SceneRenderer::add_mesh(std::vector<render::DrawItem>& out, const Model& m, int mesh_index, const math::Mat4& world) const {
    if (mesh_index < 0 || mesh_index >= static_cast<int>(m.meshes.size())) return;
    const auto index = static_cast<std::size_t>(mesh_index);
    const auto h = m.meshes[index];
    if (!h.valid()) return;
    for (const auto& sm : m.submeshes[index]) {  // one draw per material
        render::DrawItem item{h, world};
        item.first_index = sm.first_index;
        item.index_count = sm.index_count;
        if (sm.material >= 0 && sm.material < static_cast<int>(m.materials.size())) item.material = m.materials[static_cast<std::size_t>(sm.material)];
        out.push_back(item);
    }
}

std::vector<render::DrawItem> SceneRenderer::draws(const SceneView& view) {
    environment_.apply(view.environment);
    std::vector<render::DrawItem> out;
    out.reserve(view.meshes.size() + view.primitives.size());
    // Levels of detail (Milestone 7, part 6): a model far enough away is drawn whole from a lower
    // level's file, in place of its own node entities.
    const render::Camera cam = camera(view);
    const math::Vec3 eye = math::inverse(cam.view).transform_point({0, 0, 0});
    std::unordered_map<Guid, int> lowered;  // model root -> level (> 0)
    for (const auto& md : view.models) {
        const std::string key = md.asset_path.generic_string();
        auto lit = lods_.find(key);
        if (lit == lods_.end()) lit = lods_.emplace(key, read_model_lods(md.asset_path)).first;
        const ModelLods& lods = lit->second;
        if (!lods.switches()) continue;
        const math::Mat4& w = md.world;
        const float scale = std::max({math::length(math::Vec3{w.m[0].x, w.m[0].y, w.m[0].z}), math::length(math::Vec3{w.m[1].x, w.m[1].y, w.m[1].z}),
                                      math::length(math::Vec3{w.m[2].x, w.m[2].y, w.m[2].z})});
        const int level = choose_lod(lods, math::length(w.transform_point({0, 0, 0}) - eye), cam.fov_y, scale);
        if (level <= 0) continue;
        const Model* lm = model(lods.levels[static_cast<std::size_t>(level)].file);
        if (!lm) continue;  // the level failed to load: keep drawing level 0
        lowered.emplace(md.entity, level);
        for (const auto& [mesh_index, local] : lm->nodes) add_mesh(out, *lm, mesh_index, w * local);
    }
    for (const auto& m : view.meshes) {
        if (!m.model_root.is_nil() && lowered.contains(m.model_root)) continue;
        if (const Model* md = model(m.asset_path)) add_mesh(out, *md, m.mesh_index, m.world);
    }
    for (const auto& t : view.terrains) {
        TerrainMeshes& tm = terrains_[t.entity];
        const std::string key = t.settings.key();
        if (tm.key != key) {  // new, or its settings changed
            for (const auto h : tm.tiles)
                if (h.valid()) renderer_.destroy_mesh(h);
            tm = {key, {}, false};
            if (auto heights = terrain::Heights::shared(t.settings)) {
                for (std::uint32_t tz = 0; tz < t.settings.tiles_z; ++tz)
                    for (std::uint32_t tx = 0; tx < t.settings.tiles_x; ++tx) {
                        const auto mesh = terrain::tile_mesh(**heights, tx, tz);
                        auto h = renderer_.create_mesh({{mesh.vertices.data(), mesh.vertices.size()}, mesh.indices, "Terrain tile"});
                        tm.tiles.push_back(h ? *h : render::MeshHandle{});
                    }
            } else {
                NOOR_LOG_WARN("Game", "Terrain {}: {}", t.entity.to_string(), heights.error().describe());
            }
        }
        for (const auto h : tm.tiles) {
            if (!h.valid()) continue;
            out.push_back(terrain_draw(t, h, [&](const std::filesystem::path& f) { return materials_.file(f); }));
        }
    }
    for (const auto& c : view.csg) {
        CookedModel& cm = cooked_[c.entity];
        const auto file = cooked_csg_path(cooked_root_, c.entity);
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (!cm.loaded || (!ec && time != cm.time)) {  // first time, or cooked again (the editor saved)
            if (cm.mesh.valid()) renderer_.destroy_mesh(cm.mesh);
            cm = {};
            cm.loaded = true;
            cm.time = time;
            if (auto cooked = read_cooked_csg(file)) {
                cm.key = cooked->key;
                if (auto h = renderer_.create_mesh({{cooked->vertices.data(), cooked->vertices.size()}, cooked->indices, "CSG model"})) cm.mesh = *h;
                NOOR_LOG_DEBUG("Game", "CSG model {}: {} triangles, {} material(s)", c.entity.to_string(), cooked->indices.size() / 3,
                               cooked->submeshes.size());
                for (const auto& s : cooked->submeshes) {
                    cm.ranges.emplace_back(s.first_index, s.index_count);
                    std::optional<std::filesystem::path> path = s.material && resolve_ ? resolve_(*s.material) : std::nullopt;
                    cm.materials.push_back(path ? materials_.file(*path) : render::MaterialHandle{});
                }
            } else {
                NOOR_LOG_WARN("Game", "CSG model {} isn't cooked ({}): save its scene in the editor, or run noor_cli cook", c.entity.to_string(),
                              cooked.error().describe());
                cm.warned = true;
            }
        }
        if (cm.mesh.valid() && cm.key != c.key && !cm.warned) {
            NOOR_LOG_WARN("Game", "CSG model {} was cooked before its last change: save its scene in the editor, or run noor_cli cook",
                          c.entity.to_string());
            cm.warned = true;
        }
        if (!cm.mesh.valid()) continue;
        for (std::size_t i = 0; i < cm.ranges.size(); ++i) {
            render::DrawItem item{cm.mesh, c.world};
            item.first_index = cm.ranges[i].first;
            item.index_count = cm.ranges[i].second;
            item.material = cm.materials[i];
            out.push_back(item);
        }
    }
    for (const auto& p : view.primitives) {
        render::MeshHandle h = box_;
        if (p.shape == SceneView::Primitive::Shape::Sphere) {
            h = sphere_;
        } else if (p.shape == SceneView::Primitive::Shape::Capsule) {
            const std::pair<int, int> key{static_cast<int>(p.radius * 1000), static_cast<int>(p.height * 1000)};
            auto it = capsules_.find(key);
            if (it == capsules_.end()) it = capsules_.emplace(key, upload(make_round(p.radius, std::max(p.height, p.radius * 2)), "Primitive capsule")).first;
            h = it->second;
        }
        if (!h.valid()) continue;
        render::DrawItem item{h, p.world};
        item.tint = {p.color.x, p.color.y, p.color.z, 1.0f};
        item.material = material(p, materials_);
        out.push_back(item);
    }
    return out;
}

render::MaterialHandle SceneRenderer::material(const SceneView::Primitive& p, MaterialLibrary& materials) {
    return p.material_file.empty() ? materials.described(p.material) : materials.file(p.material_file);
}

render::Camera SceneRenderer::camera(const SceneView& view) {
    render::Camera cam;
    if (view.camera) {
        cam.view = camera_view(view.camera->world);
        cam.fov_y = math::radians(view.camera->fov_degrees);
        cam.near_plane = view.camera->near_plane;
    } else {
        cam.view = math::look_at({0, 6, 12}, {0, 0, 0}, {0, 1, 0});
        cam.fov_y = math::radians(60.0f);
    }
    return cam;
}

} // namespace noor::game
