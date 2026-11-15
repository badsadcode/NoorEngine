#include "noor/scene/prefab.h"

#include "entity_json.h"
#include "noor/asset/migration.h"
#include "noor/foundation/file_io.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace noor::scene {
namespace {
using asset::Json;

constexpr int kMaxNesting = 16;

std::string label(const Entity& e) { return e.name + " (" + e.id.to_string() + ")"; }

bool read_vec(const Json& j, std::size_t n, float* out) {
    if (!j.is_array() || j.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!j[i].is_number()) return false;
        out[i] = j[i].get<float>();
    }
    return true;
}

void apply_override(Entity& e, const Json& o, std::vector<ExpandIssue>& issues) {
    const auto bad = [&](std::string_view what) {
        issues.push_back({label(e), std::format("override ignored: {}", what), false});
    };
    if (!o.is_object()) return bad("not an object");
    if (o.contains("name")) {
        if (o["name"].is_string()) e.name = o["name"].get<std::string>(); else bad("\"name\" must be a string");
    }
    if (o.contains("layer")) {
        if (o["layer"].is_string()) e.layer = o["layer"].get<std::string>(); else bad("\"layer\" must be a string");
    }
    if (o.contains("tags")) {
        if (o["tags"].is_array() && std::ranges::all_of(o["tags"], [](const Json& t) { return t.is_string(); })) {
            e.tags = o["tags"].get<std::vector<std::string>>();
        } else {
            bad("\"tags\" must be an array of strings");
        }
    }
    if (o.contains("transform")) {
        const Json& t = o["transform"];
        float v[4];
        if (t.contains("position")) {
            if (read_vec(t["position"], 3, v)) e.transform.position = {v[0], v[1], v[2]}; else bad("transform.position");
        }
        if (t.contains("rotation")) {
            if (read_vec(t["rotation"], 4, v)) e.transform.rotation = {v[0], v[1], v[2], v[3]}; else bad("transform.rotation");
        }
        if (t.contains("scale")) {
            if (read_vec(t["scale"], 3, v)) e.transform.scale = {v[0], v[1], v[2]}; else bad("transform.scale");
        }
    }
    if (o.contains("components")) {
        if (!o["components"].is_object()) return bad("\"components\" must be an object");
        for (const auto& [type, value] : o["components"].items()) {
            if (value.is_null()) {
                e.components.erase(type);
            } else {
                e.components[type] = value;
            }
        }
    }
}

struct Expander {
    const PrefabResolver& resolve;
    std::vector<Entity>& out;
    std::vector<ExpandIssue>& issues;
    std::size_t instances = 0;

    // Expands `prefab` under the instance entity `instance` (already in `out`).
    void expand(const Entity& instance, const Json& comp, std::vector<Guid>& stack) {
        auto guid = asset::guid_field(comp, "asset");
        if (!guid) {
            issues.push_back({label(instance), "prefab component has no valid \"asset\""});
            return;
        }
        if (std::ranges::find(stack, *guid) != stack.end()) {
            issues.push_back({label(instance), std::format("prefab {} contains itself (cycle); not expanded", guid->to_string())});
            return;
        }
        if (stack.size() >= kMaxNesting) {
            issues.push_back({label(instance), std::format("prefabs nested deeper than {}; not expanded", kMaxNesting)});
            return;
        }
        const Prefab* prefab = resolve(*guid);
        if (!prefab) {
            issues.push_back({label(instance), std::format("references missing prefab {}", guid->to_string())});
            return;
        }
        ++instances;

        std::unordered_set<Guid> skipped;
        if (comp.contains("removed") && comp["removed"].is_array()) {
            for (const auto& r : comp["removed"]) {
                if (!r.is_string()) continue;
                if (auto id = Guid::parse(r.get<std::string>())) skipped.insert(*id);
            }
        }
        const Json overrides = comp.value("overrides", Json::object());
        std::unordered_set<std::string> used_overrides;

        stack.push_back(*guid);
        for (const Entity& pe : prefab->entities) {
            if (skipped.contains(pe.id) || (pe.parent && skipped.contains(*pe.parent))) {
                skipped.insert(pe.id);  // children of removed entities go too
                continue;
            }
            Entity e = pe;
            e.id = expanded_id(instance.id, pe.id);
            e.parent = pe.parent ? expanded_id(instance.id, *pe.parent) : instance.id;
            const std::string key = pe.id.to_string();
            if (overrides.is_object() && overrides.contains(key)) {
                apply_override(e, overrides[key], issues);
                used_overrides.insert(key);
            }
            const Json nested = e.components.contains("prefab") ? e.components["prefab"] : Json();
            out.push_back(e);
            if (nested.is_object()) {
                const Entity placed = out.back();
                expand(placed, nested, stack);
            }
        }
        stack.pop_back();

        if (overrides.is_object()) {
            for (const auto& [key, value] : overrides.items()) {
                (void)value;
                if (!used_overrides.contains(key)) {
                    issues.push_back({label(instance),
                                      std::format("override for {} kept but unused: that entity is no longer in prefab '{}'",
                                                  key, prefab->name),
                                      false});
                }
            }
        }
    }
};

std::vector<std::size_t> subtree_of(const Scene& s, const Guid& root) {
    std::unordered_set<Guid> members{root};
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        const Entity& e = s.entities[i];
        if (e.id == root || (e.parent && members.contains(*e.parent))) {
            members.insert(e.id);
            out.push_back(i);
        }
    }
    return out;
}

const asset::Migrations& prefab_migrations() {
    static const asset::Migrations m("noor_prefab", kPrefabFormat);
    return m;
}

} // namespace

Result<Prefab> Prefab::from_text(std::string_view text, std::string_view source) {
    auto json = asset::parse_json(text, source);
    if (!json) return std::unexpected(json.error());
    if (json->value("noor_prefab", 0) <= 0) return fail(ErrorCode::ParseError, std::format("{}: not a NoorEngine prefab", source));
    if (auto applied = prefab_migrations().upgrade(*json, source); !applied) return std::unexpected(applied.error());

    Prefab p;
    auto id = asset::guid_field(*json, "id");
    if (!id) return fail(ErrorCode::ParseError, std::format("{}: missing prefab id", source));
    p.id = *id;
    p.name = json->value("name", std::string{});
    if (!json->contains("entities") || !(*json)["entities"].is_array() || (*json)["entities"].empty()) {
        return fail(ErrorCode::ParseError, std::format("{}: a prefab needs at least one entity", source));
    }
    std::unordered_set<Guid> seen;
    const Json& list = (*json)["entities"];
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto e = detail::parse_entity(list[i], i, source);
        if (!e) return std::unexpected(e.error());
        if (i == 0 && e->parent) return fail(ErrorCode::ParseError, std::format("{}: the first entity must be the root", source));
        if (i > 0 && (!e->parent || !seen.contains(*e->parent))) {
            return fail(ErrorCode::ParseError, std::format("{}: entity #{} must have a parent listed before it", source, i));
        }
        if (!seen.insert(e->id).second) return fail(ErrorCode::ParseError, std::format("{}: duplicate entity id", source));
        p.entities.push_back(std::move(*e));
    }
    return p;
}

Result<Prefab> Prefab::load(const std::filesystem::path& path) {
    auto text = fs::read_file(path, 64ull << 20);
    if (!text) return std::unexpected(text.error());
    return from_text(*text, path.generic_string());
}

std::string Prefab::to_text() const {
    Json json;
    json["noor_prefab"] = kPrefabFormat;
    json["id"] = id.to_string();
    json["name"] = name;
    Json list = Json::array();
    for (const auto& e : entities) list.push_back(detail::write_entity(e));
    json["entities"] = std::move(list);
    return asset::to_text(json);
}

Status Prefab::save(const std::filesystem::path& path) const { return fs::write_file_atomic(path, to_text()); }

ExpandedScene expand_prefabs(const Scene& scene, const PrefabResolver& resolve) {
    ExpandedScene result;
    result.scene.id = scene.id;
    result.scene.name = scene.name;
    result.scene.entities.reserve(scene.entities.size());
    Expander expander{resolve, result.scene.entities, result.issues};
    for (const Entity& e : scene.entities) {
        result.scene.entities.push_back(e);
        if (e.components.contains("prefab")) {
            std::vector<Guid> stack;
            const Entity placed = result.scene.entities.back();
            expander.expand(placed, placed.components["prefab"], stack);
        }
    }
    result.instances = expander.instances;
    return result;
}

const Prefab* PrefabLibrary::get(const Guid& id) {
    if (const auto it = cache_.find(id); it != cache_.end()) return it->second ? &*it->second : nullptr;
    const asset::AssetRecord* record = registry_->find(id);
    std::optional<Prefab> loaded;
    if (record && record->importer == "prefab") {
        auto path = project_->resolve(record->path);
        auto prefab = path ? Prefab::load(*path) : Result<Prefab>(std::unexpected(path.error()));
        if (prefab) {
            loaded = std::move(*prefab);
        } else {
            errors_.push_back(prefab.error().describe());
        }
    }
    auto [it, inserted] = cache_.emplace(id, std::move(loaded));
    (void)inserted;
    return it->second ? &*it->second : nullptr;
}

Entity make_instance(const Guid& prefab, std::string name, const Transform& placement, std::optional<Guid> parent) {
    Entity e;
    e.id = Guid::generate();
    e.name = std::move(name);
    e.parent = parent;
    e.transform = placement;
    e.components["prefab"] = {{"asset", prefab.to_string()}, {"removed", Json::array()}, {"overrides", Json::object()}};
    return e;
}

Result<Prefab> make_prefab(Scene& scene, const Guid& root, std::string name, bool replace_with_instance) {
    const auto family = subtree_of(scene, root);
    if (family.empty()) return fail(ErrorCode::NotFound, std::format("No entity {} in scene '{}'", root.to_string(), scene.name));

    Prefab prefab;
    prefab.name = std::move(name);
    std::unordered_map<Guid, Guid> remap;
    for (const std::size_t i : family) remap[scene.entities[i].id] = Guid::generate();
    for (const std::size_t i : family) {
        Entity e = scene.entities[i];
        e.id = remap.at(e.id);
        if (i == family.front()) {
            e.parent.reset();
            e.transform = {};  // the placement lives in the instance
        } else {
            e.parent = remap.at(*e.parent);
        }
        prefab.entities.push_back(std::move(e));
    }

    if (replace_with_instance) {
        const Entity old_root = scene.entities[family.front()];
        Entity instance = make_instance(prefab.id, old_root.name, old_root.transform, old_root.parent);
        instance.id = old_root.id;  // keep references to the old root valid
        instance.layer = old_root.layer;
        instance.tags = old_root.tags;
        for (auto it = family.rbegin(); it != family.rend(); ++it) {
            scene.entities.erase(scene.entities.begin() + static_cast<std::ptrdiff_t>(*it));
        }
        scene.entities.insert(scene.entities.begin() + static_cast<std::ptrdiff_t>(family.front()), std::move(instance));
    }
    return prefab;
}

} // namespace noor::scene
