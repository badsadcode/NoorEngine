#include "document.h"
#include "noor/asset/material.h"
#include "noor/asset/texture.h"
#include "noor/csg/csg.h"
#include "noor/csg/scene_csg.h"
#include "noor/asset/gltf_export.h"
#include "noor/game/cooked_csg.h"
#include "noor/foundation/file_io.h"

#include "noor/asset/gltf_import.h"
#include "noor/game/game.h"
#include "noor/foundation/log.h"

#include <algorithm>
#include <map>
#include <chrono>
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
    cook_csg();  // Play loads cooked CSG models, like a game
    finish_stroke();
    game::WorldOptions options;
    options.script_host = script_host_;
    options.audio_device = audio_device_;
    // Terrains collide with what is being sculpted, saved or not.
    options.terrain_heights = [this](const Json& component, const terrain::Settings& settings) -> std::shared_ptr<const terrain::Heights> {
        const auto id = asset::guid_field(component, "sculpt");
        auto edit = id ? terrain_edit(*id, settings) : nullptr;
        return edit ? edit->snapshot() : nullptr;
    };
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

Result<Json> Document::texture(std::string_view texture_path) {
    auto file = project_.resolve(texture_path);
    if (!file) return std::unexpected(file.error());
    const auto settings = asset::texture_settings_for(*file);
    auto info = asset::texture_info(*file, settings);
    if (!info) return std::unexpected(info.error());
    return Json{{"settings", settings.to_json()},
                {"source_width", info->source_width},
                {"source_height", info->source_height},
                {"width", info->width},
                {"height", info->height},
                {"mips", info->mips},
                {"format", std::string(asset::format_name(info->format))},
                {"gpu_bytes", info->gpu_bytes},
                {"rgba8_bytes", info->rgba8_bytes}};
}

Status Document::set_texture_settings(std::string_view texture_path, const Json& settings) {
    auto file = project_.resolve(texture_path);
    if (!file) return std::unexpected(file.error());
    if (!std::filesystem::exists(std::filesystem::path(file->string() + ".noormeta")))
        return fail(ErrorCode::NotFound, std::format("{} has no .noormeta (Rescan first)", texture_path));
    return asset::set_texture_settings(*file, asset::TextureSettings::from_json(settings));
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
    stroke_.reset();
    terrain_edits_.clear();  // unsaved sculpting goes with the unsaved scene
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
    finish_stroke();
    if (auto s = scene_->save(scene_file_); !s) return s;
    save_sculpt();
    cook_csg();  // games (noor_player on the project) load what was saved
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
    finish_stroke();

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
    } else if (op == "move") {
        // {"op": "move", "entity": id, "before": sibling id | null (= last)}
        std::optional<Guid> before;
        if (command.contains("before") && !command["before"].is_null()) {
            auto b = entity_field(command, "before");
            if (!b) return std::unexpected(b.error());
            before = *b;
        }
        applied = history_.execute(*scene_, scene::move_before(*id, before));
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
    finish_stroke();
    if (!scene_ || world_ || !history_.undo(*scene_)) return false;
    changed();
    return true;
}

bool Document::redo() {
    finish_stroke();
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
    snap->terrains = std::move(view.terrains);
    // Sculpted terrains: the live heights (what is being sculpted, saved or not).
    for (auto& t : snap->terrains) {
        if (!t.sculpt) continue;
        if (auto edit = terrain_edit(*t.sculpt, t.settings)) {
            t.heights = edit->snapshot();
            t.tile_versions = edit->tile_versions;
        }
    }
    snap->lighting = view.lighting;
    snap->environment = view.environment;
    for (const auto& lm : view.light_markers) snap->markers.push_back({math::translation(lm.position), lm.entity, lm.color});
    snap->texture_cache = project_.root() / ".noor" / "cache" / "textures";
    snap->asset_files = asset_files();
    if (world_) {
        snap->camera = view.camera;
        snap->ui = world_->ui_commands();
    }
    // CSG models: built from their brush children in order (ADR-016), cached per model.
    csg_last_.clear();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i].has("csg_model")) continue;
        auto mesh = build_csg(expanded, i);
        if (!mesh) continue;
        snap->csg.push_back({entities[i].id, expanded.world_matrix(i), mesh});
        csg_last_.emplace_back(entities[i].id, std::move(mesh));
    }
    for (auto& m : view.meshes) {
        std::string key = m.asset_path.generic_string();
        const auto version = asset_versions_.contains(key) ? asset_versions_.at(key) : 0;
        snap->meshes.push_back({std::move(key), m.mesh_index, m.world, m.entity, version});
    }
    channel_->publish(std::move(snap));
}

// ---- CSG (ADR-016) ----

namespace {

float json_float(const Json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}
math::Vec2 json_vec2(const Json& j, const char* key, math::Vec2 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 2 || !j[key][0].is_number() || !j[key][1].is_number()) return fallback;
    return {j[key][0].get<float>(), j[key][1].get<float>()};
}

csg::Shape shape_of(const std::string& name) {
    for (const auto s : {csg::Shape::Box, csg::Shape::Wedge, csg::Shape::Cylinder, csg::Shape::Cone, csg::Shape::Sphere, csg::Shape::Stairs, csg::Shape::Arch})
        if (csg::shape_name(s) == name) return s;
    return csg::Shape::Box;
}

} // namespace

std::shared_ptr<const CsgMesh> Document::build_csg(const scene::Scene& expanded, std::size_t model_index) {
    const auto& entities = expanded.entities;
    const Guid model_id = entities[model_index].id;
    const std::uint64_t key = game::csg_model_key(expanded, model_index);
    if (const auto it = csg_cache_.find(model_id); it != csg_cache_.end() && it->second->key == key) return it->second;

    const auto start = std::chrono::steady_clock::now();
    auto mesh = std::make_shared<CsgMesh>();
    mesh->key = key;
    const csg::SceneModel sm = csg::brushes_from_scene(expanded, model_index);
    mesh->material_ids = sm.slot_materials;
    for (const auto& id : sm.slot_materials) {
        std::filesystem::path file;
        if (id)
            if (const auto* rec = registry_.find(*id))
                if (auto resolved = project_.resolve(rec->path)) file = *resolved;
        mesh->materials.push_back(std::move(file));
    }
    const auto& brushes = sm.brushes;
    const auto& brush_ids = sm.brush_ids;
    for (std::size_t i = 0; i < brushes.size(); ++i) {
        if (brushes[i].operation == csg::Operation::Add) continue;
        const auto bm = csg::brush_mesh(brushes[i]);
        CsgMesh::Ghost ghost{brush_ids[i], brushes[i].operation == csg::Operation::Intersect, {}};
        for (const auto index : bm.indices) ghost.positions.push_back(bm.positions[index]);
        mesh->ghosts.push_back(std::move(ghost));
    }
    mesh->brush_count = brushes.size();

    auto built = csg::build_model(brushes);
    if (!built) {
        mesh->warnings.push_back(built.error().describe());
    } else {
        mesh->warnings = built->warnings;
        mesh->volume = built->volume;
        mesh->triangles = built->triangle_count();
        mesh->vertices.reserve(built->vertices.size());
        for (const auto& v : built->vertices)
            mesh->vertices.push_back({v.position, v.normal, {1, 1, 1}, v.uv, v.tangent});
        mesh->indices = std::move(built->indices);
        // Runs of the same (brush, material) become one part of that brush's group.
        std::unordered_map<std::uint32_t, std::size_t> group_of;
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> face_triangles;
        mesh->triangle_face.reserve(built->triangle_source.size());
        for (std::size_t t = 0; t < built->triangle_source.size(); ++t) {
            const std::uint32_t brush = built->triangle_source[t].brush;
            const std::uint32_t slot = brushes[brush].surface_of(built->triangle_source[t].face).material;
            mesh->triangle_face.push_back(built->triangle_source[t].face);
            ++face_triangles[{brush, built->triangle_source[t].face}];
            auto [git, added] = group_of.try_emplace(brush, mesh->groups.size());
            if (added) mesh->groups.push_back({brush_ids[brush], {}});
            auto& parts = mesh->groups[git->second].parts;
            const auto first = static_cast<std::uint32_t>(t * 3);
            if (!parts.empty() && parts.back().slot == slot && parts.back().first_index + parts.back().index_count == first) parts.back().index_count += 3;
            else parts.push_back({first, 3, slot});
        }
        for (const auto& [brush_face, count] : face_triangles)
            mesh->faces.push_back({brush_ids[brush_face.first], brush_face.second, brushes[brush_face.first].surface_of(brush_face.second).material, count});
    }
    mesh->build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    for (const auto& w : mesh->warnings) NOOR_LOG_WARN("CSG", "{}: {}", entities[model_index].name, w);
    csg_cache_[model_id] = mesh;
    return mesh;
}

void Document::cook_csg() {
    for (const auto& [id, mesh] : csg_last_) {
        const auto file = game::cooked_csg_path(project_.root(), id);
        if (auto old = game::read_cooked_csg(file); old && old->key == mesh->key) continue;
        game::CookedCsg cooked;
        cooked.key = mesh->key;
        cooked.vertices = mesh->vertices;
        cooked.indices = mesh->indices;
        // The draw parts are runs per (material, brush); games need one run per material.
        std::vector<CsgMesh::Part> parts;
        for (const auto& g : mesh->groups) parts.insert(parts.end(), g.parts.begin(), g.parts.end());
        std::ranges::sort(parts, {}, &CsgMesh::Part::first_index);
        for (const auto& part : parts) {
            const auto material = part.slot < mesh->material_ids.size() ? mesh->material_ids[part.slot] : std::nullopt;
            if (!cooked.submeshes.empty() && cooked.submeshes.back().material == material &&
                cooked.submeshes.back().first_index + cooked.submeshes.back().index_count == part.first_index)
                cooked.submeshes.back().index_count += part.index_count;
            else
                cooked.submeshes.push_back({part.first_index, part.index_count, material});
        }
        if (auto w = game::write_cooked_csg(file, cooked); !w) NOOR_LOG_WARN("CSG", "Couldn't cook {}: {}", id.to_string(), w.error().describe());
    }
}

Result<Json> Document::export_csg(const Guid& model, const std::filesystem::path& glb) {
    const auto it = std::ranges::find_if(csg_last_, [&](const auto& m) { return m.first == model; });
    if (it == csg_last_.end()) return fail(ErrorCode::NotFound, "no CSG model with that id in the open scene");
    const CsgMesh& mesh = *it->second;
    asset::ExportModel em;
    const auto* entity = scene_ ? [&]() -> const scene::Entity* {
        for (const auto& e : scene_->entities)
            if (e.id == model) return &e;
        return nullptr;
    }() : nullptr;
    // A readable, file-safe name: the entity's, else the file's.
    std::string name = entity && !entity->name.empty() ? entity->name : glb.stem().string();
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    em.name = name;
    em.id = model;
    em.generator_version = project_.info().engine_version;
    em.vertices.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) em.vertices.push_back({v.position, v.normal, v.color, v.uv, v.tangent});
    em.indices = mesh.indices;
    for (const auto& g : mesh.groups)
        for (const auto& part : g.parts)
            em.parts.push_back({part.first_index, part.index_count, part.slot < mesh.materials.size() ? mesh.materials[part.slot] : std::filesystem::path{}});
    std::ranges::sort(em.parts, {}, &asset::ExportModel::Part::first_index);
    em.collision = entity && entity->has("collider");
    std::vector<std::string> warnings;
    auto manifest = asset::export_noor_glb(em, game::file_resolver(game::registry_asset_path(project_, registry_)), glb, &warnings);
    if (!manifest) return std::unexpected(manifest.error());
    for (const auto& w : warnings) NOOR_LOG_WARN("Export", "{}", w);
    NOOR_LOG_INFO("Export", "Exported {} ({} triangles) to {}", name, mesh.triangles, glb.generic_string());
    return Json{{"path", glb.generic_string()}, {"manifest", *manifest}, {"warnings", warnings}};
}

Json Document::csg_info() const {
    Json out = Json::array();
    for (const auto& [id, mesh] : csg_last_) {
        Json faces = Json::array();
        for (const auto& f : mesh->faces) {
            const auto& file = f.slot < mesh->materials.size() ? mesh->materials[f.slot] : std::filesystem::path{};
            const auto rel = file.empty() ? std::optional<std::string>{} : project_.relative(file) ? std::optional(*project_.relative(file)) : std::nullopt;
            faces.push_back({{"brush", f.brush.to_string()}, {"face", f.face}, {"material", rel ? Json(*rel) : Json(nullptr)}, {"triangles", f.triangles}});
        }
        out.push_back({{"model", id.to_string()},
                       {"faces", std::move(faces)},
                       {"brushes", mesh->brush_count},
                       {"triangles", mesh->triangles},
                       {"volume", mesh->volume},
                       {"build_ms", mesh->build_ms},
                       {"warnings", mesh->warnings}});
    }
    return out;
}

} // namespace noor::editor

// ---- Terrain sculpting (Milestone 6, part 6) ----

namespace noor::editor {

namespace {

// The tiles whose meshes change when the samples in `r` change (normals reach one sample further).
void for_tiles(const terrain::SampleRect& r, std::uint32_t tiles_x, std::uint32_t tiles_z, auto&& fn) {
    const std::uint32_t xa = r.x0 > 0 ? r.x0 - 1 : 0, za = r.z0 > 0 ? r.z0 - 1 : 0;
    const std::uint32_t xb = r.x1 + 1, zb = r.z1 + 1;
    const std::uint32_t tx0 = xa > 0 ? (xa - 1) / terrain::kTileCells : 0, tz0 = za > 0 ? (za - 1) / terrain::kTileCells : 0;
    const std::uint32_t tx1 = std::min(xb / terrain::kTileCells, tiles_x - 1), tz1 = std::min(zb / terrain::kTileCells, tiles_z - 1);
    for (std::uint32_t tz = tz0; tz <= tz1; ++tz)
        for (std::uint32_t tx = tx0; tx <= tx1; ++tx) fn(tx, tz);
}

// One sculpt stroke as an undo step: the offsets of a rectangle before and after.
class SculptCommand final : public scene::Command {
public:
    SculptCommand(std::shared_ptr<TerrainEdit> edit, terrain::SampleRect rect, std::vector<float> before, std::vector<float> after, std::string label)
        : edit_(std::move(edit)), rect_(rect), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {}
    [[nodiscard]] std::string label() const override { return label_; }
    bool apply(scene::Scene&) override {
        edit_->write(rect_, after_);
        return true;
    }
    void revert(scene::Scene&) override { edit_->write(rect_, before_); }

private:
    std::shared_ptr<TerrainEdit> edit_;
    terrain::SampleRect rect_;
    std::vector<float> before_, after_;
    std::string label_;
};

std::string safe_file_name(std::string name) {
    for (char& ch : name)
        if (std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos || static_cast<unsigned char>(ch) < 32) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? std::string("Terrain") : name;
}

} // namespace

void TerrainEdit::touched(const terrain::SampleRect& rect) {
    current->recompose(*base, layer, rect);
    ++revision;
    for_tiles(rect, current->tiles_x(), current->tiles_z(), [&](std::uint32_t tx, std::uint32_t tz) {
        tile_versions[static_cast<std::size_t>(tz) * current->tiles_x() + tx] = revision;
    });
}

void TerrainEdit::write(const terrain::SampleRect& rect, const std::vector<float>& offsets) {
    std::size_t i = 0;
    for (std::uint32_t z = rect.z0; z <= rect.z1; ++z)
        for (std::uint32_t x = rect.x0; x <= rect.x1; ++x) layer.set(x, z, offsets[i++]);
    touched(rect);
    dirty = true;
}

std::shared_ptr<const terrain::Heights> TerrainEdit::snapshot() {
    if (published_revision != revision || !published) {
        published = std::make_shared<const terrain::Heights>(*current);
        published_revision = revision;
    }
    return published;
}

std::shared_ptr<TerrainEdit> Document::terrain_edit(const Guid& asset, const terrain::Settings& settings) {
    auto& slot = terrain_edits_[asset];
    if (!slot) {
        auto edit = std::make_shared<TerrainEdit>();
        edit->asset = asset;
        if (const auto* rec = registry_.find(asset))
            if (auto file = project_.resolve(rec->path)) edit->file = *file;
        if (edit->file.empty()) {
            NOOR_LOG_WARN("Editor", "Terrain: its sculpt layer {} isn't in the project", asset.to_string());
            terrain_edits_.erase(asset);
            return nullptr;
        }
        if (auto layer = terrain::SculptLayer::load(edit->file)) {
            edit->layer = std::move(*layer);
        } else {
            NOOR_LOG_ERROR("Editor", "Terrain: {} (starting from an empty layer; saving will replace it)", layer.error().describe());
            edit->layer = terrain::SculptLayer(settings.tiles_x, settings.tiles_z);
        }
        slot = std::move(edit);
    }
    TerrainEdit& e = *slot;
    e.layer.resize(settings.tiles_x, settings.tiles_z);
    const auto base_settings = settings.base_only();
    if (auto key = base_settings.key(); key != e.base_key || !e.current) {
        auto base = terrain::Heights::shared(base_settings);
        if (!base) {
            NOOR_LOG_WARN("Editor", "Terrain: {}", base.error().describe());
            return nullptr;
        }
        // A new base (another seed, size or heightmap): the same offsets on top of it.
        e.base = *base;
        e.current = std::make_shared<terrain::Heights>(terrain::Heights::compose(*e.base, e.layer));
        e.base_key = std::move(key);
        ++e.revision;
        e.tile_versions.assign(static_cast<std::size_t>(settings.tiles_x) * settings.tiles_z, e.revision);
    }
    return slot;
}

void Document::save_sculpt() {
    for (auto& [id, edit] : terrain_edits_) {
        if (!edit->dirty) continue;
        if (auto s = edit->layer.save(edit->file); !s) {
            NOOR_LOG_ERROR("Editor", "Terrain: can't save {}: {}", edit->file.filename().string(), s.error().describe());
            continue;
        }
        edit->dirty = false;
    }
}

void Document::apply_dabs(const Json& dabs) {
    if (!stroke_ || !dabs.is_array()) return;
    Stroke& s = *stroke_;
    for (const auto& d : dabs) {
        float p[3];
        if (!d.is_object() || !d.contains("point") || !read_vec(d["point"], 3, p)) continue;
        const float seconds = std::clamp(json_float(d, "seconds", 0.0f), 0.0f, 0.25f);  // a stall doesn't make a mountain
        const math::Vec2 local{p[0] - s.position.x, p[2] - s.position.z};
        if (s.first_dab) {
            s.brush.flatten_height = s.edit->current->height(local.x, local.y);  // Flatten levels to where it began
            s.first_dab = false;
        }
        if (auto rect = terrain_edit::apply(*s.edit->current, s.edit->layer, s.brush, local, seconds)) {
            s.edit->touched(*rect);
            s.edit->dirty = true;
            if (s.changed) s.changed->include(*rect);
            else s.changed = *rect;
        }
    }
}

void Document::finish_stroke() {
    if (!stroke_) return;
    Stroke s = std::move(*stroke_);
    stroke_.reset();
    if (s.changed && scene_) {
        const auto& r = *s.changed;
        std::vector<float> before, after;
        before.reserve(r.count());
        after.reserve(r.count());
        for (std::uint32_t z = r.z0; z <= r.z1; ++z)
            for (std::uint32_t x = r.x0; x <= r.x1; ++x) {
                before.push_back(s.before.offset(x, z));
                after.push_back(s.edit->layer.offset(x, z));
            }
        history_.execute(*scene_, std::make_unique<SculptCommand>(s.edit, r, std::move(before), std::move(after), "Sculpt"));
    }
    history_.end_transaction();  // opened when the stroke began
    changed();
}

Result<Json> Document::sculpt(const Json& command) {
    if (auto s = require_editable(); !s) return std::unexpected(s.error());
    const std::string phase = command.value("phase", std::string("dab"));
    if (phase == "begin") {
        finish_stroke();
        auto id = entity_field(command, "terrain");
        if (!id) return std::unexpected(id.error());
        const scene::Entity* target = scene_->find(*id);
        if (!target || !target->has("terrain") || !target->components["terrain"].is_object())
            return fail(ErrorCode::InvalidArgument, "sculpting needs an editable entity with a terrain component");
        terrain_edit::Brush brush;
        if (auto tool = terrain_edit::tool_from_name(command.value("tool", std::string("raise")))) brush.tool = *tool;
        else return fail(ErrorCode::InvalidArgument, "tool is raise, lower, smooth or flatten");
        brush.radius = std::clamp(json_float(command, "radius", brush.radius), 0.1f, 500.0f);
        brush.strength = std::clamp(json_float(command, "strength", brush.strength), 0.0f, 1000.0f);
        brush.falloff = std::clamp(json_float(command, "falloff", brush.falloff), 0.0f, 1.0f);

        Json component = target->components["terrain"];
        auto sculpt_id = asset::guid_field(component, "sculpt");
        std::string label(terrain_edit::tool_name(brush.tool));
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        history_.begin_transaction("Sculpt: " + label);  // ended by finish_stroke
        if (!sculpt_id || !registry_.find(*sculpt_id)) {
            // The terrain's first stroke: a sculpt layer file of its own, next to the scene's assets.
            const auto settings = terrain::Settings::from_json(component, {});
            const auto dir = project_.assets_dir() / "Terrain";
            std::error_code ec;
            stdfs::create_directories(dir, ec);
            const std::string stem = safe_file_name(target->name);
            stdfs::path file = dir / (stem + std::string(terrain::SculptLayer::kExtension));
            for (int n = 2; stdfs::exists(file); ++n) file = dir / std::format("{} {}{}", stem, n, terrain::SculptLayer::kExtension);
            if (auto w = terrain::SculptLayer(settings.tiles_x, settings.tiles_z).save(file); !w) {
                history_.end_transaction();
                return std::unexpected(w.error());
            }
            if (auto r = registry_.scan(project_); !r) {
                history_.end_transaction();
                return std::unexpected(r.error());
            }
            const auto rel = project_.relative(file);
            const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
            if (!rec) {
                history_.end_transaction();
                return fail(ErrorCode::Unknown, "the new sculpt layer wasn't registered as an asset");
            }
            sculpt_id = rec->guid;
            component["sculpt"] = rec->guid.to_string();
            history_.execute(*scene_, scene::set_component(*id, "terrain", component));
        }
        const auto settings = terrain::Settings::from_json(component, game::file_resolver(game::registry_asset_path(project_, registry_)));
        auto edit = terrain_edit(*sculpt_id, settings);
        if (!edit) {
            history_.end_transaction();
            return fail(ErrorCode::Unknown, "the terrain's heights couldn't be built (see the log)");
        }
        // Where the terrain is: its entity's world position (it may have parents).
        const auto expanded = scene::expand_prefabs(*scene_, prefabs_.resolver());
        math::Vec3 position = target->transform.position;
        for (std::size_t i = 0; i < expanded.scene.entities.size(); ++i)
            if (expanded.scene.entities[i].id == *id) position = expanded.scene.world_matrix(i).transform_point({0, 0, 0});
        stroke_ = Stroke{*id, edit, brush, position, edit->layer, std::nullopt, true};
    }
    if (!stroke_) return fail(ErrorCode::InvalidArgument, "no sculpt stroke has begun");
    apply_dabs(command.value("dabs", Json::array()));
    const Guid asset = stroke_->edit->asset;
    const bool any = stroke_->changed.has_value();
    if (phase == "end") finish_stroke();
    else changed();
    return Json{{"sculpt", asset.to_string()}, {"changed", any}};
}

} // namespace noor::editor
