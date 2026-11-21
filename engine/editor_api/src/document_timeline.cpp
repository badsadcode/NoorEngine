// The Timeline panel's side of the document (gameplay foundations, part 5): the project's timelines, an entity's
// animation player, keys set from what the scene shows, and a preview that scrubs a timeline in the viewport without
// touching the scene's undo history (what was there comes back when the preview ends, before any edit or save).
#include "document.h"

#include "noor/asset/timeline_asset.h"
#include "noor/foundation/file_io.h"
#include "noor/game/timeline.h"
#include "noor/runtime/tween.h"
#include "noor/runtime/world.h"

#include <cmath>
#include <format>

namespace noor::editor {
namespace {
using asset::Json;
namespace stdfs = std::filesystem;

// The scene's entity, to change (Scene::find is for reading).
scene::Entity* entity_in(scene::Scene& s, const Guid& id) {
    for (auto& e : s.entities)
        if (e.id == id) return &e;
    return nullptr;
}

Json track_json(const asset::TimelineTrack& t) {
    Json keys = Json::array();
    for (const auto& k : t.keys) {
        Json kj = {{"t", k.t}};
        if (t.kind == asset::TimelineTrack::Kind::Value) {
            kj["v"] = k.value;
            kj["ease"] = k.ease.empty() ? "linear" : k.ease;
        } else if (t.kind == asset::TimelineTrack::Kind::Call) {
            kj["function"] = k.function;
            kj["args"] = k.args;
        } else {
            kj["message"] = k.message;
            kj["data"] = k.data;
        }
        keys.push_back(std::move(kj));
    }
    const char* kind = t.kind == asset::TimelineTrack::Kind::Value ? "value" : t.kind == asset::TimelineTrack::Kind::Call ? "call" : "emit";
    return {{"target", t.target}, {"kind", kind}, {"property", t.property}, {"keys", std::move(keys)}};
}
} // namespace

void Document::end_timeline_preview() {
    if (timeline_preview_.empty() || !scene_) return;
    for (auto& [id, original] : timeline_preview_)
        if (auto* e = entity_in(*scene_, id)) *e = original;
    timeline_preview_.clear();
    republish();
}

Result<Json> Document::timeline(const Json& command) {
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
        return fail(ErrorCode::InvalidArgument, "timeline: the command needs an \"op\"");
    const std::string op = command["op"];

    if (op == "list") {
        Json out = Json::array();
        for (const auto& rec : registry_.assets()) {
            if (rec.importer != "timeline") continue;
            auto t = asset::load_timeline_file(project_.root() / rec.path);
            out.push_back({{"id", rec.guid.to_string()}, {"path", rec.path}, {"name", t ? t->name : std::string()}});
        }
        return Json{{"timelines", std::move(out)}};
    }
    if (op == "preview_end") {
        end_timeline_preview();
        return Json{{"previewing", false}};
    }
    if (!scene_) return fail(ErrorCode::InvalidArgument, "timeline: no scene is open");
    if (!command.contains("entity") || !command["entity"].is_string()) return fail(ErrorCode::InvalidArgument, "timeline: \"entity\" (an id) is needed");
    const auto id = Guid::parse(command["entity"].get<std::string>());
    if (!id || !scene_->find(*id)) return fail(ErrorCode::NotFound, "timeline: no such entity (or not editable here)");

    // The player's timelines: name -> (asset record, timeline).
    const auto player = [&]() -> Json {
        const scene::Entity* e = scene_->find(*id);
        return e && e->has("animation_player") && e->components.at("animation_player").is_object() ? e->components.at("animation_player") : Json();
    };
    const auto resolve = [&](const std::string& name) -> Result<std::pair<stdfs::path, asset::Timeline>> {
        const Json p = player();
        const auto tid = p.is_object() && p.contains("timelines") ? asset::guid_field(p["timelines"], name) : std::nullopt;
        const auto* rec = tid ? registry_.find(*tid) : nullptr;
        if (!rec) return fail(ErrorCode::NotFound, std::format("timeline: the entity's animation player has no timeline '{}'", name));
        const stdfs::path file = project_.root() / rec->path;
        auto t = asset::load_timeline_file(file);
        if (!t) return std::unexpected(t.error());
        return std::pair{file, std::move(*t)};
    };
    const auto save = [&](const stdfs::path& file, const asset::Timeline& t) -> Status {
        if (auto s = fs::write_file_atomic(file, asset::to_text(asset::timeline_file_json(t)), false); !s) return s;
        changed();  // the panel and a playing preview read it again
        return {};
    };

    if (op == "info") {
        Json out{{"timelines", Json::object()}};
        const Json p = player();
        if (p.is_object()) {
            out["autoplay"] = p.value("autoplay", Json());
            const Json list = p.value("timelines", Json::object());  // kept alive for the loop (a temporary's items wouldn't be)
            for (const auto& [name, ref] : list.items()) {
                auto r = resolve(name);
                if (!r) {
                    out["timelines"][name] = {{"error", r.error().message}};
                    continue;
                }
                const auto& [file, t] = *r;
                Json tracks = Json::array();
                for (const auto& track : t.tracks) tracks.push_back(track_json(track));
                out["timelines"][name] = {{"id", ref}, {"path", *project_.relative(file)}, {"name", t.name}, {"length", t.length},
                                          {"loop", t.loop}, {"tracks", std::move(tracks)}};
            }
        }
        return out;
    }

    if (op == "new") {  // a timeline asset, added to the entity's animation player (one undo step for the component)
        end_timeline_preview();
        const std::string name = command.value("name", std::string("default"));
        const scene::Entity* e = scene_->find(*id);
        std::string rel = command.value("asset", std::format("Assets/Timelines/{}_{}.noortimeline", e->name, name));
        for (char& c : rel)
            if (c == ' ') c = '_';
        const auto file = project_.resolve(rel);
        if (!file) return std::unexpected(file.error());
        std::error_code ec;
        if (!stdfs::exists(*file, ec)) {
            asset::Timeline t;
            t.name = name;
            t.length = std::max(0.01, command.value("length", 1.0));
            t.loop = command.value("loop", false);
            stdfs::create_directories(file->parent_path(), ec);
            if (auto s = fs::write_file_atomic(*file, asset::to_text(asset::timeline_file_json(t)), false); !s) return std::unexpected(s.error());
        }
        if (auto s = registry_.scan(project_); !s) return std::unexpected(s.error());
        const auto* rec = registry_.find_path(rel);
        if (!rec) return fail(ErrorCode::IoError, std::format("timeline: {} wasn't registered", rel));
        Json p = player();
        if (!p.is_object()) p = Json::object();
        p["timelines"][name] = rec->guid.to_string();
        if (!p.contains("autoplay")) p["autoplay"] = name;
        if (auto s = execute({{"op", "set_component"}, {"entity", id->to_string()}, {"type", "animation_player"}, {"value", p}}); !s)
            return std::unexpected(s.error());
        return Json{{"id", rec->guid.to_string()}, {"path", rel}};
    }

    const std::string name = command.value("timeline", std::string());
    auto r = resolve(name);
    if (!r) return std::unexpected(r.error());
    auto& [file, t] = *r;

    if (op == "set") {  // {"length"?, "loop"?}
        if (command.contains("length") && command["length"].is_number()) t.length = std::max(0.01, command["length"].get<double>());
        if (command.contains("loop") && command["loop"].is_boolean()) t.loop = command["loop"].get<bool>();
        if (auto s = save(file, t); !s) return std::unexpected(s.error());
        return Json{{"length", t.length}, {"loop", t.loop}};
    }

    if (op == "key") {
        // A value key at "t" on (target, property): "value" given, or what the scene shows now. Same time: replaced.
        end_timeline_preview();
        const double at = std::clamp(command.value("t", 0.0), 0.0, t.length);
        const std::string target = command.value("target", std::string());
        const std::string property = command.value("property", std::string());
        Json value = command.contains("value") ? command["value"] : Json();
        if (value.is_null()) {
            runtime::World view(*scene_);  // reading through the same code games use
            const auto who = game::timeline_target(view, *id, target);
            const auto now = who ? runtime::read_property(view, *who, property) : std::nullopt;
            if (!now) return fail(ErrorCode::InvalidArgument, std::format("timeline: '{}' has no '{}' to key", target.empty() ? "the entity" : target, property));
            value = *now;
        }
        auto track = std::ranges::find_if(t.tracks, [&](const auto& tr) {
            return tr.kind == asset::TimelineTrack::Kind::Value && tr.target == target && tr.property == property;
        });
        if (track == t.tracks.end()) {
            t.tracks.push_back({asset::TimelineTrack::Kind::Value, target, property, {}});
            track = t.tracks.end() - 1;
        }
        std::erase_if(track->keys, [&](const auto& k) { return std::abs(k.t - at) < 1e-4; });
        asset::TimelineKey k;
        k.t = at;
        k.value = value;
        k.ease = command.value("ease", std::string());
        track->keys.push_back(std::move(k));
        asset::sort_keys(*track);
        if (auto s = save(file, t); !s) return std::unexpected(s.error());
        return Json{{"track", track - t.tracks.begin()}, {"keys", track->keys.size()}, {"value", value}};
    }

    if (op == "event") {  // {"t", "target"?, "function"? + "args"?, or "message"? + "data"?}: a call or emit key
        const double at = std::clamp(command.value("t", 0.0), 0.0, t.length);
        const std::string target = command.value("target", std::string());
        const bool call = command.contains("function");
        const auto kind = call ? asset::TimelineTrack::Kind::Call : asset::TimelineTrack::Kind::Emit;
        auto track = std::ranges::find_if(t.tracks, [&](const auto& tr) { return tr.kind == kind && tr.target == target; });
        if (track == t.tracks.end()) {
            t.tracks.push_back({kind, target, "", {}});
            track = t.tracks.end() - 1;
        }
        asset::TimelineKey k;
        k.t = at;
        if (call) {
            k.function = command.value("function", std::string());
            if (command.contains("args") && command["args"].is_array()) k.args = command["args"];
        } else {
            k.message = command.value("message", std::string());
            k.data = command.contains("data") ? command["data"] : Json();
        }
        track->keys.push_back(std::move(k));
        asset::sort_keys(*track);
        if (auto s = save(file, t); !s) return std::unexpected(s.error());
        return Json{{"track", track - t.tracks.begin()}, {"keys", track->keys.size()}};
    }

    if (op == "remove_key") {  // {"track", "key"}; a track left empty goes too
        const int ti = command.value("track", -1), ki = command.value("key", -1);
        if (ti < 0 || ti >= static_cast<int>(t.tracks.size()) || ki < 0 || ki >= static_cast<int>(t.tracks[ti].keys.size()))
            return fail(ErrorCode::InvalidArgument, "timeline: no such track or key");
        t.tracks[ti].keys.erase(t.tracks[ti].keys.begin() + ki);
        if (t.tracks[ti].keys.empty()) t.tracks.erase(t.tracks.begin() + ti);
        if (auto s = save(file, t); !s) return std::unexpected(s.error());
        return Json{{"tracks", t.tracks.size()}};
    }

    if (op == "preview") {  // {"t"}: the scene as the timeline has it then, in the viewport only
        const double at = std::clamp(command.value("t", 0.0), 0.0, t.length);
        // Start from what's really there each time: put back what earlier previews moved (their originals are kept
        // until the preview ends).
        for (const auto& [eid, original] : timeline_preview_)
            if (auto* e = entity_in(*scene_, eid)) *e = original;
        runtime::World view(*scene_);
        game::apply_timeline(view, *id, t, at);
        int moved = 0;
        for (const auto& e : view.scene().entities) {
            const scene::Entity* was = scene_->find(e.id);
            const auto same = [](const scene::Transform& a, const scene::Transform& b) {
                return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
                       a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
                       a.rotation.w == b.rotation.w && a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z;
            };
            if (!was || (same(was->transform, e.transform) && was->components == e.components)) continue;
            auto* mine = entity_in(*scene_, e.id);
            if (!mine) continue;
            if (!timeline_preview_.contains(e.id)) timeline_preview_[e.id] = *mine;
            mine->transform = e.transform;
            mine->components = e.components;
            ++moved;
        }
        republish();
        return Json{{"t", at}, {"changed", moved}, {"previewing", !timeline_preview_.empty()}};
    }
    return fail(ErrorCode::InvalidArgument, std::format("timeline: unknown op \"{}\" (list, info, new, set, key, event, remove_key, preview, preview_end)", op));
}

} // namespace noor::editor
