#include "noor/fx/particle_system.h"

#include "noor/asset/environment_asset.h"
#include "noor/fx/fx_asset.h"
#include "noor/runtime/script_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace noor::fx {
namespace {

using asset::Json;

float number(const Json& j, const char* key, float fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

} // namespace

std::uint64_t emitter_salt(const Guid& id) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : id.to_string()) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ull;
    }
    return h;
}

Json resolve_emitter(const Json& component, const std::function<std::optional<std::filesystem::path>(const Guid&)>& resolve) {
    Json out = merge_emitter(Json::object(), component);
    if (component.is_object() && component.contains("asset") && resolve)
        if (const auto id = asset::guid_field(component, "asset"))
            if (const auto path = resolve(*id))
                if (auto file = load_fx_file(*path)) out = merge_emitter((*file)["emitter"], component);
    // Sub-emitters may name effect assets too (part 7): {"emitter": {"asset": id, ...overrides}}. One level deep.
    if (out.is_object() && out.contains("sub_emitters") && out["sub_emitters"].is_array())
        for (auto& sub : out["sub_emitters"])
            if (sub.is_object() && sub.contains("emitter") && sub["emitter"].is_object() && sub["emitter"].contains("asset")) {
                Json inner = resolve_emitter(sub["emitter"], resolve);
                inner.erase("sub_emitters");
                sub["emitter"] = std::move(inner);
            }
    return out;
}

Guid sub_emitter_id(const Guid& parent, std::size_t index) {
    Guid g = parent;
    g.bytes[15] ^= static_cast<std::uint8_t>(0xA5u + index);
    g.bytes[7] ^= 0x5Au;
    return g;
}

// ---- EmitterSet ----

Emitter& EmitterSet::ensure(const Guid& id, const Json& settings, bool sub) {
    const auto parsed = [&] {
        EmitterSettings s = EmitterSettings::parse(settings);
        if (sub) {  // a sub-emitter: world space, bursts only (unless it says otherwise), no sub-emitters of its own
            s.local_space = false;
            if (!settings.contains("rate")) s.rate = 0.0f;
            s.sub_emitters.clear();
        }
        return s;
    };
    auto it = emitters_.find(id);
    if (it == emitters_.end()) {
        it = emitters_.emplace(id, Emitter(parsed(), emitter_salt(id))).first;
        settings_[id] = settings;
    } else if (settings_[id] != settings) {  // changed while running
        it->second.set_settings(parsed());
        settings_[id] = settings;
    }
    return it->second;
}

void EmitterSet::step(std::span<const Input> inputs, float dt, math::Vec3 wind, const GroundQuery* ground, std::uint32_t budget) {
    // The budget left: the set's particles now (they live on), then what each spawn takes.
    std::uint64_t alive = 0;
    for (const auto& [id, e] : emitters_) alive += e.particles().size();
    std::uint32_t left = alive >= budget ? 0u : static_cast<std::uint32_t>(budget - alive);
    std::unordered_set<Guid> seen;
    for (const Input& in : inputs) {
        if (!in.settings) continue;
        seen.insert(in.entity);
        Emitter& parent = ensure(in.entity, *in.settings, false);
        const std::size_t before = parent.particles().size();
        parent.step(dt, in.world, wind, ground, &left);
        // Particles that died this step give their room back (the budget counts the living).
        if (parent.particles().size() < before) left += static_cast<std::uint32_t>(before - parent.particles().size());
        // Sub-emitters: step, then burst where this step's hits and deaths happened.
        const auto& subs = parent.settings().sub_emitters;
        auto& kids = children_[in.entity];
        kids.clear();
        for (std::size_t i = 0; i < subs.size(); ++i) {
            const Guid id = sub_emitter_id(in.entity, i);
            kids.push_back(id);
            seen.insert(id);
            Emitter& child = ensure(id, subs[i].emitter, true);
            const std::size_t had = child.particles().size();
            child.step(dt, math::Mat4::identity(), wind, ground, &left);
            if (child.particles().size() < had) left += static_cast<std::uint32_t>(had - child.particles().size());
            for (const Emitter::Event& ev : parent.events())
                if (ev.on == subs[i].on) child.burst(subs[i].count, math::translation(ev.position), ev.velocity * subs[i].inherit_velocity, &left);
        }
    }
    for (auto it = emitters_.begin(); it != emitters_.end();) {  // the entity or its component went
        if (seen.contains(it->first)) {
            ++it;
        } else {
            settings_.erase(it->first);
            it = emitters_.erase(it);
        }
    }
    std::erase_if(children_, [&](const auto& kv) { return !seen.contains(kv.first); });
}

void EmitterSet::restart(const Guid& entity) {
    emitters_.erase(entity);
    settings_.erase(entity);
    if (const auto it = children_.find(entity); it != children_.end()) {
        for (const Guid& id : it->second) {
            emitters_.erase(id);
            settings_.erase(id);
        }
        children_.erase(it);
    }
}

void EmitterSet::clear() {
    emitters_.clear();
    settings_.clear();
    children_.clear();
}

Guid EmitterSet::owner(const Guid& id) const {
    for (const auto& [parent, kids] : children_)
        if (std::ranges::find(kids, id) != kids.end()) return parent;
    return id;
}

Emitter* EmitterSet::find(const Guid& entity) {
    const auto it = emitters_.find(entity);
    return it == emitters_.end() ? nullptr : &it->second;
}

EmitterSet::Stats EmitterSet::stats() const {
    Stats s;
    s.emitters = emitters_.size();
    std::vector<Guid> ids;
    ids.reserve(emitters_.size());
    for (const auto& [id, emitter] : emitters_) {
        ids.push_back(id);
        s.particles += emitter.particles().size();
        s.hits += emitter.hits();
        s.throttled += emitter.throttled();
    }
    std::ranges::sort(ids, [](const Guid& a, const Guid& b) { return a.to_string() < b.to_string(); });
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const Guid& id : ids) h = fingerprint(emitters_.at(id).particles(), h ^ emitter_salt(id));
    s.fingerprint = h;
    return s;
}

// ---- ParticleSystem ----

ParticleSystem::ParticleSystem(Options options) : options_(std::move(options)) {}

math::Vec3 ParticleSystem::wind(runtime::World& world) const {
    if (options_.wind) return options_.wind(world);
    for (const auto& e : world.scene().entities) {
        if (!e.has("environment") || !e.components.at("environment").is_object()) continue;
        Json env = e.components.at("environment");
        if (env.contains("asset") && options_.resolve_asset)
            if (const auto id = asset::guid_field(env, "asset"))
                if (const auto path = options_.resolve_asset(*id))
                    if (auto file = asset::load_environment_file(*path)) env = asset::merge_environment((*file)["environment"], env);
        const float speed = std::max(0.0f, number(env, "wind_speed", 0.0f));
        const float a = number(env, "wind_direction", 0.0f) * 3.14159265f / 180.0f;
        return {det_sin(a) * speed, 0.0f, -det_cos(a) * speed};
    }
    return {0, 0, 0};
}

void ParticleSystem::attached(runtime::World& world) {
    world.handle("emitter.burst", [this](runtime::World&, const Guid& entity, const Json& args) {
        const int count = args.is_object() && args.contains("count") && args["count"].is_number() ? args["count"].get<int>() : 0;
        if (count > 0) bursts_.emplace_back(entity, std::min(count, 100000));
    });
}

void ParticleSystem::fixed_update(runtime::World& world, double fixed_dt) {
    const auto start = std::chrono::steady_clock::now();
    if (timed_frame_ != world.frame_count()) {
        timed_frame_ = world.frame_count();
        step_ms_ = 0;
    }
    std::vector<EmitterSet::Input> inputs;
    std::unordered_set<Guid> seen;
    for (const auto& e : world.scene().entities) {
        if (!e.has("emitter") || !e.components.at("emitter").is_object()) continue;
        const Json& component = e.components.at("emitter");
        seen.insert(e.id);
        auto& r = resolved_[e.id];
        if (r.settings.is_null() || r.component != component) {  // resolve (read the asset) only when the component changed
            r.component = component;
            r.settings = resolve_emitter(component, options_.resolve_asset);
        }
        const auto matrix = runtime::api::world_matrix(world, e.id);
        inputs.push_back({e.id, &r.settings, matrix ? *matrix : math::Mat4::identity()});
    }
    std::erase_if(resolved_, [&](const auto& kv) { return !seen.contains(kv.first); });
    const GroundQuery ground = options_.ground ? options_.ground(world) : GroundQuery{};
    set_.step(inputs, static_cast<float>(fixed_dt), wind(world), ground ? &ground : nullptr, options_.max_particles);
    for (const auto& [entity, count] : bursts_)
        if (Emitter* e = set_.find(entity))
            if (const auto matrix = runtime::api::world_matrix(world, entity)) e->burst(count, *matrix);
    bursts_.clear();
    step_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ParticleSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) {
    set_.restart(entity.id);
    resolved_.erase(entity.id);
}

void ParticleSystem::shutdown(runtime::World&) {
    set_.clear();
    bursts_.clear();
    resolved_.clear();
}

ParticleSystem::Stats ParticleSystem::stats() const {
    const auto s = set_.stats();
    return {s.emitters, s.particles, s.fingerprint, step_ms_, s.hits, s.throttled};
}

} // namespace noor::fx
