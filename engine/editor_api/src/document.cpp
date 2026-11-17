#include "document.h"
#include "noor/asset/material.h"
#include "noor/foundation/file_io.h"

#include "noor/asset/gltf_import.h"
#include "noor/game/game.h"
#include "noor/foundation/log.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace noor::editor {
namespace {
using asset::Json;
namespace stdfs = std::filesystem;

bool read_vec(const Json& j, std::size_t n, float* out) {
    if (!j.is_array() || j.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!j[i].is_number()) return false;
        out[i] = j[i].get<float>();
    }
    return true;
}

Result<Guid> entity_field(const Json& command, const char* key = "entity") {
    if (auto g = asset::guid_field(command, key)) return *g;
    return fail(ErrorCode::InvalidArgument, std::format("command needs a valid \"{}\" id", key));
}

Json transform_json(const scene::Transform& t) {
    return {{"position", {t.position.x, t.position.y, t.position.z}},
            {"rotation", {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w}},
            {"scale", {t.scale.x, t.scale.y, t.scale.z}}};
}

} // namespace

Document::Document(asset::Project project) : project_(std::move(project)), prefabs_(project_, registry_) {}

Result<std::unique_ptr<Document>> Document::open(const stdfs::path& project) {
    auto p = asset::Project::open(project);
    if (!p) return std::unexpected(p.error());
    std::unique_ptr<Document> doc(new Document(std::move(*p)));
    if (auto r = doc->registry_.scan(doc->project_); !r) return std::unexpected(r.error());
    doc->watch_assets();
    NOOR_LOG_INFO("Editor", "Opened project '{}' ({} assets)", doc->project_.info().name, doc->registry_.assets().size());
    return doc;
}

Result<std::unique_ptr<Document>> Document::create(const stdfs::path& folder, std::string_view name) {
    auto p = asset::Project::create(folder, name);
    if (!p) return std::unexpected(p.error());
    std::unique_ptr<Document> doc(new Document(std::move(*p)));
    if (auto r = doc->registry_.scan(doc->project_); !r) return std::unexpected(r.error());
    doc->watch_assets();
    return doc;
}

void Document::watch_assets() {
    watcher_ = asset::FileWatcher{};
    for (const auto& a : registry_.assets()) {
        if (auto path = project_.resolve(a.path)) watcher_.watch(*path);
    }
}

asset::Json Document::poll_files() {
    Json changed_list = Json::array();
    bool prefab_changed = false;
    for (const auto& path : watcher_.poll()) {
        ++asset_versions_[path.generic_string()];
        if (path.extension() == ".noorprefab") prefab_changed = true;
        auto rel = project_.relative(path);
        changed_list.push_back(rel ? *rel : path.generic_string());
        NOOR_LOG_INFO("Editor", "'{}' changed on disk: reloading", rel ? *rel : path.generic_string());
    }
    if (!changed_list.empty()) {
        if (prefab_changed) prefabs_.clear();
        if (world_) {
            // Keep playing on the old runtime copy; the new content shows after Stop.
            NOOR_LOG_INFO("Editor", "Changes apply when play mode stops");
        } else {
            changed();
        }
    }
    return changed_list;
}

Status Document::require_editable() const {
    if (auto s = require_scene(); !s) return s;
    if (world_) return fail(ErrorCode::InvalidArgument, "Stop play mode to edit the scene");
    return {};
}

Status Document::play() {
    std::scoped_lock lock(play_mutex_);
    if (auto s = require_scene(); !s) return s;
    if (world_) return {};
    game::WorldOptions options;
    options.script_host = script_host_;
    options.audio_device = audio_device_;
    auto world = game::make_world(project_, registry_, prefabs_, *scene_, options);
    if (!world) return std::unexpected(world.error());
    world_ = std::move(*world);
    NOOR_LOG_INFO("Editor", "Play");
    ++revision_;
    std::unordered_set<Guid> authored;
    for (const auto& e : scene_->entities) authored.insert(e.id);
    publish(world_->scene(), authored);
    return {};
}

void Document::stop() {
    std::scoped_lock lock(play_mutex_);
    if (!world_) return;
    NOOR_LOG_INFO("Editor", "Stop after {:.1f} s ({} frames)", world_->time(), world_->frame_count());
    world_.reset();
    changed();
}

void Document::tick(double seconds, const runtime::InputState& input) {
    std::scoped_lock lock(play_mutex_);
    if (!world_ || seconds <= 0.0) return;
    world_->frame(seconds, input);  // caps dt itself
    ++revision_;
    std::unordered_set<Guid> authored;
    for (const auto& e : scene_->entities) authored.insert(e.id);
    publish(world_->scene(), authored);
}

Json Document::project_info() const {
    return {{"id", project_.info().id.to_string()},
            {"name", project_.info().name},
            {"root", project_.root().generic_string()},
            {"engine", project_.info().engine_version}};
}

std::shared_ptr<const std::unordered_map<Guid, std::filesystem::path>> Document::asset_files() const {
    auto files = std::make_shared<std::unordered_map<Guid, std::filesystem::path>>();
    for (const auto& a : registry_.assets())
        if (auto path = project_.resolve(a.path)) files->emplace(a.guid, *path);
    return files;
}

Status Document::set_preview(const Json& s) {
    if (!s.is_object()) return fail(ErrorCode::InvalidArgument, "preview settings must be an object");
    auto snap = std::make_shared<RenderSnapshot>();
    snap->show_grid = false;
    snap->texture_cache = project_.root() / ".noor" / "cache" / "textures";
    snap->asset_files = asset_files();
    snap->reload_token = s.value("reload", std::uint64_t{0});

    game::SceneView::Primitive prim{game::SceneView::Primitive::Shape::Box, math::Mat4::identity(), 0.5f, 2.0f, {1, 1, 1}, Guid{}, {}, {}};
    const std::string shape = s.value("shape", std::string("sphere"));
    if (shape == "sphere") {
        prim.shape = game::SceneView::Primitive::Shape::Sphere;
        prim.world = math::translation({0, 0.5f, 0}) * math::scale({1, 1, 1});
    } else if (shape == "plane") {
        prim.world = math::translation({0, 0.5f, 0}) * math::rotation({1, 0, 0}, math::radians(70.0f)) * math::scale({1.2f, 1.2f, 0.02f});
    } else {
        prim.world = math::translation({0, 0.45f, 0}) * math::rotation({0, 1, 0}, math::radians(30.0f)) * math::scale({0.75f, 0.75f, 0.75f});
    }
    if (const std::string material = s.value("material", std::string()); !material.empty()) {
        auto file = project_.resolve(material);
        if (!file) return std::unexpected(file.error());
        prim.material_file = *file;
    }
    snap->primitives.push_back(prim);

    render::Lighting lighting;
    lighting.ev100 = std::clamp(s.value("ev100", 14.0f), -10.0f, 25.0f);
    lighting.sun_shadows = false;  // nothing to cast on
    if (const std::string hdri = s.value("hdri", std::string()); !hdri.empty()) {
        auto file = project_.resolve(hdri);
        if (!file) return std::unexpected(file.error());
        snap->environment.hdri = *file;
        lighting.environment_intensity = std::clamp(s.value("intensity", 8000.0f), 0.0f, 1.0e7f);
        lighting.sun_illuminance = 0.0f;  // the photo has its sun
    } else {
        // The default procedural sky (as game::collect_view makes it) and the default sun.
        snap->environment.zenith = lighting.sky_color * 0.8f;
        snap->environment.horizon = (lighting.sky_color + math::Vec3{1, 1, 1}) * 0.6f;
        snap->environment.ground = lighting.ground_color;
        lighting.environment_intensity = lighting.sky_luminance;
    }
    lighting.environment_rotation = math::radians(s.value("rotation", 0.0f));
    snap->lighting = lighting;
    preview_channel_->publish(std::move(snap));
    return {};
}

Result<Json> Document::make_material(std::string_view texture_path) {
    auto texture = project_.resolve(texture_path);
    if (!texture) return std::unexpected(texture.error());
    if (!std::filesystem::exists(*texture)) return fail(ErrorCode::NotFound, std::format("no texture at {}", texture_path));
    if (auto s = registry_.scan(project_); !s) return std::unexpected(s.error());
    const auto set = asset::find_texture_set(*texture);
    const auto out = texture->parent_path() / (set.name + std::string(asset::kMaterialExtension));
    Json result{{"created", false}, {"maps", Json::array()}};
    for (const auto& [role, file] : set.maps) {
        const auto rel = project_.relative(file);
        result["maps"].push_back({{"role", std::string(asset::role_name(role))}, {"path", rel ? *rel : file.generic_string()}});
    }
    if (!std::filesystem::exists(out)) {
        const auto json = asset::material_json_for(set, [&](const std::filesystem::path& f) -> std::optional<Guid> {
            const auto rel = project_.relative(f);
            const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
            return rec ? std::optional(rec->guid) : std::nullopt;
        });
        if (auto w = noor::fs::write_file_atomic(out, asset::to_text(json), false); !w) return std::unexpected(w.error());
        result["created"] = true;
        if (auto s = registry_.scan(project_); !s) return std::unexpected(s.error());
    }
    const auto rel = project_.relative(out);
    result["path"] = rel ? *rel : out.generic_string();
    return result;
}

Result<Json> Document::assets(bool rescan) {
    if (rescan) {
        auto r = registry_.scan(project_);
        if (!r) return std::unexpected(r.error());
        prefabs_.clear();
        watch_assets();
        if ((r->moved > 0 || r->content_changed > 0) && !world_) changed();
    }
    Json list = Json::array();
    for (const auto& a : registry_.assets()) {
        list.push_back({{"guid", a.guid.to_string()},
                        {"path", a.path},
                        {"importer", a.importer},
                        {"name", stdfs::path(a.path).stem().string()},
                        {"from_interchange", a.from_interchange}});
    }
    return list;
}

Json Document::scenes() const {
    Json list = Json::array();
    std::error_code ec;
    std::vector<std::string> found;
    for (auto it = stdfs::recursive_directory_iterator(project_.scenes_dir(), ec);
         !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().extension() == ".noorscene") {
            if (auto rel = project_.relative(it->path())) found.push_back(*rel);
        }
    }
    std::ranges::sort(found);
    for (auto& f : found) list.push_back(std::move(f));
    return list;
}

Status Document::open_scene(std::string_view relative) {
    if (world_) return fail(ErrorCode::InvalidArgument, "Stop play mode before opening another scene");
    auto path = project_.resolve(relative);
    if (!path) return std::unexpected(path.error());
    auto s = scene::Scene::load(*path);
    if (!s) return std::unexpected(s.error());
    for (const auto& m : s->migrations_applied) NOOR_LOG_INFO("Editor", "Scene upgraded on open: {}", m);
    scene_ = std::move(*s);
    scene_file_ = *path;
    history_.clear();
    // A scene that was just upgraded differs from its file: it should save as modified.
    if (scene_->migrations_applied.empty()) history_.mark_saved();
    changed();
    return {};
}

Status Document::new_scene(std::string_view relative, std::string_view name) {
    if (world_) return fail(ErrorCode::InvalidArgument, "Stop play mode before creating a scene");
    auto path = project_.resolve(relative);
    if (!path) return std::unexpected(path.error());
    if (path->extension() != ".noorscene") return fail(ErrorCode::InvalidArgument, "Scene files end in .noorscene");
    if (stdfs::exists(*path)) return fail(ErrorCode::AlreadyExists, std::format("'{}' already exists", relative));
    scene::Scene s;
    s.name = std::string(name);
    if (auto saved = s.save(*path); !saved) return saved;
    return open_scene(relative);
}

Status Document::save_scene() {
    if (auto s = require_scene(); !s) return s;
    if (auto s = scene_->save(scene_file_); !s) return s;
    scene_->migrations_applied.clear();
    history_.mark_saved();
    ++revision_;  // "modified" changed
    NOOR_LOG_INFO("Editor", "Saved {}", scene_file_.filename().generic_string());
    return {};
}

Status Document::require_scene() const {
    if (!scene_) return fail(ErrorCode::InvalidArgument, "No scene is open");
    return {};
}

Json Document::scene_snapshot() const {
    std::scoped_lock lock(play_mutex_);
    Json snap;
    snap["revision"] = revision_;
    if (!scene_) {
        snap["open"] = false;
        return snap;
    }
    auto rel = project_.relative(scene_file_);
    snap["open"] = true;
    snap["path"] = rel ? *rel : scene_file_.generic_string();
    snap["name"] = scene_->name;
    snap["modified"] = history_.modified() || !scene_->migrations_applied.empty();
    snap["can_undo"] = history_.can_undo();
    snap["can_redo"] = history_.can_redo();
    snap["undo_label"] = history_.undo_label().value_or("");
    snap["redo_label"] = history_.redo_label().value_or("");
    snap["playing"] = playing();
    snap["play_seconds"] = world_ ? world_->time() : 0.0;

    // List the scene as it will run (prefabs expanded); entities that come from a prefab are
    // marked read-only with the instance they belong to.
    // While playing, the runtime copy is shown (values change as behaviours run).
    const auto expanded = scene::expand_prefabs(*scene_, prefabs_.resolver());
    const scene::Scene& shown = world_ ? world_->scene() : expanded.scene;
    std::unordered_set<Guid> authored;
    for (const auto& e : scene_->entities) authored.insert(e.id);
    std::unordered_map<Guid, Guid> instance_of;  // expanded id -> owning instance (authored) id
    Json list = Json::array();
    for (const auto& e : shown.entities) {
        const bool from_prefab = !authored.contains(e.id);
        Json j;
        j["id"] = e.id.to_string();
        j["name"] = e.name;
        j["parent"] = e.parent ? Json(e.parent->to_string()) : Json(nullptr);
        j["layer"] = e.layer;
        j["tags"] = e.tags;
        j["transform"] = transform_json(e.transform);
        j["components"] = e.components;
        j["from_prefab"] = from_prefab;
        if (from_prefab && e.parent) {
            const Guid owner = authored.contains(*e.parent) ? *e.parent : instance_of[*e.parent];
            instance_of[e.id] = owner;
            j["instance"] = owner.to_string();
        }
        list.push_back(std::move(j));
    }
    snap["entities"] = std::move(list);
    Json issues = Json::array();
    for (const auto& i : expanded.issues) issues.push_back({{"entity", i.entity}, {"message", i.message}, {"error", i.error}});
    snap["issues"] = std::move(issues);
    return snap;
}

Status Document::execute(const Json& command) {
    if (auto s = require_editable(); !s) return s;
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string()) {
        return fail(ErrorCode::InvalidArgument, "command needs an \"op\"");
    }
    const std::string op = command["op"];
    bool applied = false;

    if (op == "begin") {
        history_.begin_transaction(command.value("label", std::string("Edit")));
        return {};
    }
    if (op == "end") {
        history_.end_transaction();
        changed();
        return {};
    }
    if (op == "add_asset") return add_asset(command);
    if (op == "create") return create(command);

    auto id = entity_field(command);
    if (!id) return std::unexpected(id.error());
    const scene::Entity* target = scene_->find(*id);
    if (!target) {
        return fail(ErrorCode::NotFound, std::format("Entity {} is not editable here (missing, or part of a prefab "
                                                     "instance: edit the prefab or add an override)", id->to_string()));
    }

    if (op == "set_transform") {
        scene::Transform t = target->transform;
        const Json tj = command.value("transform", Json::object());
        float v[4];
        if (tj.contains("position") && read_vec(tj["position"], 3, v)) t.position = {v[0], v[1], v[2]};
        if (tj.contains("rotation") && read_vec(tj["rotation"], 4, v)) t.rotation = {v[0], v[1], v[2], v[3]};
        if (tj.contains("scale") && read_vec(tj["scale"], 3, v)) t.scale = {v[0], v[1], v[2]};
        // An optional "label" names the undo step ("Rotate" from a gizmo instead of "Move").
        if (command.contains("label") && command["label"].is_string()) {
            history_.begin_transaction(command["label"].get<std::string>());
            applied = history_.execute(*scene_, scene::set_transform(*id, t));
            history_.end_transaction();
        } else {
            applied = history_.execute(*scene_, scene::set_transform(*id, t));
        }
    } else if (op == "rename") {
        applied = history_.execute(*scene_, scene::rename(*id, command.value("name", std::string{})));
    } else if (op == "set_component") {
        if (!command.contains("type") || !command["type"].is_string()) return fail(ErrorCode::InvalidArgument, "set_component needs \"type\"");
        applied = history_.execute(*scene_, scene::set_component(*id, command["type"], command.value("value", Json())));
    } else if (op == "remove") {
        applied = history_.execute(*scene_, scene::remove_entity(*id));
    } else if (op == "reparent") {
        std::optional<Guid> parent;
        if (command.contains("parent") && !command["parent"].is_null()) {
            auto p = entity_field(command, "parent");
            if (!p) return std::unexpected(p.error());
            parent = *p;
        }
        applied = history_.execute(*scene_, scene::reparent(*id, parent));
    } else {
        return fail(ErrorCode::InvalidArgument, std::format("unknown command \"{}\"", op));
    }
    if (!applied) return fail(ErrorCode::InvalidArgument, std::format("\"{}\" could not be applied", op));
    changed();
    return {};
}

// {"op": "create", "name": "Crate", "id"?: "...", "parent"?: id, "transform"?: {...}, "components"?: {...}}
Status Document::create(const Json& command) {
    scene::Entity e;
    if (command.contains("id")) {
        auto id = entity_field(command, "id");
        if (!id) return std::unexpected(id.error());
        if (scene_->find(*id)) return fail(ErrorCode::AlreadyExists, std::format("entity {} already exists", id->to_string()));
        e.id = *id;
    } else {
        e.id = Guid::generate();
    }
    e.name = command.value("name", std::string("Entity"));
    if (command.contains("parent") && !command["parent"].is_null()) {
        auto p = entity_field(command, "parent");
        if (!p) return std::unexpected(p.error());
        if (!scene_->find(*p)) return fail(ErrorCode::NotFound, "the parent is not an editable entity");
        e.parent = *p;
    }
    const Json tj = command.value("transform", Json::object());
    float v[4];
    if (tj.contains("position") && read_vec(tj["position"], 3, v)) e.transform.position = {v[0], v[1], v[2]};
    if (tj.contains("rotation") && read_vec(tj["rotation"], 4, v)) e.transform.rotation = {v[0], v[1], v[2], v[3]};
    if (tj.contains("scale") && read_vec(tj["scale"], 3, v)) e.transform.scale = {v[0], v[1], v[2]};
    if (command.contains("components")) {
        if (!command["components"].is_object()) return fail(ErrorCode::InvalidArgument, "\"components\" must be an object");
        e.components = command["components"];
    }
    const std::string label = "Create " + e.name;
    history_.begin_transaction(label);
    const bool ok = history_.execute(*scene_, scene::add_entity(std::move(e)));
    history_.end_transaction();
    if (!ok) return fail(ErrorCode::InvalidArgument, "the entity could not be created");
    changed();
    return {};
}

Status Document::add_asset(const Json& command) {
    auto guid = entity_field(command, "asset");
    if (!guid) return std::unexpected(guid.error());
    const asset::AssetRecord* rec = registry_.find(*guid);
    if (!rec) return fail(ErrorCode::NotFound, std::format("Unknown asset {}", guid->to_string()));
    scene::Transform placement;
    float v[3];
    if (command.contains("position") && read_vec(command["position"], 3, v)) placement.position = {v[0], v[1], v[2]};
    const std::string name = command.value("name", stdfs::path(rec->path).stem().string());

    // Build the new entities in a scratch scene, then add them through the history so the whole
    // insertion is one undo step.
    scene::Scene scratch;
    if (rec->importer == "prefab") {
        scratch.entities.push_back(scene::make_instance(rec->guid, name, placement));
    } else {
        auto source = project_.resolve(rec->path);
        if (!source) return std::unexpected(source.error());
        auto model = asset::import_gltf(*source);
        if (!model) return std::unexpected(model.error());
        scene::instantiate_model(scratch, rec->guid, *model, name, placement);
    }
    history_.begin_transaction("Add " + name);
    bool ok = true;
    for (auto& e : scratch.entities) ok = history_.execute(*scene_, scene::add_entity(std::move(e))) && ok;
    history_.end_transaction();
    changed();
    if (!ok) return fail(ErrorCode::Unknown, "some entities could not be added");
    return {};
}

bool Document::undo() {
    if (!scene_ || world_ || !history_.undo(*scene_)) return false;
    changed();
    return true;
}

bool Document::redo() {
    if (!scene_ || world_ || !history_.redo(*scene_)) return false;
    changed();
    return true;
}

void Document::changed() {
    ++revision_;
    if (!scene_) {
        auto snap = std::make_shared<RenderSnapshot>();
        snap->revision = revision_;
        channel_->publish(std::move(snap));
        return;
    }
    std::unordered_set<Guid> authored;
    for (const auto& e : scene_->entities) authored.insert(e.id);
    publish(scene::expand_prefabs(*scene_, prefabs_.resolver()).scene, authored);
}

void Document::publish(const scene::Scene& expanded, const std::unordered_set<Guid>& authored) {
    auto snap = std::make_shared<RenderSnapshot>();
    snap->revision = revision_;
    const auto& entities = expanded.entities;
    std::unordered_map<Guid, std::size_t> index;
    for (std::size_t i = 0; i < entities.size(); ++i) index.emplace(entities[i].id, i);
    auto world_rotation = [&](std::size_t i) {
        math::Quat q = entities[i].transform.rotation;
        std::optional<Guid> p = entities[i].parent;
        for (int depth = 0; p && depth < 256; ++depth) {  // depth guard: cycles are reported elsewhere
            const auto it = index.find(*p);
            if (it == index.end()) break;
            q = entities[it->second].transform.rotation * q;
            p = entities[it->second].parent;
        }
        return math::normalize_quat(q);
    };
    snap->entities.reserve(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        const math::Mat4 world = expanded.world_matrix(i);
        snap->entities.push_back({e.id, e.parent, world, world_rotation(i), e.transform, !world_ && authored.contains(e.id)});
        if (e.has("marker")) snap->markers.push_back({world, e.id, std::nullopt});
    }
    // Models, shapes and the game camera: the same view noor_player draws.
    auto view = game::collect_view(expanded, game::registry_asset_path(project_, registry_), world_ ? &world_->render_poses() : nullptr);
    snap->primitives = std::move(view.primitives);
    snap->lighting = view.lighting;
    snap->environment = view.environment;
    for (const auto& lm : view.light_markers) snap->markers.push_back({math::translation(lm.position), lm.entity, lm.color});
    snap->texture_cache = project_.root() / ".noor" / "cache" / "textures";
    snap->asset_files = asset_files();
    if (world_) {
        snap->camera = view.camera;
        snap->ui = world_->ui_commands();
    }
    for (auto& m : view.meshes) {
        std::string key = m.asset_path.generic_string();
        const auto version = asset_versions_.contains(key) ? asset_versions_.at(key) : 0;
        snap->meshes.push_back({std::move(key), m.mesh_index, m.world, m.entity, version});
    }
    channel_->publish(std::move(snap));
}

} // namespace noor::editor
