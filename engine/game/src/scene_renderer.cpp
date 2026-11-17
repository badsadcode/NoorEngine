#include "noor/game/scene_renderer.h"

#include "noor/asset/gltf_import.h"
#include "noor/foundation/log.h"

#include <algorithm>

namespace noor::game {

SceneRenderer::SceneRenderer(render::Renderer& renderer, std::filesystem::path texture_cache, asset::AssetFileResolver resolve)
    : renderer_(renderer), textures_(renderer, std::move(texture_cache)), materials_(renderer, textures_, std::move(resolve)) {
    box_ = upload(make_box({1, 1, 1}, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}), "Primitive box");
    sphere_ = upload(make_round(0.5f, 1.0f), "Primitive sphere");
}

SceneRenderer::~SceneRenderer() {
    auto destroy = [&](render::MeshHandle h) {
        if (h.valid()) renderer_.destroy_mesh(h);
    };
    destroy(box_);
    destroy(sphere_);
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

std::vector<render::DrawItem> SceneRenderer::draws(const SceneView& view) {
    std::vector<render::DrawItem> out;
    out.reserve(view.meshes.size() + view.primitives.size());
    for (const auto& m : view.meshes) {
        const std::string key = m.asset_path.generic_string();
        auto it = models_.find(key);
        if (it == models_.end()) {
            std::optional<Model> loaded;
            if (auto model = asset::import_gltf(m.asset_path)) {
                loaded.emplace();
                for (const auto& mesh : model->meshes) {
                    auto h = renderer_.create_mesh({{reinterpret_cast<const render::Vertex*>(mesh.vertices.data()), mesh.vertices.size()},
                                                    mesh.indices, mesh.name});
                    loaded->meshes.push_back(h ? *h : render::MeshHandle{});
                    loaded->submeshes.push_back(mesh.submeshes);
                }
                loaded->materials = materials_.model(m.asset_path, *model);
            } else {
                NOOR_LOG_WARN("Game", "{}", model.error().describe());
            }
            it = models_.emplace(key, std::move(loaded)).first;
        }
        if (!it->second || m.mesh_index < 0 || m.mesh_index >= static_cast<int>(it->second->meshes.size())) continue;
        const auto index = static_cast<std::size_t>(m.mesh_index);
        const auto h = it->second->meshes[index];
        if (!h.valid()) continue;
        for (const auto& sm : it->second->submeshes[index]) {  // one draw per material
            render::DrawItem item{h, m.world};
            item.first_index = sm.first_index;
            item.index_count = sm.index_count;
            if (sm.material >= 0 && sm.material < static_cast<int>(it->second->materials.size()))
                item.material = it->second->materials[static_cast<std::size_t>(sm.material)];
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
