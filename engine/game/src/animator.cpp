#include "noor/game/animator.h"

#include "noor/foundation/log.h"
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

void AnimatorSystem::update(runtime::World& world, double) {
    const double now = world.time();
    for (const auto& e : world.scene().entities) {
        if (!e.has("animator") || !e.has("model")) continue;
        const Json& c = e.components.at("animator");
        if (!c.is_object() || !c.contains("started") || !c["started"].is_number() || c.contains("paused_at") || !c.contains("animation") ||
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
        if (!c.is_object() || !c.contains("animation") || !c["animation"].is_string()) continue;
        const auto id = asset::guid_field(root.components.at("model"), "asset");
        const Model* data = id ? model(*id) : nullptr;
        if (!data) continue;
        const std::string name = c["animation"].get<std::string>();
        const asset::ImportedAnimation* clip = asset::find_animation(data->model, name);
        if (!clip) {
            say_once(std::format("'{}' has no animation '{}'", root.name, name));
            continue;
        }
        const std::vector<Guid>& ids = bind(world, root);
        pose_.assign(data->rest.begin(), data->rest.end());
        asset::sample(*clip, animator_time(c, now, clip->duration), pose_);
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
}

} // namespace noor::game
