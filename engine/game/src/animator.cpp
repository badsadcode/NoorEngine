#include "noor/game/animator.h"

#include "noor/foundation/log.h"
#include "noor/asset/pose_blend.h"
#include "noor/game/sprite_animation.h"
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

namespace noor::game {
namespace {

using asset::Json;

void mix(std::uint64_t& h, float v) {  // FNV-1a over the float's bits
    const auto bits = std::bit_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; ++i) {
        h ^= (bits >> (8 * i)) & 0xffu;
        h *= 0x100000001b3ull;
    }
}

} // namespace

double animator_time(const Json& c, double seconds, double duration) {
    const double t = sprite_time(c, seconds);  // the same clock as a sprite's: scene time, or since a script played it
    if (duration <= 0.0) return 0.0;
    const bool loop = !c.is_object() || !c.contains("loop") || !c["loop"].is_boolean() || c["loop"].get<bool>();
    if (loop) return t - std::floor(t / duration) * duration;
    return std::clamp(t, 0.0, duration);
}

void AnimatorSystem::say_once(const std::string& message) {
    if (said_.emplace(message, true).second) NOOR_LOG_WARN("Animation", "{}", message);
}

const AnimatorSystem::Model* AnimatorSystem::model(const Guid& id, const Guid& clips) {
    const std::string key = clips.is_nil() ? id.to_string() : id.to_string() + "<" + clips.to_string();
    auto it = models_.find(key);
    if (it == models_.end()) {
        std::optional<Model> loaded;
        if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
            if (auto m = asset::import_gltf(*file)) {
                Model data{std::move(*m), {}, {}};
                const asset::BoneMap own = asset::bone_map_for(data.model, *file);
                for (std::size_t b = 0; b < asset::kHumanBones; ++b)
                    if (own.nodes[b] >= 0) data.aliases.emplace(asset::human_bone_name(static_cast<asset::HumanBone>(b)), own.nodes[b]);
                if (!clips.is_nil()) {
                    // Another model's clips, retargeted onto this one (once per pair).
                    const auto source_file = resolve_(clips);
                    auto source = source_file ? asset::import_gltf(*source_file) : decltype(asset::import_gltf(*file)){};
                    if (!source_file) {
                        say_once(std::format("an animator's retarget_from {} isn't in the project", clips.to_string()));
                        data.model.animations.clear();
                    } else if (!source) {
                        say_once(source.error().message);
                        data.model.animations.clear();
                    } else {
                        const asset::BoneMap theirs = asset::bone_map_for(*source, *source_file);
                        const asset::Retargeter retargeter(*source, theirs, data.model, own);
                        // A controller written for the clips' model names its bones: they mean this model's.
                        for (std::size_t b = 0; b < asset::kHumanBones; ++b)
                            if (theirs.nodes[b] >= 0 && own.nodes[b] >= 0)
                                data.aliases.emplace(source->nodes[static_cast<std::size_t>(theirs.nodes[b])].name, own.nodes[b]);
                        for (const auto& w : retargeter.warnings())
                            say_once(std::format("retargeting {}'s clips onto {}: {}", source_file->filename().string(),
                                                 file->filename().string(), w));
                        std::vector<asset::ImportedAnimation> baked;
                        if (retargeter.usable())
                            for (const auto& clip : source->animations) baked.push_back(retargeter.bake(clip));
                        data.model.animations = std::move(baked);
                    }
                }
                data.rest = asset::rest_pose(data.model);
                data.root = asset::root_motion_node(data.model);
                if (data.root >= 0)
                    if (const int parent = data.model.nodes[static_cast<std::size_t>(data.root)].parent; parent >= 0)
                        data.root_space = data.model.world_matrix(static_cast<std::size_t>(parent));
                std::vector<bool> moved(data.model.nodes.size(), false);
                for (const auto& clip : data.model.animations)
                    for (const auto& ch : clip.channels)
                        if (ch.node >= 0 && static_cast<std::size_t>(ch.node) < moved.size()) moved[static_cast<std::size_t>(ch.node)] = true;
                for (std::size_t i = 0; i < moved.size(); ++i)
                    if (moved[i]) data.animated.push_back(static_cast<int>(i));
                loaded = std::move(data);
            } else {
                say_once(m.error().message);
            }
        } else {
            say_once(std::format("an animator's model {} isn't in the project", id.to_string()));
        }
        it = models_.emplace(key, std::move(loaded)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const AnimatorSystem::Model* AnimatorSystem::model_of(const scene::Entity& root) {
    const auto id = asset::guid_field(root.components.at("model"), "asset");
    if (!id) return nullptr;
    const auto clips = root.has("animator") ? asset::guid_field(root.components.at("animator"), "retarget_from") : std::nullopt;
    return model(*id, clips.value_or(Guid{}));
}

const std::vector<Guid>& AnimatorSystem::bind(runtime::World& world, const scene::Entity& root) {
    if (const auto it = nodes_.find(root.id); it != nodes_.end()) return it->second;
    std::vector<Guid> ids;
    const Json& m = root.components.at("model");
    const Model* data = model_of(root);
    if (data && m.contains("nodes") && m["nodes"].is_array()) {
        for (const auto& n : m["nodes"]) {
            Guid g{};
            if (n.is_string())
                if (auto parsed = Guid::parse(n.get<std::string>())) g = *parsed;
            ids.push_back(g);
        }
    } else if (data) {
        // Placed before 8C part 2: no node list. Match the model's nodes by name among the root's descendants
        // (entities come parents first, so one pass finds them all).
        say_once(std::format("'{}' was placed before animation existed (no node list); its bones are matched by name. "
                             "Placing it again fixes this",
                             root.name));
        std::unordered_set<Guid> below{root.id};
        std::unordered_map<std::string, Guid> by_name;
        for (const auto& e : world.scene().entities) {
            if (!e.parent || !below.contains(*e.parent)) continue;
            below.insert(e.id);
            by_name.emplace(e.name, e.id);
        }
        for (const auto& node : data->model.nodes) {
            const auto f = by_name.find(node.name);
            ids.push_back(f != by_name.end() ? f->second : Guid{});
        }
    }
    return nodes_.emplace(root.id, std::move(ids)).first->second;
}

const asset::AnimController* AnimatorSystem::controller(const Guid& id) {
    auto it = controllers_.find(id);
    if (it == controllers_.end()) {
        std::optional<asset::AnimController> loaded;
        if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
            if (auto c = asset::load_anim_controller(*file)) loaded = std::move(*c);
            else say_once(c.error().message);
        } else {
            say_once(std::format("an animator's controller {} isn't in the project", id.to_string()));
        }
        it = controllers_.emplace(id, std::move(loaded)).first;
    }
    return it->second ? &*it->second : nullptr;
}

// The controller running for a character (made the first time, again if its controller or model changes); nullptr
// if it has none or it can't run.
asset::AnimControllerPlayer* AnimatorSystem::running(const scene::Entity& root) {
    const Json& c = root.components.at("animator");
    const auto cid = asset::guid_field(c, "controller");
    const auto mid = asset::guid_field(root.components.at("model"), "asset");
    const Guid clips = asset::guid_field(c, "retarget_from").value_or(Guid{});
    if (!cid || !mid) return nullptr;
    if (const auto it = running_.find(root.id);
        it != running_.end() && it->second.controller == *cid && it->second.model == *mid && it->second.clips == clips)
        return it->second.player.get();
    const asset::AnimController* ctrl = controller(*cid);
    const Model* data = model(*mid, clips);
    if (!ctrl || !data) return nullptr;
    auto player = std::make_unique<asset::AnimControllerPlayer>(*ctrl, data->model, &data->aliases);
    for (const auto& w : player->warnings()) say_once(std::format("'{}': {}", root.name, w));
    auto* raw = player.get();
    running_[root.id] = {*cid, *mid, clips, std::move(player)};
    return raw;
}

asset::AnimParameters AnimatorSystem::parameters(const Json& c, const asset::AnimController& ctrl) const {
    asset::AnimParameters params;
    for (const auto& p : ctrl.parameters) params[p.name] = p.initial;
    if (c.contains("parameters") && c["parameters"].is_object())
        for (const auto& [name, v] : c["parameters"].items()) {
            if (v.is_boolean()) params[name] = v.get<bool>() ? 1.0f : 0.0f;
            else if (v.is_number()) params[name] = v.get<float>();
        }
    return params;
}

// Root motion: `moved` is in the model root's own space (callers bring it there from the root bone's parent's). A character controller at or above
// the model takes it as this frame's velocity; without one the model itself moves.
void AnimatorSystem::move_character(runtime::World& world, scene::Entity& model_root, math::Vec3 moved, double dt) {
    if (dt <= 0.0) return;
    moved.y = 0.0f;  // up and down stays in the pose
    auto* physics = world.system<physics::PhysicsSystem>();
    for (const scene::Entity* e = &model_root; e; e = e->parent ? world.find(*e->parent) : nullptr) {
        if (!e->has("character") || !physics || !physics->has_character(e->id)) continue;
        const auto m = runtime::api::world_matrix(world, model_root.id);
        if (!m) return;
        const math::Vec4 w = *m * math::Vec4{moved.x, moved.y, moved.z, 0.0f};
        const float inv = static_cast<float>(1.0 / dt);
        physics->character_move(e->id, {w.x * inv, 0.0f, w.z * inv});
        return;
    }
    const auto& t = model_root.transform;
    const math::Vec3 d = math::rotate(t.rotation, {moved.x * t.scale.x, 0.0f, moved.z * t.scale.z});
    model_root.transform.position = {t.position.x + d.x, t.position.y + d.y, t.position.z + d.z};
}

void AnimatorSystem::attached(runtime::World& world) {
    // Scripts (runtime::api::play_animation, cross_fade) reach a character's controller through this.
    world.handle("animator.play", [this](runtime::World& w, const Guid& id, const Json& args) {
        const scene::Entity* e = w.find(id);
        if (!e || !e->has("animator") || !e->has("model")) return;
        if (auto* p = running(*e))
            if (!p->play(args.value("state", std::string()), args.value("fade", 0.0f), args.value("layer", std::string())))
                say_once(std::format("'{}' has no state '{}'", e->name, args.value("state", std::string())));
    });
}

void AnimatorSystem::update(runtime::World& world, double dt) {
    // The time this frame ends at, as the pose is drawn (late_update): a clip has ended when its last pose is shown.
    const double now = world.time() + dt;
    auto& entities = world.scene().entities;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        scene::Entity& e = entities[i];
        if (!e.has("animator") || !e.has("model")) continue;
        Json& c = e.components["animator"];
        if (!c.is_object()) continue;
        // A controller: one step; triggers it used are reset, its base state and ended states reported.
        if (c.contains("controller")) {
            auto* p = running(e);
            if (!p) continue;
            const auto* ctrl = controller(running_[e.id].controller);
            std::vector<std::string> consumed;
            std::vector<asset::AnimControllerPlayer::Ended> ended;
            const double speed = c.contains("speed") && c["speed"].is_number() ? std::max(0.0, c["speed"].get<double>()) : 1.0;
            const Model* data = model_of(e);
            const bool root_motion = c.value("root_motion", false) && data && data->root >= 0;
            p->set_root_motion(root_motion ? data->root : -1);
            std::vector<asset::AnimControllerPlayer::Ended> events;
            p->step(dt * speed, parameters(c, *ctrl), &consumed, &ended, &events);
            for (const auto& ev : events) world.post({runtime::Event::Kind::AnimationEvent, e.id, e.id, ev.event});
            if (root_motion) move_character(world, e, math::transform_vector(data->root_space, p->root_motion()), dt);
            for (const auto& name : consumed) c["parameters"][name] = false;
            if (const std::string_view state = p->state(); c.value("state", std::string()) != state) c["state"] = std::string(state);
            for (const auto& end : ended) {
                ++stats_.ended;
                world.post({runtime::Event::Kind::AnimationEnd, e.id, e.id, end.state});
            }
            continue;
        }
        // A plain clip: its events and root motion over this frame (from the clock at its start to its end).
        if (c.contains("animation") && c["animation"].is_string()) {
            const Model* data = model_of(e);
            const asset::ImportedAnimation* clip = data ? asset::find_animation(data->model, c["animation"].get<std::string>()) : nullptr;
            if (clip) {
                const double t0 = sprite_time(c, world.time()), t1 = sprite_time(c, now), d = clip->duration;
                const bool loop = c.value("loop", true);
                if (c.contains("events") && c["events"].is_array())
                    for (const auto& ev : c["events"]) {
                        if (!ev.is_object() || ev.value("clip", std::string()) != clip->name) continue;
                        for (int n = asset::crossings(t0, t1, ev.value("at", 0.0) * d, d, loop); n > 0; --n)
                            world.post({runtime::Event::Kind::AnimationEvent, e.id, e.id, ev.value("name", std::string())});
                    }
                if (c.value("root_motion", false) && data->root >= 0) {
                    math::Vec3 moved = asset::root_delta(*clip, data->root, data->rest[static_cast<std::size_t>(data->root)], t0, t1, loop);
                    // Cross-fading: both clips move it, weighted by how far the fade is.
                    if (c.contains("from") && c["from"].is_object() && c.contains("started") && c["started"].is_number() && c.value("fade", 0.0) > 0.0) {
                        const Json& f = c["from"];
                        if (const auto* old = asset::find_animation(data->model, f.value("animation", std::string()))) {
                            const math::Vec3 o = asset::root_delta(*old, data->root, data->rest[static_cast<std::size_t>(data->root)],
                                                                   sprite_time(f, world.time()), sprite_time(f, now), f.value("loop", true));
                            const float w = static_cast<float>(std::clamp((now - c["started"].get<double>()) / c["fade"].get<double>(), 0.0, 1.0));
                            moved = {o.x + (moved.x - o.x) * w, o.y + (moved.y - o.y) * w, o.z + (moved.z - o.z) * w};
                        }
                    }
                    move_character(world, e, math::transform_vector(data->root_space, moved), dt);
                }
            }
        }
        // A finished cross-fade forgets what it faded from.
        if (c.contains("from") && c.contains("started") && c["started"].is_number()) {
            const double fade = c.value("fade", 0.0);
            if (fade <= 0.0 || now - c["started"].get<double>() >= fade - 1e-9) {
                c.erase("from");
                c.erase("fade");
            }
        }
        // The end of a one-shot a script played (once per play).
        if (!c.contains("started") || !c["started"].is_number() || c.contains("paused_at") || !c.contains("animation") ||
            !c["animation"].is_string() || c.value("loop", true))
            continue;
        const double started = c["started"].get<double>();
        if (const auto r = reported_.find(e.id); r != reported_.end() && r->second == started) continue;  // already told
        const Model* data = model_of(e);
        const std::string name = c["animation"].get<std::string>();
        const asset::ImportedAnimation* clip = data ? asset::find_animation(data->model, name) : nullptr;
        if (!clip || clip->duration <= 0.0f) continue;
        if (sprite_time(c, now) + 1e-9 < clip->duration) continue;
        reported_[e.id] = started;
        ++stats_.ended;
        world.post({runtime::Event::Kind::AnimationEnd, e.id, e.id, name});
    }
}

void AnimatorSystem::late_update(runtime::World& world, double dt) {
    // The game time this frame ends at (the world's clock moves on after late_update): what the frame is drawn at,
    // the same moment sprites show, and what scripts read as noor.time() next frame.
    const double now = world.time() + dt;
    stats_.animators = stats_.nodes = 0;
    stats_.fingerprint = 0xcbf29ce484222325ull;
    // Roots first (by index: posing writes other entities' transforms, not the list).
    const std::size_t count = world.scene().entities.size();
    for (std::size_t r = 0; r < count; ++r) {
        const scene::Entity& root = world.scene().entities[r];
        if (!root.has("animator") || !root.has("model")) continue;
        const Json& c = root.components.at("animator");
        if (!c.is_object()) continue;
        const Model* data = model_of(root);
        if (!data) continue;
        pose_.assign(data->rest.begin(), data->rest.end());
        if (c.contains("controller")) {
            auto* p = running(root);
            if (!p) continue;
            p->pose(parameters(c, *controller(running_[root.id].controller)), pose_);
        } else {
            if (!c.contains("animation") || !c["animation"].is_string()) continue;
            const std::string name = c["animation"].get<std::string>();
            const asset::ImportedAnimation* clip = asset::find_animation(data->model, name);
            if (!clip) {
                say_once(std::format("'{}' has no animation '{}'", root.name, name));
                continue;
            }
            asset::sample(*clip, animator_time(c, now, clip->duration), pose_);
            // Cross-fading: the clip it's leaving, on its own clock, blended out over "fade" seconds.
            if (c.contains("from") && c["from"].is_object() && c.contains("started") && c["started"].is_number() && c.value("fade", 0.0) > 0.0) {
                const Json& f = c["from"];
                const asset::ImportedAnimation* old = asset::find_animation(data->model, f.value("animation", std::string()));
                const double w = std::clamp((now - c["started"].get<double>()) / c["fade"].get<double>(), 0.0, 1.0);
                if (old && w < 1.0) {
                    from_.assign(data->rest.begin(), data->rest.end());
                    asset::sample(*old, animator_time(f, now, old->duration), from_);
                    asset::blend(from_, pose_, static_cast<float>(w), pose_);
                }
            }
            // Root motion: the root bone stays put horizontally (its movement moved the character in update).
            if (c.value("root_motion", false) && data->root >= 0) {
                auto& rp = pose_[static_cast<std::size_t>(data->root)].translation;
                rp.x = data->rest[static_cast<std::size_t>(data->root)].translation.x;
                rp.z = data->rest[static_cast<std::size_t>(data->root)].translation.z;
            }
        }
        const std::vector<Guid>& ids = bind(world, root);
        for (const int n : data->animated) {
            if (static_cast<std::size_t>(n) >= ids.size() || ids[static_cast<std::size_t>(n)].is_nil()) continue;
            scene::Entity* bone = world.find(ids[static_cast<std::size_t>(n)]);
            if (!bone) continue;
            const asset::NodePose& p = pose_[static_cast<std::size_t>(n)];
            bone->transform = {p.translation, p.rotation, p.scale};
            for (const float v : {p.translation.x, p.translation.y, p.translation.z, p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w,
                                  p.scale.x, p.scale.y, p.scale.z})
                mix(stats_.fingerprint, v);
            ++stats_.nodes;
        }
        ++stats_.animators;
    }
}

void AnimatorSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) {
    nodes_.erase(entity.id);
    reported_.erase(entity.id);
    running_.erase(entity.id);
}

} // namespace noor::game
