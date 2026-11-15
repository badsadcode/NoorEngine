#include "noor/scene/scene.h"

#include "noor/foundation/file_io.h"

#include <format>
#include <unordered_map>
#include <unordered_set>

namespace noor::scene {
namespace {
using asset::Json;

Json vec3_json(math::Vec3 v) { return Json::array({v.x, v.y, v.z}); }
Json quat_json(math::Quat q) { return Json::array({q.x, q.y, q.z, q.w}); }

template <std::size_t N>
bool read_floats(const Json& j, float (&out)[N]) {
    if (!j.is_array() || j.size() != N) return false;
    for (std::size_t i = 0; i < N; ++i) {
        if (!j[i].is_number()) return false;
        out[i] = j[i].get<float>();
    }
    return true;
}

Result<Entity> entity_from_json(const Json& j, std::size_t index, std::string_view source) {
    const auto where = [&](std::string_view what) {
        return fail(ErrorCode::ParseError, std::format("{}: entity #{}: {}", source, index, what));
    };
    if (!j.is_object()) return where("not an object");
    Entity e;
    auto id = asset::guid_field(j, "id");
    if (!id) return where("missing or invalid \"id\"");
    e.id = *id;
    e.name = j.value("name", std::string{});
    if (j.contains("parent") && !j["parent"].is_null()) {
        auto parent = asset::guid_field(j, "parent");
        if (!parent) return where("invalid \"parent\"");
        e.parent = *parent;
    }
    if (j.contains("transform")) {
        const Json& t = j["transform"];
        float p[3], r[4], s[3];
        if (t.contains("position")) {
            if (!read_floats(t["position"], p)) return where("transform.position must be [x, y, z]");
            e.transform.position = {p[0], p[1], p[2]};
        }
        if (t.contains("rotation")) {
            if (!read_floats(t["rotation"], r)) return where("transform.rotation must be [x, y, z, w]");
            e.transform.rotation = {r[0], r[1], r[2], r[3]};
        }
        if (t.contains("scale")) {
            if (!read_floats(t["scale"], s)) return where("transform.scale must be [x, y, z]");
            e.transform.scale = {s[0], s[1], s[2]};
        }
    }
    if (j.contains("components")) {
        if (!j["components"].is_object()) return where("\"components\" must be an object");
        e.components = j["components"];
    }
    return e;
}

Json entity_to_json(const Entity& e) {
    Json j;
    j["id"] = e.id.to_string();
    j["name"] = e.name;
    j["parent"] = e.parent ? Json(e.parent->to_string()) : Json(nullptr);
    j["transform"] = {{"position", vec3_json(e.transform.position)},
                      {"rotation", quat_json(e.transform.rotation)},
                      {"scale", vec3_json(e.transform.scale)}};
    j["components"] = e.components;
    return j;
}

} // namespace

Result<Scene> Scene::from_text(std::string_view text, std::string_view source) {
    auto json = asset::parse_json(text, source);
    if (!json) return std::unexpected(json.error());
    const int format = json->value("noor_scene", 0);
    if (format <= 0) return fail(ErrorCode::ParseError, std::format("{}: not a NoorEngine scene", source));
    if (format > kSceneFormat) {
        return fail(ErrorCode::VersionMismatch,
                    std::format("{}: scene format {} needs a newer NoorEngine (this one reads up to {})", source,
                                format, kSceneFormat));
    }
    Scene scene;
    auto id = asset::guid_field(*json, "id");
    if (!id) return fail(ErrorCode::ParseError, std::format("{}: missing scene id", source));
    scene.id = *id;
    scene.name = json->value("name", std::string("Untitled"));
    if (json->contains("entities")) {
        const Json& list = (*json)["entities"];
        if (!list.is_array()) return fail(ErrorCode::ParseError, std::format("{}: \"entities\" must be an array", source));
        scene.entities.reserve(list.size());
        for (std::size_t i = 0; i < list.size(); ++i) {
            auto e = entity_from_json(list[i], i, source);
            if (!e) return std::unexpected(e.error());
            scene.entities.push_back(std::move(*e));
        }
    }
    return scene;
}

Result<Scene> Scene::load(const std::filesystem::path& path) {
    auto text = fs::read_file(path, 256ull << 20);
    if (!text) return std::unexpected(text.error());
    return from_text(*text, path.generic_string());
}

std::string Scene::to_text() const {
    Json json;
    json["noor_scene"] = kSceneFormat;
    json["id"] = id.to_string();
    json["name"] = name;
    Json list = Json::array();
    for (const auto& e : entities) list.push_back(entity_to_json(e));
    json["entities"] = std::move(list);
    return asset::to_text(json);
}

Status Scene::save(const std::filesystem::path& path) const { return fs::write_file_atomic(path, to_text()); }

const Entity* Scene::find(const Guid& entity_id) const {
    for (const auto& e : entities) {
        if (e.id == entity_id) return &e;
    }
    return nullptr;
}

math::Mat4 Scene::world_matrix(std::size_t index) const {
    math::Mat4 m = entities[index].transform.matrix();
    std::optional<Guid> parent = entities[index].parent;
    std::size_t guard = 0;
    while (parent && guard++ < entities.size()) {
        const Entity* p = find(*parent);
        if (!p) break;
        m = p->transform.matrix() * m;
        parent = p->parent;
    }
    return m;
}

Guid instantiate_model(Scene& scene, const Guid& asset, const asset::ImportedModel& model, std::string_view root_name,
                       const Transform& root_transform) {
    Entity root;
    root.id = Guid::generate();
    root.name = std::string(root_name);
    root.transform = root_transform;
    root.components["model"] = {{"asset", asset.to_string()}};
    const Guid root_id = root.id;
    scene.entities.push_back(std::move(root));

    std::vector<Guid> ids(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        Entity e;
        e.id = Guid::generate();  // each instance of a model gets its own entity ids
        e.name = node.name;
        e.parent = node.parent >= 0 ? ids[static_cast<std::size_t>(node.parent)] : root_id;
        e.transform = {node.translation, node.rotation, node.scale};

        if (node.is_collision()) {
            e.components["collider"] = {{"shape", node.noor.value("collision", std::string("mesh"))},
                                        {"asset", asset.to_string()},
                                        {"mesh_index", node.mesh}};
        } else if (node.mesh >= 0) {
            e.components["mesh"] = {{"asset", asset.to_string()}, {"mesh_index", node.mesh}};
        }
        if (node.is_marker()) {
            e.components["marker"] = node.noor;
        } else if (node.noor.is_object() && !node.is_collision()) {
            e.components["noor"] = node.noor;  // parts, ids, props: kept for tools and gameplay
        }
        ids[i] = e.id;
        scene.entities.push_back(std::move(e));
    }
    return root_id;
}

std::vector<ValidationIssue> validate_structure(const Scene& scene) {
    std::vector<ValidationIssue> issues;
    std::unordered_set<Guid> seen;
    for (const auto& e : scene.entities) {
        const std::string label = e.name + " (" + e.id.to_string() + ")";
        if (!seen.insert(e.id).second) issues.push_back({label, "duplicate entity id"});
        if (e.parent) {
            if (*e.parent == e.id) {
                issues.push_back({label, "is its own parent"});
            } else if (!seen.contains(*e.parent)) {
                issues.push_back({label, scene.find(*e.parent) ? "parent appears after the entity"
                                                               : "parent " + e.parent->to_string() + " does not exist"});
            }
        }
    }
    return issues;
}

std::vector<std::pair<std::size_t, Guid>> asset_references(const Scene& scene) {
    std::vector<std::pair<std::size_t, Guid>> refs;
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        for (const auto& [type, data] : scene.entities[i].components.items()) {
            (void)type;
            if (auto guid = asset::guid_field(data, "asset")) refs.emplace_back(i, *guid);
        }
    }
    return refs;
}

} // namespace noor::scene
