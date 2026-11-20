#include "document.h"

#include "noor/asset/environment_asset.h"
#include "noor/fx/fx_asset.h"
#include "noor/fx/particle_system.h"
#include "noor/fx/presets.h"
#include "noor/asset/material.h"
#include "noor/asset/texture.h"
#include "noor/asset/sprite_asset.h"
#include "noor/csg/csg.h"
#include "noor/csg/scene_csg.h"
#include "noor/asset/gltf_export.h"
#include "noor/game/cooked_csg.h"
#include "noor/foundation/file_io.h"

#include "noor/asset/gltf_import.h"
#include "noor/asset/model_import.h"
#include "noor/asset/interchange.h"
#include "noor/game/game.h"
#include "noor/game/scatter.h"
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
        forget_animation_files();
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
    options.terrain_heights = [this](const Json& component, const terrain::Settings&) -> std::shared_ptr<const terrain::Heights> {
        const auto id = asset::guid_field(component, "sculpt");
        if (!id) return nullptr;
        for (auto& [entity, edit] : terrain_edits_)  // brought up to date by the last publish
            if (edit->sculpt_active && edit->sculpt_asset == id) return edit->snapshot();
        return nullptr;
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

// ------------------------------------------------------------------ environment assets (part 6)

namespace {

// A file name made from an environment's name (letters, digits, spaces, dashes, underscores kept).
std::string environment_file_name(std::string_view name) {
    std::string out;
    for (const char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return (out.empty() ? std::string("Environment") : out) + std::string(asset::kEnvironmentExtension);
}

} // namespace

Result<Json> Document::environment(const Json& command) {
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
        return fail(ErrorCode::InvalidArgument, "environment: the command needs an \"op\"");
    const std::string op = command["op"];
    const stdfs::path folder = project_.assets_dir() / "Environments";
    const auto rescan = [&]() -> Status {
        if (auto s = registry_.scan(project_); !s) return std::unexpected(s.error());
        return {};
    };
    const auto describe = [&](const stdfs::path& file) -> Result<Json> {
        auto json = asset::load_environment_file(file);
        if (!json) return std::unexpected(json.error());
        const auto rel = project_.relative(file);
        const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
        Json out{{"path", rel ? *rel : file.generic_string()}, {"guid", rec ? rec->guid.to_string() : std::string()},
                 {"name", json->value("name", file.stem().string())}, {"preset", json->value("preset", std::string())},
                 {"environment", (*json)["environment"]}};
        return out;
    };
    const auto file_of = [&](const Json& c) -> Result<stdfs::path> {
        if (!c.contains("asset") || !c["asset"].is_string()) return fail(ErrorCode::InvalidArgument, "environment: \"asset\" (a project path) is needed");
        auto file = project_.resolve(c["asset"].get<std::string>());
        if (!file) return std::unexpected(file.error());
        if (!stdfs::exists(*file)) return fail(ErrorCode::NotFound, std::format("no environment at {}", c["asset"].get<std::string>()));
        return *file;
    };
    const auto write = [&](const stdfs::path& file, const Json& json) -> Status {
        std::error_code ec;
        stdfs::create_directories(file.parent_path(), ec);
        return noor::fs::write_file_atomic(file, asset::to_text(json), false);
    };

    if (op == "list") {
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        Json list = Json::array();
        for (const auto& rec : registry_.assets())
            if (rec.importer == "environment")
                if (auto d = describe(project_.root() / rec.path)) list.push_back(*d);
        return list;
    }
    if (op == "read") {
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(*file);
    }
    if (op == "save") {  // edit: new settings (and optionally a new name); the preset it came from is kept
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        if (!command.contains("environment") || !command["environment"].is_object())
            return fail(ErrorCode::InvalidArgument, "environment save: \"environment\" (an object) is needed");
        auto old = asset::load_environment_file(*file);
        if (!old) return std::unexpected(old.error());
        const std::string name = command.value("name", old->value("name", file->stem().string()));
        if (auto w = write(*file, asset::environment_file_json(name, command["environment"], old->value("preset", std::string()))); !w)
            return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(*file);
    }
    if (op == "duplicate") {
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        auto old = asset::load_environment_file(*file);
        if (!old) return std::unexpected(old.error());
        std::string base = command.value("name", old->value("name", file->stem().string()) + " copy");
        stdfs::path target = file->parent_path() / environment_file_name(base);
        for (int n = 2; stdfs::exists(target); ++n) target = file->parent_path() / environment_file_name(std::format("{} {}", base, n));
        const std::string name = target.stem().string();
        if (auto w = write(target, asset::environment_file_json(name, (*old)["environment"], old->value("preset", std::string()))); !w)
            return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(target);
    }
    if (op == "reset") {  // back to the built-in preset it came from (the presets themselves are never written)
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        auto old = asset::load_environment_file(*file);
        if (!old) return std::unexpected(old.error());
        const auto* preset = asset::find_environment_preset(old->value("preset", std::string()));
        if (!preset) return fail(ErrorCode::InvalidArgument, std::format("{} didn't come from a preset: nothing to reset it to", file->filename().string()));
        if (auto w = write(*file, asset::environment_file_json(old->value("name", preset->name), preset->environment, preset->name)); !w)
            return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(*file);
    }
    if (op == "apply") {  // the scene's environment becomes {"asset": id}: one undo step
        if (auto s = require_editable(); !s) return std::unexpected(s.error());
        stdfs::path file;
        if (command.contains("preset")) {
            const auto* preset = asset::find_environment_preset(command.value("preset", std::string()));
            if (!preset) return fail(ErrorCode::NotFound, std::format("no environment preset \"{}\"", command.value("preset", std::string())));
            file = folder / environment_file_name(preset->name);
            if (!stdfs::exists(file))  // a project copy, kept (with its edits) once made
                if (auto w = write(file, asset::environment_file_json(preset->name, preset->environment, preset->name)); !w)
                    return std::unexpected(w.error());
        } else {
            auto f = file_of(command);
            if (!f) return std::unexpected(f.error());
            file = *f;
        }
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        auto d = describe(file);
        if (!d) return std::unexpected(d.error());
        const Json component{{"asset", (*d)["guid"]}};
        const scene::Entity* holder = nullptr;
        for (const auto& e : scene_->entities)
            if (e.has("environment")) {
                holder = &e;
                break;
            }
        Status applied = holder ? execute({{"op", "set_component"}, {"entity", holder->id.to_string()}, {"type", "environment"}, {"value", component}})
                                : execute({{"op", "create"}, {"name", "Environment"}, {"components", {{"environment", component}}}});
        if (!applied) return std::unexpected(applied.error());
        return d;
    }
    return fail(ErrorCode::InvalidArgument, std::format("environment: unknown op \"{}\"", op));
}

// ------------------------------------------------------------------ effect assets (8B part 4)

namespace {

std::string effect_file_name(std::string_view name) {
    std::string out;
    for (const char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return (out.empty() ? std::string("Effect") : out) + std::string(fx::kFxExtension);
}

} // namespace

Result<Json> Document::effect(const Json& command) {
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
        return fail(ErrorCode::InvalidArgument, "effect: the command needs an \"op\"");
    const std::string op = command["op"];
    const stdfs::path folder = project_.assets_dir() / "Effects";
    const auto rescan = [&]() -> Status {
        if (auto s = registry_.scan(project_); !s) return std::unexpected(s.error());
        return {};
    };
    const auto describe = [&](const stdfs::path& file) -> Result<Json> {
        auto json = fx::load_fx_file(file);
        if (!json) return std::unexpected(json.error());
        const auto rel = project_.relative(file);
        const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
        return Json{{"path", rel ? *rel : file.generic_string()}, {"guid", rec ? rec->guid.to_string() : std::string()},
                    {"name", json->value("name", file.stem().string())}, {"preset", json->value("preset", std::string())},
                    {"emitter", (*json)["emitter"]}};
    };
    const auto file_of = [&](const Json& c) -> Result<stdfs::path> {
        if (!c.contains("asset") || !c["asset"].is_string()) return fail(ErrorCode::InvalidArgument, "effect: \"asset\" (a project path) is needed");
        auto file = project_.resolve(c["asset"].get<std::string>());
        if (!file) return std::unexpected(file.error());
        if (!stdfs::exists(*file)) return fail(ErrorCode::NotFound, std::format("no effect at {}", c["asset"].get<std::string>()));
        return *file;
    };
    const auto write = [&](const stdfs::path& file, const Json& json) -> Status {
        std::error_code ec;
        stdfs::create_directories(file.parent_path(), ec);
        return noor::fs::write_file_atomic(file, asset::to_text(json), false);
    };

    if (op == "list") {
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        Json list = Json::array();
        for (const auto& rec : registry_.assets())
            if (rec.importer == "fx")
                if (auto d = describe(project_.root() / rec.path)) list.push_back(*d);
        return list;
    }
    if (op == "read") {
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(*file);
    }
    if (op == "save") {  // edit: new settings (and optionally a new name); the preset it came from is kept
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        if (!command.contains("emitter") || !command["emitter"].is_object())
            return fail(ErrorCode::InvalidArgument, "effect save: \"emitter\" (an object) is needed");
        auto old = fx::load_fx_file(*file);
        if (!old) return std::unexpected(old.error());
        const std::string name = command.value("name", old->value("name", file->stem().string()));
        if (auto w = write(*file, fx::fx_file_json(name, command["emitter"], old->value("preset", std::string()))); !w) return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        if (!world_) changed();  // the preview takes the new settings now
        return describe(*file);
    }
    if (op == "duplicate") {
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        auto old = fx::load_fx_file(*file);
        if (!old) return std::unexpected(old.error());
        const std::string base = command.value("name", old->value("name", file->stem().string()) + " copy");
        stdfs::path target = file->parent_path() / effect_file_name(base);
        for (int n = 2; stdfs::exists(target); ++n) target = file->parent_path() / effect_file_name(std::format("{} {}", base, n));
        if (auto w = write(target, fx::fx_file_json(target.stem().string(), (*old)["emitter"], old->value("preset", std::string()))); !w)
            return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(target);
    }
    if (op == "reset") {  // back to the built-in preset it came from (the presets themselves are never written)
        auto file = file_of(command);
        if (!file) return std::unexpected(file.error());
        auto old = fx::load_fx_file(*file);
        if (!old) return std::unexpected(old.error());
        const auto* preset = fx::find_effect_preset(old->value("preset", std::string()));
        if (!preset) return fail(ErrorCode::InvalidArgument, std::format("{} didn't come from a preset: nothing to reset it to", file->filename().string()));
        if (auto w = write(*file, fx::fx_file_json(old->value("name", preset->name), preset->emitter, preset->name)); !w) return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        if (!world_) changed();
        return describe(*file);
    }
    if (op == "apply") {  // an entity's emitter becomes {"asset": id}, or a new entity gets one: one undo step
        if (auto s = require_editable(); !s) return std::unexpected(s.error());
        stdfs::path file;
        if (command.contains("preset")) {
            const auto* preset = fx::find_effect_preset(command.value("preset", std::string()));
            if (!preset) return fail(ErrorCode::NotFound, std::format("no effect preset \"{}\"", command.value("preset", std::string())));
            file = folder / effect_file_name(preset->name);
            if (!stdfs::exists(file))  // a project copy, kept (with its edits) once made
                if (auto w = write(file, fx::fx_file_json(preset->name, preset->emitter, preset->name)); !w) return std::unexpected(w.error());
        } else {
            auto f = file_of(command);
            if (!f) return std::unexpected(f.error());
            file = *f;
        }
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        auto d = describe(file);
        if (!d) return std::unexpected(d.error());
        const Json component{{"asset", (*d)["guid"]}};
        Json result = *d;
        if (command.contains("entity") && command["entity"].is_string()) {
            const std::string id = command["entity"].get<std::string>();
            if (auto s = execute({{"op", "set_component"}, {"entity", id}, {"type", "emitter"}, {"value", component}}); !s) return std::unexpected(s.error());
            result["entity"] = id;
        } else {
            const Guid id = Guid::generate();
            Json create{{"op", "create"}, {"id", id.to_string()}, {"name", (*d)["name"]}, {"components", {{"emitter", component}}}};
            if (command.contains("position") && command["position"].is_array()) create["transform"] = Json{{"position", command["position"]}};
            if (auto s = execute(create); !s) return std::unexpected(s.error());
            result["entity"] = id.to_string();
        }
        return result;
    }
    if (op == "preview") {  // the Effects panel's controls: solo, mute, restart, pause (published to the viewports)
        if (command.contains("solo")) {
            preview_solo_.reset();  // null (or anything not an id): no solo
            if (command["solo"].is_string())
                if (const auto id = Guid::parse(command["solo"].get<std::string>())) preview_solo_ = *id;
        }
        if (command.contains("muted") && command["muted"].is_array()) {
            preview_muted_.clear();
            for (const auto& m : command["muted"])
                if (m.is_string())
                    if (const auto id = Guid::parse(m.get<std::string>())) preview_muted_.push_back(*id);
        }
        if (command.value("restart", false)) ++preview_restart_;
        if (command.contains("paused") && command["paused"].is_boolean()) preview_paused_ = command["paused"].get<bool>();
        if (!world_) changed();
        Json muted = Json::array();
        for (const auto& id : preview_muted_) muted.push_back(id.to_string());
        return Json{{"solo", preview_solo_ ? Json(preview_solo_->to_string()) : Json(nullptr)}, {"muted", muted},
                    {"restart", preview_restart_}, {"paused", preview_paused_}};
    }
    return fail(ErrorCode::InvalidArgument, std::format("effect: unknown op \"{}\"", op));
}

// ------------------------------------------------------------------ sprite sheets (8B part 5)

Result<Json> Document::sprite(const Json& command) {
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
        return fail(ErrorCode::InvalidArgument, "sprite: the command needs an \"op\"");
    const std::string op = command["op"];
    const auto rescan = [&]() -> Status {
        if (auto s = registry_.scan(project_); !s) return std::unexpected(s.error());
        return {};
    };
    const auto relative = [&](const stdfs::path& file) {
        const auto rel = project_.relative(file);
        return rel ? *rel : file.generic_string();
    };
    const auto file_of = [&](const Json& c, const char* key) -> Result<stdfs::path> {
        if (!c.contains(key) || !c[key].is_string()) return fail(ErrorCode::InvalidArgument, std::format("sprite: \"{}\" (a project path) is needed", key));
        auto file = project_.resolve(c[key].get<std::string>());
        if (!file) return std::unexpected(file.error());
        if (!stdfs::exists(*file)) return fail(ErrorCode::NotFound, std::format("nothing at {}", c[key].get<std::string>()));
        return *file;
    };
    // A sheet's settings checked against its texture: the sliced frames, or why it can't be sliced ("error").
    const auto sliced = [&](const Json& settings, Json& out) -> Status {
        auto sheet = asset::parse_sprite_sheet(settings);
        if (!sheet) return std::unexpected(sheet.error());
        const auto* tex = sheet->texture ? registry_.find(*sheet->texture) : nullptr;
        if (!tex) {
            out["error"] = "its texture isn't in the project";
            return {};
        }
        out["texture_path"] = tex->path;
        auto info = asset::texture_info(project_.root() / tex->path, asset::TextureSettings{});
        if (!info) {
            out["error"] = info.error().message;
            return {};
        }
        out["image"] = {{"width", info->source_width}, {"height", info->source_height}};
        if (auto s = asset::slice(*sheet, static_cast<int>(info->source_width), static_cast<int>(info->source_height)); !s) {
            out["error"] = s.error().message;
            return {};
        }
        Json frames = Json::array();
        for (const auto& f : sheet->frames) frames.push_back(Json::array({f.x, f.y, f.width, f.height}));
        out["frames"] = std::move(frames);
        out["columns"] = sheet->columns;
        out["rows"] = sheet->rows;
        out["frame_size_m"] = Json::array({sheet->frame_size_m().x, sheet->frame_size_m().y});
        return {};
    };
    const auto describe = [&](const stdfs::path& file) -> Result<Json> {
        auto text = noor::fs::read_file(file, 1 << 20);
        if (!text) return std::unexpected(text.error());
        auto json = asset::parse_json(*text, file.generic_string());
        if (!json) return std::unexpected(json.error());
        const auto rel = project_.relative(file);
        const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
        Json out{{"path", relative(file)}, {"guid", rec ? rec->guid.to_string() : std::string()},
                 {"name", json->value("name", file.stem().string())}, {"sheet", *json}};
        if (auto s = sliced(*json, out); !s) out["error"] = s.error().message;
        return out;
    };
    const auto write = [&](const stdfs::path& file, const Json& json) -> Status {
        std::error_code ec;
        stdfs::create_directories(file.parent_path(), ec);
        return noor::fs::write_file_atomic(file, asset::to_text(json), false);
    };

    if (op == "list") {
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        Json list = Json::array();
        for (const auto& rec : registry_.assets())
            if (rec.importer == "sprite")
                if (auto d = describe(project_.root() / rec.path)) list.push_back(*d);
        return list;
    }
    if (op == "read") {
        auto file = file_of(command, "asset");
        if (!file) return std::unexpected(file.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(*file);
    }
    if (op == "create") {  // a sheet for a texture, beside it: one frame (the whole image) until a grid is set
        auto texture = file_of(command, "texture");
        if (!texture) return std::unexpected(texture.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        const auto rel = project_.relative(*texture);
        const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
        if (!rec || rec->importer != "texture") return fail(ErrorCode::InvalidArgument, std::format("{} isn't a texture", relative(*texture)));
        const std::string name = command.value("name", texture->stem().string());
        stdfs::path target = texture->parent_path() / (name + std::string(asset::kSpriteExtension));
        for (int n = 2; stdfs::exists(target); ++n) target = texture->parent_path() / std::format("{} {}{}", name, n, asset::kSpriteExtension);
        Json sheet{{"noor_sprite", asset::kSpriteFormat},
                   {"name", target.stem().string()},
                   {"texture", rec->guid.to_string()},
                   {"grid", command.contains("grid") && command["grid"].is_object() ? command["grid"] : Json{{"columns", 1}, {"rows", 1}}},
                   {"pivot", Json::array({0.5, 0.0})},
                   {"pixels_per_metre", 64},
                   {"filter", "nearest"},
                   {"animations", Json::object()}};
        if (auto w = write(target, sheet); !w) return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(target);
    }
    if (op == "save") {  // new settings: one that doesn't parse isn't written (one that doesn't slice is, with its "error")
        auto file = file_of(command, "asset");
        if (!file) return std::unexpected(file.error());
        if (!command.contains("sheet") || !command["sheet"].is_object()) return fail(ErrorCode::InvalidArgument, "sprite save: \"sheet\" (an object) is needed");
        Json sheet = command["sheet"];
        sheet["noor_sprite"] = asset::kSpriteFormat;
        if (auto parsed = asset::parse_sprite_sheet(sheet); !parsed) return std::unexpected(parsed.error());
        if (auto w = write(*file, sheet); !w) return std::unexpected(w.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        return describe(*file);
    }
    if (op == "apply") {  // an entity's sprite shows this sheet (its other fields kept), or a new entity gets one
        if (auto s = require_editable(); !s) return std::unexpected(s.error());
        auto file = file_of(command, "asset");
        if (!file) return std::unexpected(file.error());
        if (auto s = rescan(); !s) return std::unexpected(s.error());
        auto d = describe(*file);
        if (!d) return std::unexpected(d.error());
        Json component{{"sheet", (*d)["guid"]}};
        if (command.contains("animation") && command["animation"].is_string()) component["animation"] = command["animation"];
        Json result = *d;
        if (command.contains("entity") && command["entity"].is_string()) {
            const std::string id = command["entity"].get<std::string>();
            const auto parsed = Guid::parse(id);  // a Result
            const scene::Entity* e = parsed && scene_ ? scene_->find(*parsed) : nullptr;
            if (!e) return fail(ErrorCode::NotFound, std::format("no entity {}", id));
            if (e->has("sprite") && e->components.at("sprite").is_object()) {
                Json merged = e->components.at("sprite");
                merged.update(component);
                component = std::move(merged);
            }
            if (auto s = execute({{"op", "set_component"}, {"entity", id}, {"type", "sprite"}, {"value", component}}); !s) return std::unexpected(s.error());
            result["entity"] = id;
        } else {
            const Guid id = Guid::generate();
            Json create{{"op", "create"}, {"id", id.to_string()}, {"name", (*d)["name"]}, {"components", {{"sprite", component}}}};
            if (command.contains("position") && command["position"].is_array()) create["transform"] = Json{{"position", command["position"]}};
            if (auto s = execute(create); !s) return std::unexpected(s.error());
            result["entity"] = id.to_string();
        }
        return result;
    }
    return fail(ErrorCode::InvalidArgument, std::format("sprite: unknown op \"{}\"", op));
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

Result<Json> Document::asset_details(const Guid& id) {
    const auto* a = registry_.find(id);
    if (!a) return fail(ErrorCode::NotFound, std::format("no asset {}", id.to_string()));
    auto full = project_.resolve(a->path);
    if (!full) return std::unexpected(full.error());
    const stdfs::path manifest = asset::manifest_path_for_model(*full);
    const bool model = a->importer == "gltf" || a->importer == "fbx";
    const bool interchange = model && a->from_interchange && stdfs::exists(manifest);

    // Stale when any file it read changed (size or time); the level files are listed in the manifest.
    auto stamp = [](const stdfs::path& p) {
        std::error_code ec;
        const auto size = stdfs::file_size(p, ec);
        const auto time = stdfs::last_write_time(p, ec);
        return ec ? std::string("-") : std::format("{}@{}", size, time.time_since_epoch().count());
    };
    std::string key = a->path + "|" + stamp(*full);
    std::optional<asset::ManifestRead> read;
    std::string read_error;
    if (interchange) {
        key += "|" + stamp(manifest);
        auto r = asset::read_manifest(manifest);
        if (r) {
            for (const auto& f : r->manifest.files) key += "|" + stamp(manifest.parent_path() / stdfs::path(f.path));
            read = std::move(*r);
        } else {
            read_error = r.error().message;
        }
    }
    if (const auto it = details_.find(id); it != details_.end() && it->second.key == key) return it->second.json;

    std::error_code ec;
    Json j{{"guid", id.to_string()}, {"path", a->path}, {"importer", a->importer}, {"name", stdfs::path(a->path).stem().string()},
           {"bytes", stdfs::file_size(*full, ec)}, {"interchange", nullptr}};
    if (interchange) {
        if (read) {
            Json m = asset::to_json(*read);
            const auto levels = asset::check_levels(*read, manifest.parent_path());
            m["levels"] = asset::to_json(levels);
            m["manifest"] = stdfs::path(manifest).filename().generic_string();
            std::uint64_t total = 0;
            for (const auto& l : levels) total += l.bytes;
            m["level_bytes"] = total;
            j["interchange"] = std::move(m);
        } else {
            j["interchange"] = {{"manifest", manifest.filename().generic_string()},
                                {"issues", Json::array({{{"field", ""}, {"message", read_error}, {"error", true}}})}};
        }
    } else if (model) {
        if (auto m = asset::import_model(*full)) {
            j["model"] = {{"triangles", m->render_triangles()}, {"materials", m->materials.size()}, {"images", m->images.size()},
                          {"nodes", m->nodes.size()}};
        }
    }
    details_[id] = {key, j};
    return j;
}

Result<Json> Document::scatter_edit(const Json& command) {
    if (auto s = require_editable(); !s) return std::unexpected(s.error());
    const std::string phase = command.value("phase", std::string("dab"));
    if (phase == "begin") {
        finish_stroke();
        if (scatter_stroke_) history_.end_transaction();
        scatter_stroke_.reset();
        auto id = entity_field(command, "scatter");
        if (!id) return std::unexpected(id.error());
        const scene::Entity* target = scene_->find(*id);
        if (!target || !target->has("scatter") || !target->components["scatter"].is_object())
            return fail(ErrorCode::InvalidArgument, "planting and removing need an editable entity with a scatter component");
        ScatterStroke s;
        s.entity = *id;
        const std::string tool = command.value("tool", std::string("plant"));
        if (tool != "plant" && tool != "remove") return fail(ErrorCode::InvalidArgument, "tool is plant or remove");
        s.plant = tool == "plant";
        s.radius = std::clamp(command.contains("radius") && command["radius"].is_number() ? command["radius"].get<float>() : s.radius, 0.1f, 500.0f);
        s.asset = asset::guid_field(command, "asset");
        if (s.plant && !s.asset) {
            // The set's first model: what it's made of.
            const auto items = game::scatter_items(target->components["scatter"], project_.root());
            if (!items.empty()) s.asset = items.front().asset;
            if (!s.asset) return fail(ErrorCode::InvalidArgument, "choose the model to plant (\"asset\"): this scatter has no instances to take one from");
        }
        scatter_stroke_ = s;
        history_.begin_transaction(s.plant ? "Plant trees" : "Remove trees");
        return Json{{"ok", true}};
    }
    if (!scatter_stroke_) return fail(ErrorCode::InvalidArgument, "no planting stroke: send \"begin\" first");
    ScatterStroke& s = *scatter_stroke_;
    const auto index = std::ranges::find(scene_->entities, s.entity, &scene::Entity::id) - scene_->entities.begin();
    if (index >= static_cast<std::ptrdiff_t>(scene_->entities.size())) {
        history_.end_transaction();
        scatter_stroke_.reset();
        return fail(ErrorCode::NotFound, "the scatter entity is gone");
    }
    const math::Mat4 world = scene_->world_matrix(static_cast<std::size_t>(index));
    const math::Mat4 to_local = math::inverse(world);
    for (const auto& dab : command.value("dabs", Json::array())) {
        float p[3];
        if (!dab.is_object() || !dab.contains("point") || !read_vec(dab["point"], 3, p)) continue;
        const math::Vec3 point{p[0], p[1], p[2]};
        Json comp = scene_->entities[static_cast<std::size_t>(index)].components["scatter"];
        bool edited = false;
        if (s.plant) {
            const auto flat = [](math::Vec3 a, math::Vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); };
            if (s.last && flat(*s.last, point) < std::max(1.0f, s.radius)) continue;
            s.last = point;
            const math::Vec3 local = to_local.transform_point(point);
            procgraph::Random rng(game::new_instance_id());
            const std::uint64_t id = game::new_instance_id();
            Json added = comp.contains("added") && comp["added"].is_array() ? comp["added"] : Json::array();
            added.push_back({{"id", asset::instance_id_text(id)}, {"asset", s.asset->to_string()}, {"position", {local.x, local.y, local.z}},
                             {"yaw", std::round(rng.uniform() * 3600.0) / 10.0}, {"scale", std::round((0.9 + rng.uniform() * 0.2) * 1000.0) / 1000.0}});
            comp["added"] = std::move(added);
            ++s.planted;
            edited = true;
        } else {
            Json removed = comp.contains("removed") && comp["removed"].is_array() ? comp["removed"] : Json::array();
            Json added = comp.contains("added") && comp["added"].is_array() ? comp["added"] : Json::array();
            for (const auto& it : game::scatter_items(comp, project_.root())) {
                const math::Vec3 w = world.transform_point(it.position);
                if (std::hypot(w.x - point.x, w.z - point.z) > s.radius) continue;
                const std::string id = asset::instance_id_text(it.id);
                if (it.added) {
                    for (std::size_t k = 0; k < added.size(); ++k)
                        if (added[k].is_object() && added[k].value("id", std::string()) == id) {
                            added.erase(k);
                            break;
                        }
                } else {
                    removed.push_back(id);
                }
                ++s.removed;
                edited = true;
            }
            comp["removed"] = std::move(removed);
            comp["added"] = std::move(added);
        }
        if (edited)
            if (auto e = execute({{"op", "set_component"}, {"entity", s.entity.to_string()}, {"type", "scatter"}, {"value", comp}}); !e)
                return std::unexpected(e.error());
    }
    Json result{{"planted", s.planted}, {"removed", s.removed}};
    if (phase == "end") {
        history_.end_transaction();
        scatter_stroke_.reset();
        changed();
    }
    return result;
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
    anim_preview_.reset();   // the Animation panel's preview was of the old scene's character
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
    save_terrain_layers();
    cook_graphs(scene::expand_prefabs(*scene_, prefabs_.resolver()).scene, true);  // a picture a graph reads may have changed
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
        auto model = asset::import_model(*source);
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
    publish(expanded_for_view(), authored);
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
    const auto worlds = expanded.world_matrices();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        const math::Mat4& world = worlds[i];
        snap->entities.push_back({e.id, e.parent, world, world_rotation(i), e.transform, !world_ && authored.contains(e.id)});
        if (e.has("marker")) snap->markers.push_back({world, e.id, std::nullopt});
    }
    // Terrains built on graphs: their cooked height fields first (only when a graph changed).
    if (!world_) cook_graphs(expanded, false);
    // Models, shapes and the game camera: the same view noor_player draws.
    auto view = game::collect_view(expanded, game::registry_asset_path(project_, registry_), world_ ? &world_->render_poses() : nullptr, project_.root());
    snap->primitives = std::move(view.primitives);
    snap->sprites = std::move(view.sprites);
    snap->water = std::move(view.water);
    snap->terrains = std::move(view.terrains);
    // Sculpted or painted terrains: the live layers (saved or not).
    for (auto& t : snap->terrains) {
        if (!t.sculpt && !t.paint && !terrain_edits_.contains(t.entity)) continue;
        if (auto edit = terrain_edit(t.entity, t.sculpt, t.paint, t.settings)) {
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
        snap->game_time = world_->time();  // sprites played by scripts (and the clouds) follow the game's clock
    }
    // Effect particles (8B part 4): every emitter, its settings resolved now (an edited .noorfx shows at once).
    const auto asset_path = game::registry_asset_path(project_, registry_);
    for (std::size_t i = 0; i < entities.size(); ++i)
        if (entities[i].has("emitter") && entities[i].components.at("emitter").is_object())
            snap->emitters.push_back({entities[i].id, fx::resolve_emitter(entities[i].components.at("emitter"), asset_path), worlds[i]});
    snap->wind = view.lighting.weather.wind;
    snap->preview_solo = preview_solo_;
    snap->preview_muted = preview_muted_;
    snap->preview_restart = preview_restart_;
    snap->preview_paused = preview_paused_;
    if (!world_) snap->bones = anim_bones_;
    // CSG models: built from their brush children in order (ADR-016), cached per model.
    csg_last_.clear();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i].has("csg_model")) continue;
        auto mesh = build_csg(expanded, i);
        if (!mesh) continue;
        snap->csg.push_back({entities[i].id, worlds[i], mesh});
        csg_last_.emplace_back(entities[i].id, std::move(mesh));
    }
    for (auto& m : view.meshes) {
        std::string key = m.asset_path.generic_string();
        const auto version = asset_versions_.contains(key) ? asset_versions_.at(key) : 0;
        snap->meshes.push_back({std::move(key), m.mesh_index, m.world, m.entity, version, m.model_root, m.skin, std::move(m.joints)});
    }
    for (auto& md : view.models) {
        std::string key = md.asset_path.generic_string();
        const auto version = asset_versions_.contains(key) ? asset_versions_.at(key) : 0;
        snap->models.push_back({md.entity, std::move(key), md.world, version});
    }
    for (auto& sv : view.scatters)
        for (auto& g : sv.groups) {
            std::string key = g.asset_path.generic_string();
            const auto version = asset_versions_.contains(key) ? asset_versions_.at(key) : 0;
            snap->scattered.push_back({sv.entity, std::move(key), version, g.worlds, sv.draw_distance, sv.cast_shadows});
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

// ---- Terrain sculpting and painting (Milestone 6, parts 6-7) ----

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

// One stroke as an undo step: a layer's values in a rectangle before and after.
template <class T>
class LayerCommand final : public scene::Command {
public:
    LayerCommand(std::shared_ptr<TerrainEdit> edit, terrain::SampleRect rect, std::vector<T> before, std::vector<T> after)
        : edit_(std::move(edit)), rect_(rect), before_(std::move(before)), after_(std::move(after)) {}
    [[nodiscard]] std::string label() const override { return std::is_same_v<T, float> ? "Sculpt" : "Paint"; }
    bool apply(scene::Scene&) override {
        edit_->write(rect_, after_);
        return true;
    }
    void revert(scene::Scene&) override { edit_->write(rect_, before_); }

private:
    std::shared_ptr<TerrainEdit> edit_;
    terrain::SampleRect rect_;
    std::vector<T> before_, after_;
};

std::string safe_file_name(std::string name) {
    for (char& ch : name)
        if (std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos || static_cast<unsigned char>(ch) < 32) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? std::string("Terrain") : name;
}

std::string capitalised(std::string_view s) {
    std::string out(s);
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

} // namespace

void TerrainEdit::touched(const terrain::SampleRect& rect) {
    current->recompose(*base, sculpt_active ? layer : terrain::SculptLayer(layer.tiles_x(), layer.tiles_z()), rect);
    if (paint_active) current->repaint(paint, rect);
    ++revision;
    for_tiles(rect, current->tiles_x(), current->tiles_z(), [&](std::uint32_t tx, std::uint32_t tz) {
        tile_versions[static_cast<std::size_t>(tz) * current->tiles_x() + tx] = revision;
    });
}

void TerrainEdit::rebuild() {
    current = std::make_shared<terrain::Heights>(sculpt_active ? terrain::Heights::compose(*base, layer) : *base);
    if (paint_active) current->repaint(paint);
    ++revision;
    tile_versions.assign(static_cast<std::size_t>(current->tiles_x()) * current->tiles_z(), revision);
}

void TerrainEdit::write(const terrain::SampleRect& rect, const std::vector<float>& offsets) {
    std::size_t i = 0;
    for (std::uint32_t z = rect.z0; z <= rect.z1; ++z)
        for (std::uint32_t x = rect.x0; x <= rect.x1; ++x) layer.set(x, z, offsets[i++]);
    touched(rect);
    sculpt_dirty = true;
}

void TerrainEdit::write(const terrain::SampleRect& rect, const std::vector<std::uint32_t>& weights) {
    std::size_t i = 0;
    for (std::uint32_t z = rect.z0; z <= rect.z1; ++z)
        for (std::uint32_t x = rect.x0; x <= rect.x1; ++x) paint.set(x, z, weights[i++]);
    touched(rect);
    paint_dirty = true;
}

std::shared_ptr<const terrain::Heights> TerrainEdit::snapshot() {
    if (published_revision != revision || !published) {
        published = std::make_shared<const terrain::Heights>(*current);
        published_revision = revision;
    }
    return published;
}

std::shared_ptr<TerrainEdit> Document::terrain_edit(const Guid& entity, const std::optional<Guid>& sculpt, const std::optional<Guid>& paint,
                                                     const terrain::Settings& settings) {
    auto& slot = terrain_edits_[entity];
    if (!slot) {
        slot = std::make_shared<TerrainEdit>();
        slot->entity = entity;
        slot->layer = terrain::SculptLayer(settings.tiles_x, settings.tiles_z);
        slot->paint = terrain::PaintLayer(settings.tiles_x, settings.tiles_z);
    }
    TerrainEdit& e = *slot;
    bool changed = false;
    auto file_of = [&](const Guid& id) -> std::filesystem::path {
        if (const auto* rec = registry_.find(id))
            if (auto file = project_.resolve(rec->path)) return *file;
        return {};
    };
    // A layer asset the edit hasn't seen: load it (a missing or damaged file starts empty, with a message).
    if (sculpt && sculpt != e.sculpt_asset) {
        e.sculpt_asset = sculpt;
        e.sculpt_file = file_of(*sculpt);
        e.sculpt_dirty = false;
        auto layer = e.sculpt_file.empty() ? Result<terrain::SculptLayer>(fail(ErrorCode::NotFound, "its sculpt layer isn't in the project"))
                                           : terrain::SculptLayer::load(e.sculpt_file);
        if (layer) e.layer = std::move(*layer);
        else {
            NOOR_LOG_ERROR("Editor", "Terrain: {} (starting from an empty layer)", layer.error().describe());
            e.layer = terrain::SculptLayer(settings.tiles_x, settings.tiles_z);
        }
        changed = true;
    }
    if (paint && paint != e.paint_asset) {
        e.paint_asset = paint;
        e.paint_file = file_of(*paint);
        e.paint_dirty = false;
        auto layer = e.paint_file.empty() ? Result<terrain::PaintLayer>(fail(ErrorCode::NotFound, "its paint layer isn't in the project"))
                                          : terrain::PaintLayer::load(e.paint_file);
        if (layer) e.paint = std::move(*layer);
        else {
            NOOR_LOG_ERROR("Editor", "Terrain: {} (starting from an empty layer)", layer.error().describe());
            e.paint = terrain::PaintLayer(settings.tiles_x, settings.tiles_z);
        }
        changed = true;
    }
    const bool sculpt_active = sculpt.has_value() && sculpt == e.sculpt_asset, paint_active = paint.has_value() && paint == e.paint_asset;
    changed = changed || sculpt_active != e.sculpt_active || paint_active != e.paint_active;
    e.sculpt_active = sculpt_active;
    e.paint_active = paint_active;
    e.layer.resize(settings.tiles_x, settings.tiles_z);
    e.paint.resize(settings.tiles_x, settings.tiles_z);
    if (auto key = settings.base_only().key(); key != e.base_key || !e.current) {
        // A new base (another seed, size or heightmap): the same layers on top of it.
        auto base = terrain::Heights::shared(settings.base_only());
        if (!base) {
            NOOR_LOG_WARN("Editor", "Terrain: {}", base.error().describe());
            return nullptr;
        }
        e.base = *base;
        e.base_key = std::move(key);
        changed = true;
    }
    if (changed) e.rebuild();
    return slot;
}

void Document::save_terrain_layers() {
    for (auto& [id, edit] : terrain_edits_) {
        if (edit->sculpt_dirty && !edit->sculpt_file.empty()) {
            if (auto s = edit->layer.save(edit->sculpt_file); s) edit->sculpt_dirty = false;
            else NOOR_LOG_ERROR("Editor", "Terrain: can't save {}: {}", edit->sculpt_file.filename().string(), s.error().describe());
        }
        if (edit->paint_dirty && !edit->paint_file.empty()) {
            if (auto s = edit->paint.save(edit->paint_file); s) edit->paint_dirty = false;
            else NOOR_LOG_ERROR("Editor", "Terrain: can't save {}: {}", edit->paint_file.filename().string(), s.error().describe());
        }
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
            s.last = local;
            s.first_dab = false;
        }
        // A pointer that moved far since the last dab leaves a line, not a trail of dots: dabs every
        // quarter of a radius along the way, sharing this dab's time (the amount per second stays the same).
        const math::Vec2 step{local.x - s.last.x, local.y - s.last.y};
        const float distance = std::sqrt(step.x * step.x + step.y * step.y);
        const int pieces = std::clamp(static_cast<int>(std::ceil(distance / std::max(0.05f, s.brush.radius * 0.25f))), 1, 256);
        for (int k = 1; k <= pieces; ++k) {
            const float f = static_cast<float>(k) / static_cast<float>(pieces);
            const math::Vec2 at{s.last.x + step.x * f, s.last.y + step.y * f};
            const float dt = seconds / static_cast<float>(pieces);
            const auto rect = s.painting ? terrain_edit::paint(*s.edit->current, s.edit->paint, s.brush, s.layer, at, dt)
                                         : terrain_edit::apply(*s.edit->current, s.edit->layer, s.brush, at, dt);
            if (!rect) continue;
            s.edit->touched(*rect);
            (s.painting ? s.edit->paint_dirty : s.edit->sculpt_dirty) = true;
            if (s.changed) s.changed->include(*rect);
            else s.changed = *rect;
        }
        s.last = local;
    }
}

void Document::finish_stroke() {
    if (!stroke_) return;
    Stroke s = std::move(*stroke_);
    stroke_.reset();
    if (s.changed && scene_) {
        const auto& r = *s.changed;
        auto collect = [&](const auto& before_layer, const auto& after_layer, auto& before, auto& after) {
            before.reserve(r.count());
            after.reserve(r.count());
            for (std::uint32_t z = r.z0; z <= r.z1; ++z)
                for (std::uint32_t x = r.x0; x <= r.x1; ++x) {
                    before.push_back(before_layer.get(x, z));
                    after.push_back(after_layer.get(x, z));
                }
        };
        if (s.painting) {
            std::vector<std::uint32_t> before, after;
            collect(s.before_paint, s.edit->paint, before, after);
            history_.execute(*scene_, std::make_unique<LayerCommand<std::uint32_t>>(s.edit, r, std::move(before), std::move(after)));
        } else {
            std::vector<float> before, after;
            collect(s.before, s.edit->layer, before, after);
            history_.execute(*scene_, std::make_unique<LayerCommand<float>>(s.edit, r, std::move(before), std::move(after)));
        }
    }
    history_.end_transaction();  // opened when the stroke began
    changed();
}

Result<Guid> Document::new_layer_file(const std::string& name, std::string_view extension, const terrain::Settings& settings, bool paint) {
    const auto dir = project_.assets_dir() / "Terrain";
    std::error_code ec;
    stdfs::create_directories(dir, ec);
    const std::string stem = safe_file_name(name);
    stdfs::path file = dir / (stem + std::string(extension));
    for (int n = 2; stdfs::exists(file); ++n) file = dir / std::format("{} {}{}", stem, n, extension);
    const Status written = paint ? terrain::PaintLayer(settings.tiles_x, settings.tiles_z).save(file)
                                 : terrain::SculptLayer(settings.tiles_x, settings.tiles_z).save(file);
    if (!written) return std::unexpected(written.error());
    if (auto r = registry_.scan(project_); !r) return std::unexpected(r.error());
    const auto rel = project_.relative(file);
    const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
    if (!rec) return fail(ErrorCode::Unknown, "the new terrain layer wasn't registered as an asset");
    return rec->guid;
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
            return fail(ErrorCode::InvalidArgument, "sculpting and painting need an editable entity with a terrain component");
        const std::string tool_name = command.value("tool", std::string("raise"));
        const bool painting = tool_name == "paint";
        terrain_edit::Brush brush;
        if (!painting) {
            if (auto tool = terrain_edit::tool_from_name(tool_name)) brush.tool = *tool;
            else return fail(ErrorCode::InvalidArgument, "tool is raise, lower, smooth, flatten or paint");
        }
        brush.radius = std::clamp(json_float(command, "radius", brush.radius), 0.1f, 500.0f);
        brush.strength = std::clamp(json_float(command, "strength", brush.strength), 0.0f, 1000.0f);
        brush.falloff = std::clamp(json_float(command, "falloff", brush.falloff), 0.0f, 1.0f);

        Json component = target->components["terrain"];
        const Json original = component;
        auto settings = game::terrain_settings(component, game::file_resolver(game::registry_asset_path(project_, registry_)), project_.root());
        std::uint32_t layer_index = 0;
        std::string label;
        if (painting) {
            // The layer that holds the material, or a new one (up to four per terrain).
            const auto material = asset::guid_field(command, "material");
            if (!material) return fail(ErrorCode::InvalidArgument, "paint needs a \"material\" (a .noormat asset id)");
            Json layers = component.contains("layers") && component["layers"].is_array() ? component["layers"] : Json::array();
            if (layers.empty()) layers.push_back({{"material", component.contains("material") ? component["material"] : Json(nullptr)}, {"tiling", 1}});
            std::optional<std::uint32_t> found;
            for (std::size_t i = 0; i < std::min<std::size_t>(4, layers.size()); ++i) {
                const auto m = layers[i].is_object() ? asset::guid_field(layers[i], "material") : std::nullopt;
                if (m == material) found = static_cast<std::uint32_t>(i);
            }
            if (!found) {
                if (layers.size() >= terrain::PaintLayer::kLayers)
                    return fail(ErrorCode::InvalidArgument, "this terrain already has four materials: paint with one of them, or change a layer's material");
                layers.push_back({{"material", material->to_string()}, {"tiling", 1}});
                found = static_cast<std::uint32_t>(layers.size() - 1);
            }
            layer_index = *found;
            component["layers"] = layers;
            const auto* rec = registry_.find(*material);
            label = "Paint: " + (rec ? stdfs::path(rec->path).stem().string() : std::string("material"));
        } else {
            label = "Sculpt: " + capitalised(terrain_edit::tool_name(brush.tool));
        }
        history_.begin_transaction(label);  // ended by finish_stroke
        // The terrain's first stroke of its kind: a layer file of its own.
        const char* field = painting ? "paint" : "sculpt";
        if (const auto existing = asset::guid_field(component, field); !existing || !registry_.find(*existing)) {
            auto made = new_layer_file(target->name, painting ? terrain::PaintLayer::kExtension : terrain::SculptLayer::kExtension, settings, painting);
            if (!made) {
                history_.end_transaction();
                return std::unexpected(made.error());
            }
            component[field] = made->to_string();
        }
        if (component != original) history_.execute(*scene_, scene::set_component(*id, "terrain", component));
        settings = game::terrain_settings(component, game::file_resolver(game::registry_asset_path(project_, registry_)), project_.root());
        auto edit = terrain_edit(*id, asset::guid_field(component, "sculpt"), asset::guid_field(component, "paint"), settings);
        if (!edit) {
            history_.end_transaction();
            return fail(ErrorCode::Unknown, "the terrain's heights couldn't be built (see the log)");
        }
        // Where the terrain is: its entity's world position (it may have parents).
        const auto expanded = scene::expand_prefabs(*scene_, prefabs_.resolver());
        math::Vec3 position = target->transform.position;
        for (std::size_t i = 0; i < expanded.scene.entities.size(); ++i)
            if (expanded.scene.entities[i].id == *id) position = expanded.scene.world_matrix(i).transform_point({0, 0, 0});
        Stroke stroke{*id, edit, brush, painting, layer_index, position, {}, {}, std::nullopt, true};
        if (painting) stroke.before_paint = edit->paint;
        else stroke.before = edit->layer;
        stroke_ = std::move(stroke);
    }
    if (!stroke_) return fail(ErrorCode::InvalidArgument, "no sculpt or paint stroke has begun");
    apply_dabs(command.value("dabs", Json::array()));
    Json result{{"changed", stroke_->changed.has_value()}};
    if (stroke_->painting) {
        result["paint"] = stroke_->edit->paint_asset ? stroke_->edit->paint_asset->to_string() : "";
        result["layer"] = stroke_->layer;
    } else {
        result["sculpt"] = stroke_->edit->sculpt_asset ? stroke_->edit->sculpt_asset->to_string() : "";
    }
    if (phase == "end") finish_stroke();
    else changed();
    return result;
}

Result<float> Document::terrain_height(const Guid& terrain, float x, float z) {
    if (auto s = require_scene(); !s) return std::unexpected(s.error());
    const auto expanded = scene::expand_prefabs(*scene_, prefabs_.resolver());
    for (std::size_t i = 0; i < expanded.scene.entities.size(); ++i) {
        const auto& e = expanded.scene.entities[i];
        if (e.id != terrain) continue;
        if (!e.has("terrain") || !e.components["terrain"].is_object()) break;
        const Json& component = e.components["terrain"];
        const auto settings = game::terrain_settings(component, game::file_resolver(game::registry_asset_path(project_, registry_)), project_.root());
        const math::Vec3 centre = expanded.scene.world_matrix(i).transform_point({0, 0, 0});
        std::shared_ptr<const terrain::Heights> heights;
        if (auto edit = terrain_edit(e.id, asset::guid_field(component, "sculpt"), asset::guid_field(component, "paint"), settings)) heights = edit->snapshot();
        if (!heights) {
            auto shared = terrain::Heights::shared(settings);
            if (!shared) return std::unexpected(shared.error());
            heights = *shared;
        }
        return centre.y + heights->height(x - centre.x, z - centre.z);
    }
    return fail(ErrorCode::NotFound, "no terrain entity with that id");
}

// ---- Procedural graphs (Milestone 7, part 2) ----

Document::~Document() {
    if (job_ && job_->thread.joinable()) {
        job_->cancel = true;  // closing the document stops a running graph; nothing half-written is kept
        job_->thread.join();
    }
}

Status Document::generate_start(std::string_view graph_path, const std::filesystem::path& out_dir) {
    if (job_) {
        {
            std::scoped_lock lock(job_->mutex);
            if (job_->status.value("running", false)) return fail(ErrorCode::InvalidArgument, "a graph is already running: wait, or cancel it");
        }
        if (job_->thread.joinable()) job_->thread.join();
    }
    auto file = project_.resolve(graph_path);
    if (!file) return std::unexpected(file.error());
    auto graph = procgraph::Graph::load(*file);
    if (!graph) return std::unexpected(graph.error());
    if (!graph_cache_) graph_cache_ = std::make_unique<procgraph::Cache>(project_.root() / ".noor" / "cache" / "procgraph");
    job_ = std::make_unique<GenerateJob>();
    job_->status = Json{{"running", true}, {"graph", std::string(graph_path)}, {"done", 0}, {"total", graph->nodes.size()}, {"node", ""}};
    procgraph::GenerateOptions opts;
    opts.out = out_dir.empty() ? project_.root() / "Generated" / file->stem() : out_dir;
    opts.name = file->stem().string();
    opts.run.cache = graph_cache_.get();
    opts.run.cancel = &job_->cancel;
    GenerateJob* job = job_.get();
    opts.run.progress = [job](const procgraph::Progress& p) {
        std::scoped_lock lock(job->mutex);
        job->status["done"] = p.done;
        job->status["total"] = p.total;
        job->status["node"] = p.node;
    };
    // Asset files as they are now: the run is on another thread and a Rescan may change the registry meanwhile.
    opts.run.resolve = [files = asset_files()](const Guid& id) -> std::optional<stdfs::path> {
        const auto it = files->find(id);
        return it == files->end() ? std::nullopt : std::optional(it->second);
    };
    const auto root = project_.root();
    job_->thread = std::thread([job, opts, g = std::move(*graph), root]() {
        // Evaluate first (kept for per-node results and previews), then write files when every declared
        // output was made: the second pass is all cache hits.
        auto ev = std::make_shared<const procgraph::Evaluation>(procgraph::evaluate(g, procgraph::Registry::builtin(), opts.run));
        Json done{{"running", false}, {"cancelled", ev->cancelled}, {"ran", ev->ran}, {"cached", ev->cached},
                  {"tiles_run", ev->tiles_run}, {"tiles_cached", ev->tiles_cached}, {"tiles_passed", ev->tiles_passed}, {"written", false}};
        bool complete = !g.outputs.empty() && !ev->cancelled;
        for (const auto& o : g.outputs) complete = complete && ev->outputs.contains(o.name);
        if (ev->cancelled) {
            done["ok"] = false;
            done["error"] = "cancelled: nothing was written";
        } else if (complete) {
            auto r = procgraph::generate(g, procgraph::Registry::builtin(), opts);
            done["ok"] = r.has_value();
            if (r) {
                done["written"] = true;
                done["fingerprint"] = std::format("{:016x}", r->fingerprint);
                Json files = Json::array();
                for (const auto& f : r->files) {
                    std::error_code ec;
                    const auto rel = stdfs::relative(f, root, ec);
                    files.push_back(!ec && !rel.empty() && *rel.begin() != ".." ? rel.generic_string() : f.generic_string());
                }
                done["files"] = std::move(files);
            } else {
                done["error"] = r.error().message;
                done["cancelled"] = r.error().code == ErrorCode::Cancelled;
            }
        } else {
            done["ok"] = g.outputs.empty();  // a graph without outputs only shows its nodes
            done["files"] = Json::array();
            if (!g.outputs.empty()) done["error"] = "not every output could be made (see the issues): nothing was written";
        }
        Json list = Json::array();
        for (const auto& i : ev->issues) list.push_back({{"node", i.node}, {"pin", i.pin}, {"message", i.message}});
        done["issues"] = std::move(list);
        // Every node: how it went, and a summary of each output (previews: noor_doc_generate_preview).
        Json nodes = Json::object();
        for (const auto& [id, r] : ev->nodes) {
            Json n{{"state", r.state == procgraph::NodeResult::State::Done ? "done" : r.state == procgraph::NodeResult::State::Failed ? "failed" : "skipped"},
                   {"message", r.message}, {"ms", r.ms}, {"cached", r.cached}, {"tiles_run", r.tiles_run}, {"tiles_cached", r.tiles_cached},
                   {"tiles_passed", r.tiles_passed}};
            Json outs = Json::object();
            for (const auto& [pin, v] : r.outputs) {
                Json o{{"type", std::string(procgraph::type_name(v.type))}, {"unit", std::string(procgraph::unit_name(v.unit))}};
                if (v.type == procgraph::PinType::Scalar) o["value"] = v.scalar;
                if (v.field && !v.field->v.empty()) {
                    const auto [lo, hi] = std::ranges::minmax(v.field->v);
                    o["min"] = lo;
                    o["max"] = hi;
                    o["samples"] = {v.field->sx, v.field->sz};
                }
                if (v.instances) o["count"] = v.instances->size();  // instance sets (Milestone 7, part 7)
                outs[pin] = std::move(o);
            }
            n["outputs"] = std::move(outs);
            nodes[id] = std::move(n);
        }
        done["nodes"] = std::move(nodes);
        std::scoped_lock lock(job->mutex);
        done["graph"] = job->status.value("graph", std::string());
        done["done"] = job->status.value("done", 0);
        done["total"] = job->status.value("total", 0);
        job->status = std::move(done);
        job->evaluation = std::move(ev);
    });
    return {};
}

Json Document::generate_status() {
    if (!job_) return Json{{"running", false}, {"graph", nullptr}};
    std::scoped_lock lock(job_->mutex);
    return job_->status;
}

void Document::generate_cancel() {
    if (job_) job_->cancel = true;
}

namespace {
std::string base64(const std::vector<std::uint8_t>& in) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const std::uint32_t n = (static_cast<std::uint32_t>(in[i]) << 16) | (i + 1 < in.size() ? static_cast<std::uint32_t>(in[i + 1]) << 8 : 0u) |
                                (i + 2 < in.size() ? static_cast<std::uint32_t>(in[i + 2]) : 0u);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(i + 1 < in.size() ? kTable[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < in.size() ? kTable[n & 63] : '=');
    }
    return out;
}
} // namespace

Result<Json> Document::generate_preview(std::string_view node, std::string_view pin, int size) {
    std::shared_ptr<const procgraph::Evaluation> ev;
    if (job_) {
        std::scoped_lock lock(job_->mutex);
        ev = job_->evaluation;
    }
    if (!ev) return fail(ErrorCode::NotFound, "no graph has run yet");
    const auto it = ev->nodes.find(std::string(node));
    if (it == ev->nodes.end()) return fail(ErrorCode::NotFound, std::format("the last run has no node \"{}\"", node));
    const auto out = it->second.outputs.find(std::string(pin));
    if (out == it->second.outputs.end()) return fail(ErrorCode::NotFound, std::format("node \"{}\" has no result for \"{}\" (it didn't run, or no such output)", node, pin));
    const auto& v = out->second;
    Json j{{"type", std::string(procgraph::type_name(v.type))}, {"unit", std::string(procgraph::unit_name(v.unit))}};
    if (v.instances) {
        // An instance set (Milestone 7, part 7): a dot per instance, seen from above, over the square that
        // holds them all (rows from -z to +z like fields).
        size = std::clamp(size, 8, 1024);
        float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
        for (const auto& i : *v.instances) {
            x0 = std::min(x0, i.position.x), x1 = std::max(x1, i.position.x);
            z0 = std::min(z0, i.position.z), z1 = std::max(z1, i.position.z);
        }
        const float span = std::max({x1 - x0, z1 - z0, 1.0f});
        std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size, 24);
        const int r = size >= 128 ? 1 : 0;  // dots of 3 x 3 pixels on bigger previews
        for (const auto& i : *v.instances) {
            const int cx = static_cast<int>((i.position.x - x0) / span * static_cast<float>(size - 1) + 0.5f);
            const int cy = static_cast<int>((i.position.z - z0) / span * static_cast<float>(size - 1) + 0.5f);
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const int x = cx + dx, y = cy + dy;
                    if (x >= 0 && y >= 0 && x < size && y < size) px[static_cast<std::size_t>(y) * size + x] = 235;
                }
        }
        j["width"] = size;
        j["height"] = size;
        j["count"] = v.instances->size();
        j["min"] = 0;
        j["max"] = 1;
        j["pixels"] = base64(px);
        return j;
    }
    if (!v.field) {
        if (v.type == procgraph::PinType::Scalar) j["value"] = v.scalar;
        return j;
    }
    // A greyscale picture, `size` pixels on its longer side (nearest sample), black = min, white = max
    // (masks: always 0..1), rows from -z to +z like the field.
    const auto& f = *v.field;
    size = std::clamp(size, 8, 1024);
    const int w = f.sx >= f.sz ? size : std::max(1, static_cast<int>(static_cast<long long>(size) * f.sx / f.sz));
    const int h = f.sz >= f.sx ? size : std::max(1, static_cast<int>(static_cast<long long>(size) * f.sz / f.sx));
    auto [lo, hi] = std::ranges::minmax(f.v);
    if (v.unit == procgraph::Unit::Ratio) lo = 0.0f, hi = 1.0f;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto sx = static_cast<std::uint32_t>((static_cast<long long>(x) * (f.sx - 1) + (w - 1) / 2) / std::max(1, w - 1));
            const auto sz = static_cast<std::uint32_t>((static_cast<long long>(y) * (f.sz - 1) + (h - 1) / 2) / std::max(1, h - 1));
            const float t = hi > lo ? (f.at(std::min(sx, f.sx - 1), std::min(sz, f.sz - 1)) - lo) / (hi - lo) : 0.0f;
            px[static_cast<std::size_t>(y) * w + x] = static_cast<std::uint8_t>(std::clamp(t, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    j["width"] = w;
    j["height"] = h;
    j["min"] = lo;
    j["max"] = hi;
    j["pixels"] = base64(px);
    return j;
}

void Document::cook_graphs(const scene::Scene& expanded, bool force) {
    // Terrains on a graph base and scatter components (Milestone 7, parts 3 and 7).
    for (const Guid& g : game::graphs_used(expanded)) {
        const std::optional<Guid> graph = g;
        const auto* rec = registry_.find(g);
        if (!rec) continue;  // terrain_settings/Heights::build report a missing graph
        auto file = project_.resolve(rec->path);
        if (!file) continue;
        std::error_code ec;
        const auto time = stdfs::last_write_time(*file, ec);
        const auto out = game::cooked_graph_folder(project_.root(), *graph);
        if (!force && graphs_cooked_.contains(*graph) && graphs_cooked_[*graph] == time && stdfs::exists(out / "graph.fingerprint.json", ec)) continue;
        graphs_cooked_[*graph] = time;
        if (!graph_cache_) graph_cache_ = std::make_unique<procgraph::Cache>(project_.root() / ".noor" / "cache" / "procgraph");
        auto r = procgraph::cook(*file, out, graph_cache_.get(), [this](const Guid& id) -> std::optional<stdfs::path> {
            const auto* a = registry_.find(id);
            auto p = a ? project_.resolve(a->path) : Result<stdfs::path>(fail(ErrorCode::NotFound, "unknown asset"));
            return p ? std::optional(*p) : std::nullopt;
        });
        if (!r) NOOR_LOG_WARN("Editor", "Graph {}: {}", file->filename().string(), r.error().describe());
        else NOOR_LOG_INFO("Editor", "Graph {} cooked ({} node(s) ran, {} from the cache)", file->filename().string(), r->evaluation.ran, r->evaluation.cached);
    }
}

} // namespace noor::editor
