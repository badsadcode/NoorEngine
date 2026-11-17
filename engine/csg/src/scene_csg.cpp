#include "noor/csg/scene_csg.h"

#include "noor/asset/json.h"

#include <algorithm>
#include <unordered_map>

namespace noor::csg {
namespace {

using asset::Json;

float json_float(const Json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

math::Vec2 json_vec2(const Json& j, const char* key, math::Vec2 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 2 || !j[key][0].is_number() || !j[key][1].is_number()) return fallback;
    return {j[key][0].get<float>(), j[key][1].get<float>()};
}

Shape shape_of(const std::string& name) {
    for (const auto s : {Shape::Box, Shape::Wedge, Shape::Cylinder, Shape::Cone, Shape::Sphere, Shape::Stairs, Shape::Arch})
        if (shape_name(s) == name) return s;
    return Shape::Box;
}

} // namespace

SceneModel brushes_from_scene(const scene::Scene& scene, std::size_t model_index) {
    SceneModel out;
    out.slot_materials.emplace_back();  // slot 0: the default material
    std::unordered_map<Guid, std::uint32_t> slot_of;
    const auto slot = [&](const Guid& id) {
        auto [it, added] = slot_of.try_emplace(id, static_cast<std::uint32_t>(out.slot_materials.size()));
        if (added) out.slot_materials.emplace_back(id);
        return it->second;
    };
    for (const std::size_t j : game::csg_brush_children(scene, model_index)) {
        const Json& c = scene.entities[j].components.at("brush");
        if (!c.is_object()) continue;
        Brush b;
        b.shape = shape_of(c.value("shape", std::string("box")));
        const std::string op = c.value("operation", std::string("add"));
        b.operation = op == "subtract" ? Operation::Subtract : op == "intersect" ? Operation::Intersect : Operation::Add;
        if (c.contains("size") && c["size"].is_array() && c["size"].size() == 3 && c["size"][0].is_number() && c["size"][1].is_number() &&
            c["size"][2].is_number())
            b.size = {c["size"][0].get<float>(), c["size"][1].get<float>(), c["size"][2].get<float>()};
        b.sides = static_cast<std::uint32_t>(std::clamp(json_float(c, "sides", 16.0f), 3.0f, 256.0f));
        b.steps = static_cast<std::uint32_t>(std::clamp(json_float(c, "steps", 4.0f), 1.0f, 256.0f));
        const auto& t = scene.entities[j].transform;
        b.transform = math::compose(t.position, t.rotation, t.scale);
        // A surface from JSON: material, UV scale/offset/rotation, alignment; missing keys from `base`.
        const auto surface = [&](const Json& sj, const Surface& base) {
            Surface s = base;
            s.scale = json_vec2(sj, "uv_scale", base.scale);
            s.offset = json_vec2(sj, "uv_offset", base.offset);
            s.rotation_degrees = json_float(sj, "uv_rotation", base.rotation_degrees);
            if (sj.contains("uv_align") && sj["uv_align"].is_string())
                s.align = sj["uv_align"].get<std::string>() == "brush" ? Align::Brush : Align::World;
            if (sj.contains("material")) {
                const auto id = asset::guid_field(sj, "material");
                s.material = sj["material"].is_null() ? 0u : id ? slot(*id) : base.material;
            }
            return s;
        };
        b.surface = surface(c, Surface{});
        // "faces": per logical face, null (= the brush's surface) or an object overriding it.
        if (c.contains("faces") && c["faces"].is_array())
            for (std::uint32_t f = 0; f < c["faces"].size() && f < face_count(b.shape); ++f) {
                b.faces.resize(f + 1, b.surface);
                if (c["faces"][f].is_object()) b.faces[f] = surface(c["faces"][f], b.surface);
            }
        out.brushes.push_back(std::move(b));
        out.brush_ids.push_back(scene.entities[j].id);
    }
    return out;
}

Result<game::CookedCsg> cook_model(const scene::Scene& scene, std::size_t model_index, Model* full, std::vector<std::string>* warnings) {
    const SceneModel sm = brushes_from_scene(scene, model_index);
    auto built = build_model(sm.brushes);
    if (!built) return std::unexpected(built.error());
    if (warnings) warnings->insert(warnings->end(), built->warnings.begin(), built->warnings.end());
    game::CookedCsg out;
    out.key = game::csg_model_key(scene, model_index);
    out.vertices.reserve(built->vertices.size());
    for (const auto& v : built->vertices) out.vertices.push_back({v.position, v.normal, {1, 1, 1}, v.uv, v.tangent});
    out.indices = built->indices;
    for (const auto& s : built->submeshes)  // already one per slot, in slot order
        out.submeshes.push_back({s.first_index, s.index_count, s.material < sm.slot_materials.size() ? sm.slot_materials[s.material] : std::nullopt});
    if (full) *full = std::move(*built);
    return out;
}

} // namespace noor::csg
