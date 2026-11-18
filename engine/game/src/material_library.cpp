#include "noor/game/material_library.h"

#include "noor/foundation/log.h"

#include <format>

namespace noor::game {
namespace {

std::filesystem::file_time_type time_of(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

// A stable text key for a described material (equal materials share one GPU material).
std::string key_of(const asset::Material& m) {
    const auto ref = [](const asset::TextureRef& r) { return std::format("{}#{}", r.file.generic_string(), r.embedded_image); };
    return std::format("{},{},{},{}|{},{},{},{}|{},{},{}|{},{},{},{}|{},{},{},{}|{}|{}|{}|{}|{}|{}|{}", m.base_color.x, m.base_color.y,
                       m.base_color.z, m.base_color.w, m.metallic, m.roughness, m.normal_scale, m.occlusion_strength, m.emissive.x,
                       m.emissive.y, m.emissive.z, m.emissive_strength, static_cast<int>(m.alpha), m.alpha_cutoff, m.double_sided,
                       m.uv_scale.x, m.uv_scale.y, m.uv_offset.x, m.uv_offset.y, ref(m.base_color_map), ref(m.metallic_roughness_map),
                       ref(m.roughness_map), ref(m.metallic_map), ref(m.normal_map), ref(m.occlusion_map), ref(m.emissive_map));
}

render::AlphaMode alpha_of(asset::AlphaMode a) {
    return a == asset::AlphaMode::Cutout ? render::AlphaMode::Cutout : a == asset::AlphaMode::Blend ? render::AlphaMode::Blend : render::AlphaMode::Opaque;
}

} // namespace

MaterialLibrary::MaterialLibrary(render::Renderer& renderer, TextureLibrary& textures, asset::AssetFileResolver resolve)
    : renderer_(renderer), textures_(textures), resolve_(std::move(resolve)) {}

MaterialLibrary::~MaterialLibrary() {
    for (const auto& [k, e] : files_) renderer_.destroy_material(e.handle);
    for (const auto& [k, h] : described_) renderer_.destroy_material(h);
    for (const auto& [k, v] : models_)
        for (const auto h : v) renderer_.destroy_material(h);
}

render::MaterialDesc MaterialLibrary::desc(const asset::Material& m, const asset::ImportedModel* model) {
    const auto texture = [&](const asset::TextureRef& ref, asset::TextureKind kind, float alpha_coverage = 0.0f) -> render::TextureHandle {
        if (ref.empty()) return {};
        if (ref.embedded_image < 0) return textures_.get(ref.file);
        if (!model || ref.embedded_image >= static_cast<int>(model->images.size())) return renderer_.missing_texture();
        return textures_.get_embedded(ref.file, ref.embedded_image, model->images[static_cast<std::size_t>(ref.embedded_image)].bytes, kind,
                                      alpha_coverage);
    };
    render::MaterialDesc d;
    d.base_color = m.base_color;
    d.metallic = m.metallic;
    d.roughness = m.roughness;
    d.normal_scale = m.normal_scale;
    d.occlusion_strength = m.occlusion_strength;
    d.emissive = m.emissive * m.emissive_strength;
    d.alpha = alpha_of(m.alpha);
    d.alpha_cutoff = m.alpha_cutoff;
    d.double_sided = m.double_sided;
    d.uv_scale = m.uv_scale;
    d.uv_offset = m.uv_offset;
    // Cutout (leaves): the base colour's mips keep their coverage, so far leaves don't fade away.
    d.base_color_texture = texture(m.base_color_map, asset::TextureKind::Color, m.alpha == asset::AlphaMode::Cutout ? m.alpha_cutoff : 0.0f);
    d.metallic_roughness_texture = texture(m.metallic_roughness_map, asset::TextureKind::Data);
    d.roughness_texture = texture(m.roughness_map, asset::TextureKind::Data);
    d.metallic_texture = texture(m.metallic_map, asset::TextureKind::Data);
    d.normal_texture = texture(m.normal_map, asset::TextureKind::Normal);
    d.occlusion_texture = texture(m.occlusion_map, asset::TextureKind::Data);
    d.emissive_texture = texture(m.emissive_map, asset::TextureKind::Color);
    return d;
}

render::MaterialDesc MaterialLibrary::load_file(const std::filesystem::path& noormat) {
    std::vector<std::string> warnings;
    auto m = asset::load_material(noormat, resolve_, &warnings);
    for (const auto& w : warnings) NOOR_LOG_WARN("Game", "Material {}: {}", noormat.filename().string(), w);
    if (!m) {
        NOOR_LOG_WARN("Game", "Material {} can't be loaded: {}", noormat.filename().string(), m.error().describe());
        render::MaterialDesc broken;  // visible on purpose
        broken.base_color_texture = renderer_.missing_texture();
        return broken;
    }
    return desc(*m, nullptr);
}

render::MaterialHandle MaterialLibrary::file(const std::filesystem::path& noormat) {
    if (noormat.empty()) return {};
    const std::string key = noormat.generic_string();
    if (const auto it = files_.find(key); it != files_.end()) return it->second.handle;
    FileEntry e;
    e.time = time_of(noormat);
    auto h = renderer_.create_material(load_file(noormat));
    if (!h) NOOR_LOG_WARN("Game", "Material {}: {}", noormat.filename().string(), h.error().describe());
    e.handle = h ? *h : render::MaterialHandle{};
    files_.emplace(key, e);
    return e.handle;
}

render::MaterialHandle MaterialLibrary::described(const asset::Material& material) {
    const std::string key = key_of(material);
    if (const auto it = described_.find(key); it != described_.end()) return it->second;
    auto h = renderer_.create_material(desc(material, nullptr));
    const render::MaterialHandle handle = h ? *h : render::MaterialHandle{};
    described_.emplace(key, handle);
    return handle;
}

std::vector<render::MaterialHandle> MaterialLibrary::model(const std::filesystem::path& glb, const asset::ImportedModel& model) {
    const std::string key = glb.generic_string();
    if (const auto it = models_.find(key); it != models_.end()) {
        // The model was reloaded: refresh its materials in place (same count) or start over.
        if (it->second.size() == model.materials.size()) {
            for (std::size_t i = 0; i < model.materials.size(); ++i)
                renderer_.update_material(it->second[i], desc(model.materials[i].material, &model));
            return it->second;
        }
        for (const auto h : it->second) renderer_.destroy_material(h);
        models_.erase(it);
    }
    std::vector<render::MaterialHandle> out;
    for (const auto& m : model.materials) {
        auto h = renderer_.create_material(desc(m.material, &model));
        out.push_back(h ? *h : render::MaterialHandle{});
    }
    models_.emplace(key, out);
    return out;
}

bool MaterialLibrary::refresh() {
    const bool textures_changed = textures_.refresh() > 0;
    bool changed = false;
    for (auto& [key, e] : files_) {
        const auto t = time_of(key);
        if (t != e.time && t != std::filesystem::file_time_type{}) {
            e.time = t;
            changed = true;
        }
    }
    if (changed) {
        for (const auto& [key, e] : files_) renderer_.update_material(e.handle, load_file(key));
        NOOR_LOG_INFO("Game", "Reloaded materials");
    }
    return changed || textures_changed;
}

} // namespace noor::game
