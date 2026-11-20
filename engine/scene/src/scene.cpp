#include "noor/scene/scene.h"

#include "entity_json.h"
#include "noor/asset/migration.h"
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
    e.layer = j.value("layer", std::string("default"));
    if (j.contains("tags")) {
        if (!j["tags"].is_array()) return where("\"tags\" must be an array of strings");
        for (const auto& t : j["tags"]) {
            if (!t.is_string()) return where("\"tags\" must be an array of strings");
            e.tags.push_back(t.get<std::string>());
        }
    }
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
    j["layer"] = e.layer;
    j["tags"] = e.tags;
    j["parent"] = e.parent ? Json(e.parent->to_string()) : Json(nullptr);
    j["transform"] = {{"position", vec3_json(e.transform.position)},
                      {"rotation", quat_json(e.transform.rotation)},
                      {"scale", vec3_json(e.transform.scale)}};
    j["components"] = e.components;
    return j;
}

// Every change to the scene file format gets a step here, plus a test with a file in the old format.
const asset::Migrations& scene_migrations() {
    static const asset::Migrations migrations = [] {
        asset::Migrations m("noor_scene", kSceneFormat);
        m.add(1, "entities gain \"layer\" (default) and \"tags\" (empty)", [](Json& doc) -> Status {
            if (doc.contains("entities") && doc["entities"].is_array()) {
                for (auto& e : doc["entities"]) {
                    if (!e.is_object()) continue;
                    // Rebuild to keep the v2 key order: id, name, parent, layer, tags, ...
                    Json upgraded = Json::object();
                    for (auto it = e.begin(); it != e.end(); ++it) {
                        upgraded[it.key()] = it.value();
                        if (it.key() == "parent") {
                            upgraded["layer"] = "default";
                            upgraded["tags"] = Json::array();
                        }
                    }
                    if (!upgraded.contains("layer")) upgraded["layer"] = "default";
                    if (!upgraded.contains("tags")) upgraded["tags"] = Json::array();
                    e = std::move(upgraded);
                }
            }
            return {};
        });
        return m;
    }();
    return migrations;
}

} // namespace

// Shared with prefab.cpp (entity_json.h): prefab files store entities in scene format.
namespace detail {
Result<Entity> parse_entity(const asset::Json& j, std::size_t index, std::string_view source) {
    return entity_from_json(j, index, source);
}
asset::Json write_entity(const Entity& e) { return entity_to_json(e); }
} // namespace detail

Result<Scene> Scene::from_text(std::string_view text, std::string_view source) {
    auto json = asset::parse_json(text, source);
    if (!json) return std::unexpected(json.error());
    if (json->value("noor_scene", 0) <= 0) {
        return fail(ErrorCode::ParseError, std::format("{}: not a NoorEngine scene", source));
    }
    auto applied = scene_migrations().upgrade(*json, source);
    if (!applied) return std::unexpected(applied.error());
    Scene scene;
    scene.migrations_applied = std::move(*applied);
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

std::vector<math::Mat4> Scene::world_matrices() const {
    std::unordered_map<Guid, std::size_t> index;
    index.reserve(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i) index.emplace(entities[i].id, i);
    std::vector<math::Mat4> world(entities.size());
    std::vector<std::uint8_t> state(entities.size(), 0);  // 0 not yet, 1 in progress (a cycle), 2 done
    std::vector<std::size_t> chain;
    for (std::size_t start = 0; start < entities.size(); ++start) {
        if (state[start] == 2) continue;
        // Walk up to the first ancestor already known (or the root), then fill in on the way down.
        chain.clear();
        std::size_t i = start;
        while (true) {
            chain.push_back(i);
            state[i] = 1;
            const auto parent = entities[i].parent ? index.find(*entities[i].parent) : index.end();
            if (parent == index.end() || state[parent->second] != 0) break;
            i = parent->second;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const std::size_t k = *it;
            const auto parent = entities[k].parent ? index.find(*entities[k].parent) : index.end();
            const bool known = parent != index.end() && state[parent->second] == 2;
            world[k] = known ? world[parent->second] * entities[k].transform.matrix() : entities[k].transform.matrix();
            state[k] = 2;
        }
    }
    return world;
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
            // A primitive's size comes with it (Noor Interchange 3.5): a trunk capsule's radius and height.
            for (const char* key : {"radius", "height", "size"}) {
                if (const auto v = node.noor.find(key); v != node.noor.end()) e.components["collider"][key] = *v;
            }
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
    // Skinned meshes (8C, ADR-036): their entity names the skin and, in the skin's order, the entities of its joints
    // (every node is an entity, joints too), so posing those entities poses the mesh.
    const std::size_t first = scene.entities.size() - model.nodes.size();
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        if (node.skin < 0 || node.skin >= static_cast<int>(model.skins.size())) continue;
        auto& mesh = scene.entities[first + i].components["mesh"];
        if (!mesh.is_object()) continue;
        asset::Json joints = asset::Json::array();
        for (const int j : model.skins[static_cast<std::size_t>(node.skin)].joints) joints.push_back(ids[static_cast<std::size_t>(j)].to_string());
        mesh["skin"] = node.skin;
        mesh["joints"] = std::move(joints);
    }
    // Animated models (8C part 2) and skinned ones (part 5: they can play another model's clips, retargeted): the root
    // lists the entity each glTF node became, in the file's order, so clips (which name nodes by index) move the right
    // entities whatever they've been renamed to.
    if (!model.animations.empty() || !model.skins.empty()) {
        asset::Json nodes = asset::Json::array();
        for (const Guid& id : ids) nodes.push_back(id.to_string());
        scene.entities[first - 1].components["model"]["nodes"] = std::move(nodes);  // the root, just before its nodes
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
