// The Animation panel's side of the document (Milestone 8C, part 6, ADR-036): what a model's clips, skeleton and
// humanoid bone map are, the scene's characters, saving controllers and bone map corrections, and the preview: one
// character posed in the viewport from a clip at a time, or from its controller (running, one state, or one
// transition), with its skeleton drawn over it. The preview poses the snapshot only: the scene isn't changed.
#include "document.h"

#include "noor/asset/anim_controller.h"
#include "noor/asset/animation.h"
#include "noor/asset/model_import.h"
#include "noor/asset/retarget.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/game/animation_assets.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace noor::editor {
namespace {
using asset::Json;
namespace stdfs = std::filesystem;

constexpr double kPreviewStep = 1.0 / 60.0;  // controllers are stepped at 60 a second, as a game at 60 fps would

void mix(std::uint64_t& h, float v) {  // FNV-1a over the float's bits (as AnimatorSystem's fingerprint)
    const auto bits = std::bit_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; ++i) {
        h ^= (bits >> (8 * i)) & 0xffu;
        h *= 0x100000001b3ull;
    }
}

std::string hex(std::uint64_t v) { return std::format("{:016x}", v); }

Json vec3(math::Vec3 v) { return Json::array({v.x, v.y, v.z}); }

} // namespace

game::AnimationAssets& Document::anim_assets() {
    if (!anim_assets_) {
        anim_assets_ = std::make_unique<game::AnimationAssets>(
            [this](const Guid& id) -> std::optional<stdfs::path> {
                const auto* rec = registry_.find(id);
                if (!rec) return std::nullopt;
                auto file = project_.resolve(rec->path);
                return file ? std::optional(*file) : std::nullopt;
            },
            [](const std::string& m) { NOOR_LOG_WARN("Animation", "{}", m); });
    }
    return *anim_assets_;
}

void Document::forget_animation_files() {
    if (anim_assets_) anim_assets_->clear();
}

void Document::republish() {
    if (!scene_ || world_) return;  // play mode publishes every frame itself
    std::unordered_set<Guid> authored;
    for (const auto& e : scene_->entities) authored.insert(e.id);
    publish(expanded_for_view(), authored);
}

scene::Scene Document::expanded_for_view() {
    scene::Scene expanded = scene::expand_prefabs(*scene_, prefabs_.resolver()).scene;
    apply_anim_preview(expanded);
    return expanded;
}

// Poses the previewed character in `expanded` (its bone entities' transforms) and works out its skeleton's lines.
void Document::apply_anim_preview(scene::Scene& expanded) {
    anim_bones_.clear();
    if (!anim_preview_ || world_) return;
    AnimPreview& pv = *anim_preview_;
    Json result{{"entity", pv.entity.to_string()}, {"mode", pv.controller ? "controller" : (pv.clip.empty() ? "rest" : "clip")}, {"time", pv.time}};
    std::unordered_map<Guid, std::size_t> index;
    for (std::size_t i = 0; i < expanded.entities.size(); ++i) index.emplace(expanded.entities[i].id, i);
    const auto rit = index.find(pv.entity);
    if (rit == index.end() || !expanded.entities[rit->second].has("model")) {
        result["error"] = "the previewed character isn't in the scene";
        pv.result = std::move(result);
        return;
    }
    const scene::Entity root = expanded.entities[rit->second];  // a copy: posing writes other entities
    const auto* data = anim_assets().model_of(root);
    if (!data) {
        result["error"] = "its model can't be loaded";
        pv.result = std::move(result);
        return;
    }
    std::vector<asset::NodePose> pose(data->rest.begin(), data->rest.end());
    const Json animator = root.has("animator") && root.components.at("animator").is_object() ? root.components.at("animator") : Json::object();

    if (pv.controller) {
        const auto cid = asset::guid_field(animator, "controller");
        const asset::AnimController* ctrl = cid ? anim_assets().controller(*cid) : nullptr;
        if (!ctrl) {
            result["error"] = cid ? "its controller can't be loaded (see the log)" : "its animator has no controller";
            pv.result = std::move(result);
            return;
        }
        asset::AnimControllerPlayer player(*ctrl, data->model, &data->aliases);
        // Parameters: the controller's defaults, then the animator's, then the preview's own.
        asset::AnimParameters params;
        for (const auto& p : ctrl->parameters) params[p.name] = p.initial;
        const auto take = [&](const Json& values) {
            if (!values.is_object()) return;
            for (const auto& [name, v] : values.items()) {
                if (v.is_boolean()) params[name] = v.get<bool>() ? 1.0f : 0.0f;
                else if (v.is_number()) params[name] = v.get<float>();
            }
        };
        if (animator.contains("parameters")) take(animator["parameters"]);
        take(pv.parameters);
        const double speed = animator.contains("speed") && animator["speed"].is_number() ? std::max(0.0, animator["speed"].get<double>()) : 1.0;
        const auto steps_to = [](double seconds) { return static_cast<long>(std::llround(std::max(0.0, seconds) / kPreviewStep)); };
        const long steps = std::min(steps_to(pv.time), 60L * 600);  // at most ten minutes
        std::size_t layer = 0;
        for (std::size_t l = 0; l < ctrl->layers.size(); ++l)
            if (ctrl->layers[l].name == pv.layer) layer = l;
        double length = 10.0;
        if (pv.transition >= 0) {
            // One transition: its "from" state (the layer's entry for "*") plays for its exit time, or half a time
            // through (at most a second), then it fades to its "to" state, which then plays a second more.
            const auto& L = ctrl->layers[layer];
            if (static_cast<std::size_t>(pv.transition) >= L.transitions.size()) {
                result["error"] = std::format("layer '{}' has no transition {}", L.name, pv.transition);
                pv.result = std::move(result);
                return;
            }
            const auto& t = L.transitions[static_cast<std::size_t>(pv.transition)];
            const std::string from = L.states[static_cast<std::size_t>(t.from >= 0 ? t.from : L.entry)].name;
            const std::string to = L.states[static_cast<std::size_t>(t.to)].name;
            const double len = player.state_length(layer, from, params);
            const double lead = t.exit_time ? *t.exit_time * len : std::clamp(0.5 * len, 0.25, 1.0);
            length = lead + t.fade + 1.0;
            player.set_automatic(false);
            player.play(from, 0.0f, L.name);
            const long lead_steps = steps_to(lead);
            long s = 0;
            for (; s < std::min(steps, lead_steps); ++s) player.step(kPreviewStep * speed, params);
            if (steps >= lead_steps) {
                player.play(to, t.fade, L.name);
                for (; s < steps; ++s) player.step(kPreviewStep * speed, params);
            }
            result["lead"] = lead;
            result["fade"] = t.fade;
            result["from"] = from;
            result["to"] = to;
            result["part"] = steps < lead_steps ? "before" : (player.fade_weight(layer) < 1.0f ? "fading" : "after");
        } else if (!pv.state.empty()) {
            // One state, from its start.
            player.set_automatic(false);
            if (!player.play(pv.state, 0.0f, ctrl->layers[layer].name)) {
                result["error"] = std::format("layer '{}' has no state '{}'", ctrl->layers[layer].name, pv.state);
                pv.result = std::move(result);
                return;
            }
            length = std::max(0.1, player.state_length(layer, pv.state, params));
            for (long s = 0; s < steps; ++s) player.step(kPreviewStep * speed, params);
        } else {
            // The controller running from its start with these parameters, as in a game (a trigger fires once).
            std::vector<std::string> consumed;
            for (long s = 0; s < steps; ++s) {
                consumed.clear();
                player.step(kPreviewStep * speed, params, &consumed);
                for (const auto& name : consumed) params[name] = 0.0f;
            }
        }
        player.pose(params, pose);
        Json states = Json::array(), weights = Json::array();
        for (std::size_t l = 0; l < player.layers(); ++l) {
            states.push_back(std::string(player.state(l)));
            weights.push_back(player.fade_weight(l));
        }
        result["states"] = std::move(states);
        result["fade_weights"] = std::move(weights);
        result["length"] = length;
        Json warnings = Json::array();
        for (const auto& w : player.warnings()) warnings.push_back(w);
        result["warnings"] = std::move(warnings);
    } else if (!pv.clip.empty()) {
        const auto* clip = asset::find_animation(data->model, pv.clip);
        if (!clip) {
            result["error"] = std::format("its model has no clip '{}'", pv.clip);
            pv.result = std::move(result);
            return;
        }
        asset::sample(*clip, std::clamp(pv.time, 0.0, static_cast<double>(clip->duration)), pose);
        result["length"] = clip->duration;
    }
    // In place: the root-motion bone held horizontally, as a character with "root_motion" shows it.
    if (pv.in_place && data->root >= 0) {
        auto& rp = pose[static_cast<std::size_t>(data->root)].translation;
        rp.x = data->rest[static_cast<std::size_t>(data->root)].translation.x;
        rp.z = data->rest[static_cast<std::size_t>(data->root)].translation.z;
    }

    // Into the scene copy: every node a clip moves (the others are already at rest).
    const std::vector<Guid> ids = game::model_node_entities(expanded, root, data->model);
    std::uint64_t fingerprint = 0xcbf29ce484222325ull;
    std::size_t posed = 0;
    for (const int n : data->animated) {
        if (static_cast<std::size_t>(n) >= ids.size() || ids[static_cast<std::size_t>(n)].is_nil()) continue;
        const auto it = index.find(ids[static_cast<std::size_t>(n)]);
        if (it == index.end()) continue;
        const asset::NodePose& p = pose[static_cast<std::size_t>(n)];
        expanded.entities[it->second].transform = {p.translation, p.rotation, p.scale};
        for (const float v : {p.translation.x, p.translation.y, p.translation.z, p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w, p.scale.x,
                              p.scale.y, p.scale.z})
            mix(fingerprint, v);
        ++posed;
    }
    result["fingerprint"] = hex(fingerprint);
    result["nodes"] = posed;

    // The skeleton: the skins' joints (every node if the model has no skin), each joined to its nearest jointed
    // ancestor. A bone is drawn from a joint to each child joint, as Blender draws one from its head to its tail.
    const auto worlds = expanded.world_matrices();
    const auto world_pos = [&](int node) -> std::optional<math::Vec3> {
        if (node < 0 || static_cast<std::size_t>(node) >= ids.size() || ids[static_cast<std::size_t>(node)].is_nil()) return std::nullopt;
        const auto it = index.find(ids[static_cast<std::size_t>(node)]);
        if (it == index.end()) return std::nullopt;
        return worlds[it->second].transform_point({0, 0, 0});
    };
    std::vector<bool> joint(data->model.nodes.size(), data->model.skins.empty());
    for (const auto& skin : data->model.skins)
        for (const int j : skin.joints)
            if (j >= 0 && static_cast<std::size_t>(j) < joint.size()) joint[static_cast<std::size_t>(j)] = true;
    int highlight = -1;
    for (std::size_t n = 0; n < data->model.nodes.size(); ++n)
        if (!pv.highlight.empty() && data->model.nodes[n].name == pv.highlight) highlight = static_cast<int>(n);
    std::size_t joints = 0;
    for (std::size_t n = 0; n < data->model.nodes.size(); ++n) {
        if (!joint[n]) continue;
        ++joints;
        const auto here = world_pos(static_cast<int>(n));
        if (!here) continue;
        int parent = data->model.nodes[n].parent;
        while (parent >= 0 && !joint[static_cast<std::size_t>(parent)]) parent = data->model.nodes[static_cast<std::size_t>(parent)].parent;
        const auto there = parent >= 0 ? world_pos(parent) : std::nullopt;
        if (pv.skeleton) anim_bones_.push_back({there.value_or(*here), *here, parent >= 0 && parent == highlight, static_cast<int>(n) == highlight});
    }
    result["joints"] = joints;
    result["bones"] = anim_bones_.size();
    if (data->root >= 0)
        if (const auto p = world_pos(data->root)) result["root_position"] = vec3(*p);
    if (highlight >= 0)
        if (const auto p = world_pos(highlight)) result["highlight_position"] = vec3(*p);
    pv.result = std::move(result);
}

Result<Json> Document::animation(const Json& command) {
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
        return fail(ErrorCode::InvalidArgument, "animation: the command needs an \"op\"");
    const std::string op = command["op"];
    const auto relative = [&](const stdfs::path& file) {
        const auto rel = project_.relative(file);
        return rel ? *rel : file.generic_string();
    };
    const auto path_of = [&](const Guid& id) -> Json {
        const auto* rec = registry_.find(id);
        return rec ? Json(rec->path) : Json(nullptr);
    };
    // An asset named by "asset" (a project path): its file and registry record.
    struct Named {
        stdfs::path file;
        const asset::AssetRecord* rec;
    };
    const auto named = [&](const char* importer) -> Result<Named> {
        if (!command.contains("asset") || !command["asset"].is_string())
            return fail(ErrorCode::InvalidArgument, "animation: \"asset\" (a project path) is needed");
        auto file = project_.resolve(command["asset"].get<std::string>());
        if (!file) return std::unexpected(file.error());
        if (!stdfs::exists(*file)) return fail(ErrorCode::NotFound, std::format("nothing at {}", command["asset"].get<std::string>()));
        if (auto s = registry_.scan(project_); !s) return std::unexpected(s.error());
        const auto rel = project_.relative(*file);
        const auto* rec = rel ? registry_.find_path(*rel) : nullptr;
        const bool model = std::string_view(importer) == "gltf";
        if (!rec || (model ? rec->importer != "gltf" && rec->importer != "fbx" : rec->importer != importer))
            return fail(ErrorCode::InvalidArgument, std::format("{} isn't {}", relative(*file), std::string(importer) == "gltf" ? "a model" : "an animation controller"));
        return Named{*file, rec};
    };
    const auto clips_json = [](const asset::ImportedModel& m) {
        Json clips = Json::array();
        for (const auto& c : m.animations) {
            std::unordered_set<int> nodes;
            for (const auto& ch : c.channels) nodes.insert(ch.node);
            clips.push_back({{"name", c.name}, {"duration", c.duration}, {"channels", c.channels.size()}, {"nodes", nodes.size()}});
        }
        return clips;
    };
    // A model: its clips, its skeleton and its humanoid bone map (detected, with its .noormeta's corrections).
    const auto describe_model = [&](const Named& m) -> Result<Json> {
        auto model = asset::import_model(m.file);
        if (!model) return std::unexpected(model.error());
        Json corrections = Json::object();
        {
            std::ifstream in(m.file.string() + ".noormeta", std::ios::binary);
            const Json meta = in ? Json::parse(in, nullptr, false) : Json();
            if (meta.is_object() && meta.contains("settings") && meta["settings"].is_object() && meta["settings"].contains("bone_map") &&
                meta["settings"]["bone_map"].is_object())
                corrections = meta["settings"]["bone_map"];
        }
        const asset::BoneMap map = asset::bone_map(*model, corrections);
        std::vector<bool> joint(model->nodes.size(), false);
        for (const auto& skin : model->skins)
            for (const int j : skin.joints)
                if (j >= 0 && static_cast<std::size_t>(j) < joint.size()) joint[static_cast<std::size_t>(j)] = true;
        Json nodes = Json::array();
        for (std::size_t n = 0; n < model->nodes.size(); ++n)
            nodes.push_back({{"name", model->nodes[n].name}, {"parent", model->nodes[n].parent}, {"joint", static_cast<bool>(joint[n])}});
        Json bones = Json::array();
        for (std::size_t b = 0; b < asset::kHumanBones; ++b) {
            const auto hb = static_cast<asset::HumanBone>(b);
            const std::string name(asset::human_bone_name(hb));
            const int node = map.nodes[b];
            bones.push_back({{"bone", name},
                             {"required", asset::human_bone_required(hb)},
                             {"node", node >= 0 ? Json(model->nodes[static_cast<std::size_t>(node)].name) : Json(nullptr)},
                             {"corrected", corrections.contains(name)}});
        }
        Json warnings = Json::array();
        for (const auto& w : map.warnings) warnings.push_back(w);
        return Json{{"path", relative(m.file)},
                    {"guid", m.rec->guid.to_string()},
                    {"name", m.file.stem().string()},
                    {"clips", clips_json(*model)},
                    {"nodes", std::move(nodes)},
                    {"skinned", !model->skins.empty()},
                    {"bone_map", {{"preset", map.preset}, {"usable", map.usable()}, {"bones", std::move(bones)}, {"warnings", std::move(warnings)}}}};
    };
    const auto describe_controller = [&](const Named& c) -> Result<Json> {
        auto text = noor::fs::read_file(c.file, 4 << 20);
        if (!text) return std::unexpected(text.error());
        Json out{{"path", relative(c.file)}, {"guid", c.rec->guid.to_string()}, {"name", c.file.stem().string()}};
        auto json = asset::parse_json(*text, relative(c.file));
        if (!json) {
            out["error"] = json.error().message;
            return out;
        }
        out["controller"] = *json;
        if (auto parsed = asset::parse_anim_controller(*json, relative(c.file)); !parsed) out["error"] = parsed.error().message;
        return out;
    };
    const auto preview_state = [&]() -> Json {
        if (!anim_preview_) return Json{{"entity", nullptr}};
        Json out = anim_preview_->result;
        out["skeleton"] = anim_preview_->skeleton;
        out["in_place"] = anim_preview_->in_place;
        out["highlight"] = anim_preview_->highlight.empty() ? Json(nullptr) : Json(anim_preview_->highlight);
        if (anim_preview_->controller) {
            out["parameters"] = anim_preview_->parameters;
            out["layer"] = anim_preview_->layer;
            if (!anim_preview_->state.empty()) out["state"] = anim_preview_->state;
            if (anim_preview_->transition >= 0) out["transition"] = anim_preview_->transition;
        } else {
            out["clip"] = anim_preview_->clip;
        }
        return out;
    };

    if (op == "model") {
        auto m = named("gltf");
        if (!m) return std::unexpected(m.error());
        return describe_model(*m);
    }
    if (op == "set_bone") {  // a correction to a model's bone map: {"bone", "node": name | null (no bone), "auto": true (detected)}
        auto m = named("gltf");
        if (!m) return std::unexpected(m.error());
        const std::string bone = command.value("bone", std::string());
        if (bone != "*" && !asset::parse_human_bone(bone)) return fail(ErrorCode::InvalidArgument, std::format("set_bone: '{}' isn't a humanoid bone", bone));
        const stdfs::path meta_file = m->file.string() + ".noormeta";
        auto text = noor::fs::read_file(meta_file, 1 << 20);
        if (!text) return std::unexpected(text.error());
        auto meta = asset::parse_json(*text, relative(meta_file));
        if (!meta || !meta->is_object()) return fail(ErrorCode::InvalidArgument, std::format("{} can't be read", relative(meta_file)));
        Json& settings = (*meta)["settings"];
        if (!settings.is_object()) settings = Json::object();
        Json& map = settings["bone_map"];
        if (!map.is_object()) map = Json::object();
        if (bone == "*") {
            map = Json::object();  // every bone detected again
        } else if (command.value("auto", false)) {
            map.erase(bone);
        } else if (!command.contains("node") || command["node"].is_null()) {
            map[bone] = nullptr;
        } else if (command["node"].is_string()) {
            auto model = asset::import_model(m->file);
            if (!model) return std::unexpected(model.error());
            const std::string node = command["node"].get<std::string>();
            if (std::ranges::none_of(model->nodes, [&](const asset::ImportedNode& n) { return n.name == node; }))
                return fail(ErrorCode::NotFound, std::format("{} has no bone '{}'", relative(m->file), node));
            map[bone] = node;
        } else {
            return fail(ErrorCode::InvalidArgument, "set_bone: \"node\" is a bone's name or null");
        }
        if (map.empty()) settings.erase("bone_map");
        if (auto w = noor::fs::write_file_atomic(meta_file, asset::to_text(*meta), false); !w) return std::unexpected(w.error());
        forget_animation_files();
        republish();
        return describe_model(*m);
    }
    if (op == "controller") {
        auto c = named("anim_controller");
        if (!c) return std::unexpected(c.error());
        return describe_controller(*c);
    }
    if (op == "save_controller") {  // one that doesn't parse isn't written
        auto c = named("anim_controller");
        if (!c) return std::unexpected(c.error());
        if (!command.contains("controller") || !command["controller"].is_object())
            return fail(ErrorCode::InvalidArgument, "save_controller: \"controller\" (an object) is needed");
        Json ctrl = command["controller"];
        ctrl["noor_anim"] = 1;
        if (auto parsed = asset::parse_anim_controller(ctrl, relative(c->file)); !parsed) return std::unexpected(parsed.error());
        if (auto w = noor::fs::write_file_atomic(c->file, asset::to_text(ctrl), false); !w) return std::unexpected(w.error());
        forget_animation_files();
        republish();
        return describe_controller(*c);
    }
    if (op == "characters") {  // the open scene's animated or skinned models
        if (auto s = require_scene(); !s) return std::unexpected(s.error());
        const scene::Scene expanded = scene::expand_prefabs(*scene_, prefabs_.resolver()).scene;
        Json list = Json::array();
        for (const auto& e : expanded.entities) {
            if (!e.has("model") || !e.components.at("model").is_object()) continue;
            const auto model_id = asset::guid_field(e.components.at("model"), "asset");
            if (!model_id) continue;
            // Only models placed with a node list (scene::instantiate_model writes one for animated or skinned models)
            // or with an animator are loaded: a forest's trees aren't opened to find out they don't move.
            const bool has_animator = e.has("animator") && e.components.at("animator").is_object();
            const Json& placed = e.components.at("model");
            if (!has_animator && !(placed.contains("nodes") && placed["nodes"].is_array())) continue;
            const auto* data = anim_assets().model_of(e);
            if (!has_animator && (!data || (data->model.animations.empty() && data->model.skins.empty()))) continue;
            const Json animator = has_animator ? e.components.at("animator") : Json(nullptr);
            const auto controller = has_animator ? asset::guid_field(animator, "controller") : std::nullopt;
            const auto from = has_animator ? asset::guid_field(animator, "retarget_from") : std::nullopt;
            list.push_back({{"entity", e.id.to_string()},
                            {"name", e.name},
                            {"model", path_of(*model_id)},
                            {"model_guid", model_id->to_string()},
                            {"animator", animator},
                            {"controller", controller ? path_of(*controller) : Json(nullptr)},
                            {"retarget_from", from ? path_of(*from) : Json(nullptr)},
                            {"clips", data ? clips_json(data->model) : Json::array()},
                            {"skinned", data && !data->model.skins.empty()}});
        }
        return list;
    }
    if (op == "preview") {
        // Keys given change the preview, the rest stay; "entity": null ends it.
        if (command.contains("entity")) {
            if (command["entity"].is_null()) {
                const bool was = anim_preview_.has_value();
                anim_preview_.reset();
                if (was) republish();
                return preview_state();
            }
            const auto id = asset::guid_field(command, "entity");
            if (!id) return fail(ErrorCode::InvalidArgument, "preview: \"entity\" is an entity id or null");
            if (!anim_preview_ || anim_preview_->entity != *id) anim_preview_ = AnimPreview{*id};
        }
        if (!anim_preview_) return fail(ErrorCode::InvalidArgument, "preview: no character is previewed (give \"entity\")");
        AnimPreview& pv = *anim_preview_;
        if (command.contains("clip") && command["clip"].is_string()) {
            pv.clip = command["clip"];
            pv.controller = false;
        }
        if (command.contains("controller") && command["controller"].is_boolean()) pv.controller = command["controller"].get<bool>();
        if (command.contains("time") && command["time"].is_number()) pv.time = std::max(0.0, command["time"].get<double>());
        if (command.contains("parameters") && command["parameters"].is_object()) pv.parameters = command["parameters"];
        if (command.contains("layer") && command["layer"].is_string()) pv.layer = command["layer"];
        if (command.contains("state")) {
            pv.state = command["state"].is_string() ? command["state"].get<std::string>() : std::string();
            if (!pv.state.empty()) pv.transition = -1;
        }
        if (command.contains("transition")) {
            pv.transition = command["transition"].is_number_integer() ? command["transition"].get<int>() : -1;
            if (pv.transition >= 0) pv.state.clear();
        }
        if (command.contains("skeleton") && command["skeleton"].is_boolean()) pv.skeleton = command["skeleton"].get<bool>();
        if (command.contains("in_place") && command["in_place"].is_boolean()) pv.in_place = command["in_place"].get<bool>();
        if (command.contains("highlight")) pv.highlight = command["highlight"].is_string() ? command["highlight"].get<std::string>() : std::string();
        if (world_) return fail(ErrorCode::InvalidArgument, "preview: Stop play mode first");
        republish();
        return preview_state();
    }
    return fail(ErrorCode::InvalidArgument, std::format("animation: unknown op \"{}\"", op));
}

} // namespace noor::editor
