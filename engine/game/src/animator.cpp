#include "noor/game/animator.h"

#include "noor/foundation/log.h"
#include "noor/asset/pose_blend.h"
#include "noor/game/sprite_animation.h"

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

const AnimatorSystem::Model* AnimatorSystem::model(const Guid& id) {
    auto it = models_.find(id);
    if (it == models_.end()) {
        std::optional<Model> loaded;
        if (const auto file = resolve_ ? resolve_(id) : std::nullopt) {
            if (auto m = asset::import_gltf(*file)) {
                Model data{std::move(*m), {}, {}};
                data.rest = asset::rest_pose(data.model);
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
        it = models_.emplace(id, std::move(loaded)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const std::vector<Guid>& AnimatorSystem::bind(runtime::World& world, const scene::Entity& root) {
    if (const auto it = nodes_.find(root.id); it != nodes_.end()) return it->second;
    std::vector<Guid> ids;
    const Json& m = root.components.at("model");
    const auto id = asset::guid_field(m, "asset");
    const Model* data = id ? model(*id) : nullptr;
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
    if (!cid || !mid) return nullptr;
    if (const auto it = running_.find(root.id); it != running_.end() && it->second.controller == *cid && it->second.model == *mid)
        return it->second.player.get();
    const asset::AnimController* ctrl = controller(*cid);
    const Model* data = model(*mid);
    if (!ctrl || !data) return nullptr;
    auto player = std::make_unique<asset::AnimControllerPlayer>(*ctrl, data->model);
    for (const auto& w : player->warnings()) say_once(std::format("'{}': {}", root.name, w));
    auto* raw = player.get();
    running_[root.id] = {*cid, *mid, std::move(player)};
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
            p->step(dt * speed, parameters(c, *ctrl), &consumed, &ended);
            for (const auto& name : consumed) c["parameters"][name] = false;
            if (const std::string_view state = p->state(); c.value("state", std::string()) != state) c["state"] = std::string(state);
            for (const auto& end : ended) {
                ++stats_.ended;
                world.post({runtime::Event::Kind::AnimationEnd, e.id, e.id, end.state});
            }
            continue;
        }
        // A plain clip: a finished cross-fade forgets what it faded from.
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
        const auto id = asset::guid_field(e.components.at("model"), "asset");
        const Model* data = id ? model(*id) : nullptr;
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
        const auto id = asset::guid_field(root.components.at("model"), "asset");
        const Model* data = id ? model(*id) : nullptr;
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
