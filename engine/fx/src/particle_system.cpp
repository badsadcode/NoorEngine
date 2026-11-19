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
    if (component.is_object() && component.contains("asset") && resolve)
        if (const auto id = asset::guid_field(component, "asset"))
            if (const auto path = resolve(*id))
                if (auto file = load_fx_file(*path)) return merge_emitter((*file)["emitter"], component);
    return merge_emitter(Json::object(), component);
}

// ---- EmitterSet ----

void EmitterSet::step(std::span<const Input> inputs, float dt, math::Vec3 wind) {
    std::unordered_set<Guid> seen;
    for (const Input& in : inputs) {
        if (!in.settings) continue;
        seen.insert(in.entity);
        auto it = emitters_.find(in.entity);
        if (it == emitters_.end()) {
            it = emitters_.emplace(in.entity, Emitter(EmitterSettings::parse(*in.settings), emitter_salt(in.entity))).first;
            settings_[in.entity] = *in.settings;
        } else if (settings_[in.entity] != *in.settings) {  // changed while running
            it->second.set_settings(EmitterSettings::parse(*in.settings));
            settings_[in.entity] = *in.settings;
        }
        it->second.step(dt, in.world, wind);
    }
    for (auto it = emitters_.begin(); it != emitters_.end();) {  // the entity or its component went
        if (seen.contains(it->first)) {
            ++it;
        } else {
            settings_.erase(it->first);
            it = emitters_.erase(it);
        }
    }
}

void EmitterSet::restart(const Guid& entity) {
    emitters_.erase(entity);
    settings_.erase(entity);
}

void EmitterSet::clear() {
    emitters_.clear();
    settings_.clear();
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
    set_.step(inputs, static_cast<float>(fixed_dt), wind(world));
    step_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ParticleSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) {
    set_.restart(entity.id);
    resolved_.erase(entity.id);
}

void ParticleSystem::shutdown(runtime::World&) {
    set_.clear();
    resolved_.clear();
}

ParticleSystem::Stats ParticleSystem::stats() const {
    const auto s = set_.stats();
    return {s.emitters, s.particles, s.fingerprint, step_ms_};
}

} // namespace noor::fx
