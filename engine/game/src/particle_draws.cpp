// Effect particles to the renderer (Milestone 8B, parts 2 and 4, ADR-035): particle_draws and SceneRenderer::particles.
#include "noor/game/particle_draws.h"

#include "noor/game/scene_renderer.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

namespace noor::game {
namespace {

using asset::Json;

std::string text(const Json& j, const char* key, const char* fallback) {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string(fallback);
}
float number(const Json& j, const char* key, float fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

} // namespace

std::span<const render::ParticleDraw> particle_draws(const fx::EmitterSet& set, math::Vec3 eye, const ParticleDrawSources& sources,
                                                     ParticleDrawStorage& storage) {
    storage.draws.clear();
    storage.culled = 0;
    std::vector<Guid> ids;
    ids.reserve(set.emitters().size());
    for (const auto& [id, emitter] : set.emitters())
        if (!emitter.particles().empty() && (!sources.shown || sources.shown(set.owner(id)))) ids.push_back(id);  // sub-emitters follow their entity
    std::ranges::sort(ids, [](const Guid& a, const Guid& b) { return a.to_string() < b.to_string(); });
    if (storage.vertices.size() < ids.size()) storage.vertices.resize(ids.size());

    for (std::size_t n = 0; n < ids.size(); ++n) {
        const fx::Emitter& emitter = set.emitters().at(ids[n]);
        const fx::EmitterSettings& s = emitter.settings();
        const Json& r = s.render;
        render::ParticleDraw d;
        const std::string blend = text(r, "blend", "alpha");
        d.blend = blend == "additive" ? render::ParticleBlend::Additive
                  : blend == "premultiplied" ? render::ParticleBlend::Premultiplied
                                             : render::ParticleBlend::Alpha;
        d.facing = text(r, "facing", "camera") == "velocity" ? render::ParticleFacing::Velocity : render::ParticleFacing::Camera;
        // Additive particles are light (fire, sparks): unlit unless told otherwise.
        d.lit = r.is_object() && r.contains("lit") && r["lit"].is_boolean() ? r["lit"].get<bool>() : d.blend != render::ParticleBlend::Additive;
        d.emission = std::max(0.0f, number(r, "emission", 0.0f));
        d.stretch = std::max(0.0f, number(r, "stretch", 0.0f));
        if (r.is_object() && r.contains("frames") && r["frames"].is_array() && r["frames"].size() == 2 && r["frames"][0].is_number() &&
            r["frames"][1].is_number()) {
            d.frames_x = static_cast<std::uint32_t>(std::clamp(r["frames"][0].get<int>(), 1, 64));
            d.frames_y = static_cast<std::uint32_t>(std::clamp(r["frames"][1].get<int>(), 1, 64));
        }
        if (const auto id = r.is_object() && r.contains("texture") ? asset::guid_field(r, "texture") : std::nullopt; id && sources.texture)
            d.texture = sources.texture(*id);
        const std::string frame_mode = text(r, "frame", "life");  // "life": the flipbook plays over a particle's life; "random": one frame each
        const std::uint32_t frames = d.frames_x * d.frames_y;

        // Local-space particles are drawn where their entity is now.
        const auto world_matrix = s.local_space && sources.world_of ? sources.world_of(ids[n]) : std::nullopt;
        auto& out = storage.vertices[n];
        out.clear();
        // "max_distance" (part 7): an effect whose particles are all further than this from the camera isn't drawn.
        // Drawing only: the simulation never depends on where the camera is, so replays stay the same.
        if (const float far = number(r, "max_distance", 0.0f); far > 0.0f) {
            math::Vec3 blo{1e30f, 1e30f, 1e30f}, bhi{-1e30f, -1e30f, -1e30f};
            for (const fx::Particle& p : emitter.particles()) {
                const math::Vec3 q = world_matrix ? world_matrix->transform_point(p.position) : p.position;
                blo = {std::min(blo.x, q.x), std::min(blo.y, q.y), std::min(blo.z, q.z)};
                bhi = {std::max(bhi.x, q.x), std::max(bhi.y, q.y), std::max(bhi.z, q.z)};
            }
            const math::Vec3 nearest{std::clamp(eye.x, blo.x, bhi.x), std::clamp(eye.y, blo.y, bhi.y), std::clamp(eye.z, blo.z, bhi.z)};
            if (math::length(nearest - eye) > far) {
                ++storage.culled;
                continue;
            }
        }
        out.reserve(emitter.particles().size());
        math::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        float biggest = 0.0f, fastest = 0.0f;
        for (const fx::Particle& p : emitter.particles()) {
            render::ParticleVertex v;
            v.position = world_matrix ? world_matrix->transform_point(p.position) : p.position;
            v.velocity = world_matrix ? math::transform_vector(*world_matrix, p.velocity) : p.velocity;
            v.size = std::max(0.0f, emitter.size_of(p));
            const math::Vec3 c = emitter.color_of(p);
            v.color = {c.x, c.y, c.z, std::clamp(emitter.alpha_of(p), 0.0f, 1.0f)};
            const float life = std::clamp(p.age / p.lifetime, 0.0f, 0.9999f);
            v.frame = frames <= 1 ? 0.0f
                      : frame_mode == "random" ? static_cast<float>(p.random % frames)
                                               : std::floor(life * static_cast<float>(frames));
            out.push_back(v);
            lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
            hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
            biggest = std::max(biggest, v.size);
            fastest = std::max(fastest, math::length(v.velocity));
        }
        if (out.empty()) continue;
        if (d.blend != render::ParticleBlend::Additive) {  // back to front (ties: in order, so a frame never wobbles)
            std::vector<std::uint32_t> order(out.size());
            std::iota(order.begin(), order.end(), 0u);
            std::vector<float> dist(out.size());
            for (std::size_t i = 0; i < out.size(); ++i) {
                const math::Vec3 e = out[i].position - eye;
                dist[i] = math::dot(e, e);
            }
            std::ranges::stable_sort(order, [&](std::uint32_t a, std::uint32_t b) { return dist[a] > dist[b]; });
            std::vector<render::ParticleVertex> sorted;
            sorted.reserve(out.size());
            for (std::uint32_t i : order) sorted.push_back(out[i]);
            out = std::move(sorted);
        }
        d.center = (lo + hi) * 0.5f;
        d.radius = math::length(hi - lo) * 0.5f + biggest + fastest * d.stretch;  // a stretched quad reaches its motion
        d.particles = out;
        storage.draws.push_back(d);
    }
    return storage.draws;
}

std::span<const render::ParticleDraw> SceneRenderer::particles(const fx::ParticleSystem& system, runtime::World& world, math::Vec3 eye) {
    ParticleDrawSources sources;
    sources.world_of = [&world](const Guid& id) { return runtime::api::world_matrix(world, id); };
    sources.texture = [this](const Guid& id) -> render::TextureHandle {
        if (!resolve_) return {};
        const auto file = resolve_(id);
        return file ? textures_.get(*file) : render::TextureHandle{};
    };
    return particle_draws(system.set(), eye, sources, particle_storage_);
}

} // namespace noor::game
