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

// A stable salt from an entity's id (its text: the same on every machine and build).
std::uint64_t salt_of(const Guid& id) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : id.to_string()) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ull;
    }
    return h;
}

float number(const Json& j, const char* key, float fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

} // namespace

ParticleSystem::ParticleSystem(Options options) : options_(std::move(options)) {}

Json ParticleSystem::resolved(const Json& component) const {
    if (component.is_object() && component.contains("asset") && options_.resolve_asset)
        if (const auto id = asset::guid_field(component, "asset"))
            if (const auto path = options_.resolve_asset(*id))
                if (auto file = load_fx_file(*path)) return merge_emitter((*file)["emitter"], component);
    return merge_emitter(Json::object(), component);
}

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
    const math::Vec3 air = wind(world);
    std::unordered_set<Guid> seen;
    for (const auto& e : world.scene().entities) {
        if (!e.has("emitter") || !e.components.at("emitter").is_object()) continue;
        const Json& component = e.components.at("emitter");
        seen.insert(e.id);
        auto it = emitters_.find(e.id);
        if (it == emitters_.end()) {
            it = emitters_.emplace(e.id, Emitter(EmitterSettings::parse(resolved(component)), salt_of(e.id))).first;
            components_[e.id] = component;
        } else if (components_[e.id] != component) {  // changed while running
            it->second.set_settings(EmitterSettings::parse(resolved(component)));
            components_[e.id] = component;
        }
        const auto matrix = runtime::api::world_matrix(world, e.id);
        it->second.step(static_cast<float>(fixed_dt), matrix ? *matrix : math::Mat4::identity(), air);
    }
    for (auto it = emitters_.begin(); it != emitters_.end();) {  // the component was removed
        if (seen.contains(it->first)) {
            ++it;
        } else {
            components_.erase(it->first);
            it = emitters_.erase(it);
        }
    }
    step_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ParticleSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) {
    emitters_.erase(entity.id);
    components_.erase(entity.id);
}

void ParticleSystem::shutdown(runtime::World&) {
    emitters_.clear();
    components_.clear();
}

Emitter* ParticleSystem::find(const Guid& entity) {
    const auto it = emitters_.find(entity);
    return it == emitters_.end() ? nullptr : &it->second;
}

ParticleSystem::Stats ParticleSystem::stats() const {
    Stats s;
    s.emitters = emitters_.size();
    s.step_ms = step_ms_;
    std::vector<Guid> ids;
    ids.reserve(emitters_.size());
    for (const auto& [id, emitter] : emitters_) {
        ids.push_back(id);
        s.particles += emitter.particles().size();
    }
    std::ranges::sort(ids, [](const Guid& a, const Guid& b) { return a.to_string() < b.to_string(); });
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const Guid& id : ids) h = fingerprint(emitters_.at(id).particles(), h ^ salt_of(id));
    s.fingerprint = h;
    return s;
}

} // namespace noor::fx
