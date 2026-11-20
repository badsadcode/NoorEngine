#include "noor/game/water.h"

#include "noor/asset/registry.h"
#include "noor/fx/particles.h"
#include "noor/game/game.h"
#include "noor/runtime/script_api.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <numbers>
#include <string>

namespace noor::game {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct Wave {
    math::Vec2 dir;
    float amplitude, k, steepness;
};

// The four waves of a set (the same derivation as water.hlsl's waves()).
std::array<Wave, kWaveCount> waves_of(const Waves& w) noexcept {
    std::array<Wave, kWaveCount> out{};
    for (int i = 0; i < kWaveCount; ++i) {
        const float angle = w.direction + kWaveTurn[i];
        const float a = 0.5f * w.height * kWaveAmplitude[i];
        const float k = static_cast<float>(kTwoPi) / (w.length * kWaveLength[i]);
        out[i] = {{fx::det_sin(angle), -fx::det_cos(angle)}, a, k, a > 0.0f ? w.choppiness / (k * a * kWaveCount) : 0.0f};
    }
    return out;
}

} // namespace

float shoal(float kh, float* slope) noexcept {
    if (slope) *slope = 0.0f;
    if (kh <= 0.0f) return 0.0f;
    if (kh >= 3.0f) return 1.0f;
    const float x2 = kh * kh, num = kh * (27.0f + x2), den = 27.0f + 9.0f * x2;
    if (slope) *slope = ((27.0f + 3.0f * x2) * den - num * 18.0f * kh) / (den * den);
    return num / den;
}

float depth_at(const render::WaterDepthMap* map, math::Vec2 p, math::Vec2* gradient) noexcept {
    if (gradient) *gradient = {0.0f, 0.0f};
    if (!map || map->nx < 2 || map->nz < 2) return kDeepWater;
    const float ux = (p.x - map->origin.x) / map->cell, uz = (p.y - map->origin.y) / map->cell;
    const float lx = static_cast<float>(map->nx) - 1.0f, lz = static_cast<float>(map->nz) - 1.0f;
    if (ux < 0.0f || uz < 0.0f || ux > lx || uz > lz) return kDeepWater;
    const float ix = std::min(std::floor(ux), lx - 1.0f), iz = std::min(std::floor(uz), lz - 1.0f);
    const float fx = ux - ix, fz = uz - iz;
    const std::size_t i = static_cast<std::size_t>(iz) * map->nx + static_cast<std::size_t>(ix);
    const float d00 = map->depth[i], d10 = map->depth[i + 1], d01 = map->depth[i + map->nx], d11 = map->depth[i + map->nx + 1];
    const float a = d00 + (d10 - d00) * fx, b = d01 + (d11 - d01) * fx;
    if (gradient) *gradient = {((d10 - d00) + ((d11 - d01) - (d10 - d00)) * fz) / map->cell, (b - a) / map->cell};
    return a + (b - a) * fz;
}

std::array<float, kWaveCount> Waves::phases(double time) const noexcept {
    std::array<float, kWaveCount> out{};
    for (int i = 0; i < kWaveCount; ++i) {
        const double k = kTwoPi / (static_cast<double>(length) * kWaveLength[i]);
        const double omega = std::sqrt(static_cast<double>(kGravity) * k);
        out[i] = static_cast<float>(std::fmod(omega * time, kTwoPi));
    }
    return out;
}

math::Vec3 Waves::offset(math::Vec2 rest, const std::array<float, kWaveCount>& phases, const render::WaterDepthMap* depth_map) const noexcept {
    if (flat()) return {0, 0, 0};
    math::Vec3 out{0, 0, 0};
    const auto waves = waves_of(*this);
    const float depth = depth_at(depth_map, rest);
    for (int i = 0; i < kWaveCount; ++i) {
        const Wave& w = waves[i];
        const float amp = w.amplitude * shoal(w.k * depth);
        const float theta = w.k * (w.dir.x * rest.x + w.dir.y * rest.y) - phases[i];
        const float c = fx::det_cos(theta), s = fx::det_sin(theta);
        out.x += w.steepness * amp * w.dir.x * c;
        out.y += amp * s;
        out.z += w.steepness * amp * w.dir.y * c;
    }
    return out;
}

float Waves::height_at(math::Vec2 xz, const std::array<float, kWaveCount>& phases, math::Vec3* normal,
                       const render::WaterDepthMap* depth_map) const noexcept {
    if (flat()) {
        if (normal) *normal = {0, 1, 0};
        return 0.0f;
    }
    // Which point at rest ends up at xz: step back from the sideways motion until it settles (a contraction while the
    // waves' total steepness stays under 1).
    math::Vec2 rest = xz;
    for (int step = 0; step < 12; ++step) {
        const math::Vec3 o = offset(rest, phases, depth_map);
        rest = {xz.x - o.x, xz.y - o.z};
    }
    const math::Vec3 o = offset(rest, phases, depth_map);
    if (normal) {
        // The surface's two tangents at that point (d/dx and d/dz of where the waves move it), crossed: its exact normal.
        // (GPU Gems' shorter formula drops the waves' cross terms: centimetres a metre off on choppy crests.) Where the
        // bottom shelves, each wave's height changes along the way too (amp_grad).
        math::Vec3 tx{1, 0, 0}, tz{0, 0, 1};
        const auto waves = waves_of(*this);
        math::Vec2 dgrad;
        const float depth = depth_at(depth_map, rest, &dgrad);
        for (int i = 0; i < kWaveCount; ++i) {
            const Wave& w = waves[i];
            float slope = 0.0f;
            const float amp = w.amplitude * shoal(w.k * depth, &slope);
            const math::Vec2 amp_grad{w.amplitude * slope * w.k * dgrad.x, w.amplitude * slope * w.k * dgrad.y};
            const float theta = w.k * (w.dir.x * rest.x + w.dir.y * rest.y) - phases[i];
            const float c = fx::det_cos(theta), s = fx::det_sin(theta);
            const float q = w.steepness;
            const float sx = amp_grad.x * c - amp * w.k * w.dir.x * s, sz = amp_grad.y * c - amp * w.k * w.dir.y * s;
            tx.x += q * w.dir.x * sx;
            tx.y += amp_grad.x * s + amp * w.k * w.dir.x * c;
            tx.z += q * w.dir.y * sx;
            tz.x += q * w.dir.x * sz;
            tz.y += amp_grad.y * s + amp * w.k * w.dir.y * c;
            tz.z += q * w.dir.y * sz;
        }
        *normal = math::normalize(math::cross(tz, tx));
    }
    return o.y;
}

Waves make_waves(const asset::WaterLook& look, float wind_speed) {
    Waves w;
    const float u = std::max(0.0f, wind_speed);
    w.height = std::max(0.0f, look.wave_height) + look.wind * 0.21f * u * u / kGravity;  // a fully developed sea's height
    w.length = std::max(0.5f, look.wave_length);
    w.direction = math::radians(look.wave_direction);
    w.choppiness = std::clamp(look.choppiness, 0.0f, 0.9f);
    return w;
}

bool WaterBody::covers(math::Vec2 xz) const noexcept {
    if (endless) return true;
    const float dx = xz.x - center.x, dz = xz.y - center.z;
    const float c = std::cos(yaw), s = std::sin(yaw);
    // The footprint's own axes: x = (cos yaw, -sin yaw), z = (sin yaw, cos yaw) (yaw = atan2(-x.z, x.x)).
    const float u = dx * c - dz * s, v = dx * s + dz * c;
    return std::abs(u) <= half_size.x && std::abs(v) <= half_size.y;
}

asset::WaterLook water_look(const asset::Json& wc, const AssetPath& asset_path) {
    asset::WaterLook look;
    if (wc.contains("preset") && wc["preset"].is_string())
        if (const asset::Json* preset = asset::water_preset(wc["preset"].get<std::string>())) look.apply((*preset)["water"]);
    if (const auto id = asset::guid_field(wc, "asset"); id && asset_path)
        if (const auto file = asset_path(*id))
            if (auto loaded = asset::load_water_file(*file)) look.apply((*loaded)["water"]);
    look.apply(wc);
    return look;
}

std::optional<WaterSurface> water_surface(const std::vector<WaterBody>& bodies, math::Vec2 xz, double time) {
    std::optional<WaterSurface> best;
    for (const WaterBody& b : bodies) {
        if (!b.covers(xz)) continue;
        WaterSurface s;
        s.height = b.center.y + b.waves.height_at(xz, b.waves.phases(time), &s.normal, b.depth.get());
        s.entity = b.entity;
        if (!best || s.height > best->height) best = s;
    }
    return best;
}

void set_water_time(std::span<render::WaterDraw> draws, double time) {
    for (auto& d : draws) {
        d.time = time;
        Waves w;
        w.height = d.wave_height;
        w.length = d.wave_length;
        d.wave_phase = w.phases(time);
    }
}

void WaterSystem::attached(runtime::World& world) {
    world.set_water_query([this](const runtime::World& w, float x, float z) -> std::optional<runtime::World::WaterSample> {
        const auto s = water_surface(bodies_, {x, z}, w.time());
        if (!s) return std::nullopt;
        return runtime::World::WaterSample{s->height, s->normal, s->entity};
    });
    refresh(world);
}

void WaterSystem::update(runtime::World& world, double) { refresh(world); }

void WaterSystem::refresh(runtime::World& world) {
    bodies_.clear();
    bool any = false;
    for (const auto& e : world.scene().entities)
        if (e.has("water") && e.components.at("water").is_object()) any = true;
    if (!any) return;
    const float wind = math::length(effect_wind(world, asset_path_));
    const std::vector<ParticleGround::Piece> ground = ground_ ? ground_(world) : std::vector<ParticleGround::Piece>{};
    for (const auto& e : world.scene().entities) {
        if (!e.has("water") || !e.components.at("water").is_object()) continue;
        const auto m = runtime::api::world_matrix(world, e.id);
        WaterBody b = water_body(e.components.at("water"), m ? *m : math::Mat4::identity(), make_waves(water_look(e.components.at("water"), asset_path_), wind));
        b.entity = e.id;
        b.depth = water_depth_map(b.endless, b.center, b.half_size, b.yaw, ground);
        bodies_.push_back(std::move(b));
    }
}

namespace {

// The highest terrain at world (x, z) (as ParticleGround::at, without the normal).
std::optional<float> ground_height(std::span<const ParticleGround::Piece> ground, float x, float z) {
    std::optional<float> best;
    for (const auto& p : ground) {
        if (!p.heights) continue;
        const terrain::Heights& h = *p.heights;
        const float lx = x - p.position.x, lz = z - p.position.z;
        const math::Vec2 o = h.origin();
        if (lx < o.x || lz < o.y || lx > -o.x || lz > -o.y) continue;
        const float y = p.position.y + h.height(lx, lz);
        if (!best || y > *best) best = y;
    }
    return best;
}

struct DepthCacheEntry {
    std::string key;
    std::vector<std::shared_ptr<const terrain::Heights>> heights;  // kept alive: the key has their addresses
    std::shared_ptr<const render::WaterDepthMap> map;
};

} // namespace

std::shared_ptr<const render::WaterDepthMap> water_depth_map(bool endless, math::Vec3 center, math::Vec2 half_size, float yaw,
                                                             std::span<const ParticleGround::Piece> ground) {
    if (ground.empty()) return nullptr;
    // The region: a box's footprint (its bounds in x and z), a sea's terrains.
    float x0 = 0, x1 = 0, z0 = 0, z1 = 0;
    bool any = false;
    const auto take = [&](float ax, float az, float bx, float bz) {
        if (!any) {
            x0 = ax, z0 = az, x1 = bx, z1 = bz;
            any = true;
        } else {
            x0 = std::min(x0, ax), z0 = std::min(z0, az), x1 = std::max(x1, bx), z1 = std::max(z1, bz);
        }
    };
    if (!endless) {
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float ex = std::abs(c) * half_size.x + std::abs(s) * half_size.y, ez = std::abs(s) * half_size.x + std::abs(c) * half_size.y;
        take(center.x - ex, center.z - ez, center.x + ex, center.z + ez);
    }
    std::string key = std::format("{} {} {} {} {} {} {}", endless, center.x, center.y, center.z, half_size.x, half_size.y, yaw);
    bool under = false;
    for (const auto& p : ground) {
        if (!p.heights) continue;
        const math::Vec2 o = p.heights->origin();
        const float ax = p.position.x + o.x, az = p.position.z + o.y, bx = p.position.x - o.x, bz = p.position.z - o.y;
        if (endless) take(ax, az, bx, bz);
        else if (bx < x0 || ax > x1 || bz < z0 || az > z1) continue;
        under = true;
        key += std::format(" {} {} {} {}", static_cast<const void*>(p.heights.get()), p.position.x, p.position.y, p.position.z);
    }
    if (!under || !any || x1 <= x0 || z1 <= z0) return nullptr;

    static std::mutex mutex;
    static std::vector<DepthCacheEntry> cache;  // most recent last
    {
        const std::lock_guard lock(mutex);
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->key == key) {
                auto map = it->map;
                std::rotate(it, it + 1, cache.end());
                return map;
            }
    }
    auto map = std::make_shared<render::WaterDepthMap>();
    const float span = std::max(x1 - x0, z1 - z0);
    map->cell = std::max(0.25f, span / 255.0f);
    map->nx = std::min(256u, static_cast<std::uint32_t>(std::ceil((x1 - x0) / map->cell)) + 1u);
    map->nz = std::min(256u, static_cast<std::uint32_t>(std::ceil((z1 - z0) / map->cell)) + 1u);
    map->origin = {x0, z0};
    map->depth.resize(std::size_t{map->nx} * map->nz);
    for (std::uint32_t z = 0; z < map->nz; ++z)
        for (std::uint32_t x = 0; x < map->nx; ++x) {
            const float px = x0 + static_cast<float>(x) * map->cell, pz = z0 + static_cast<float>(z) * map->cell;
            const auto g = ground_height(ground, px, pz);
            map->depth[std::size_t{z} * map->nx + x] = g ? std::clamp(center.y - *g, -100.0f, kDeepWater) : kDeepWater;
        }
    DepthCacheEntry entry{std::move(key), {}, map};
    for (const auto& p : ground) entry.heights.push_back(p.heights);
    const std::lock_guard lock(mutex);
    cache.push_back(std::move(entry));
    if (cache.size() > 16) cache.erase(cache.begin());
    return map;
}

void set_water_depths(std::span<render::WaterDraw> draws, std::span<const ParticleGround::Piece> ground) {
    for (auto& d : draws) d.depth_map = water_depth_map(d.endless, d.center, d.half_size, d.yaw, ground);
}

WaterBody water_body(const asset::Json& wc, const math::Mat4& world, const Waves& waves) {
    const math::Vec3 ax = math::transform_vector(world, {1, 0, 0}), az = math::transform_vector(world, {0, 0, 1});
    math::Vec2 size{10.0f, 10.0f};
    if (wc.contains("size") && wc["size"].is_array() && wc["size"].size() == 2 && wc["size"][0].is_number() && wc["size"][1].is_number())
        size = {std::max(0.01f, wc["size"][0].get<float>()), std::max(0.01f, wc["size"][1].get<float>())};
    WaterBody b;
    b.center = world.transform_point({0, 0, 0});
    b.half_size = {size.x * 0.5f * math::length(ax), size.y * 0.5f * math::length(az)};
    b.yaw = std::atan2(-ax.z, ax.x);
    b.endless = wc.contains("shape") && wc["shape"].is_string() && wc["shape"].get<std::string>() == "sea";
    b.waves = waves;
    return b;
}

render::WaterDraw water_draw(const WaterBody& b, const asset::WaterLook& look) {
    render::WaterDraw d;
    d.center = b.center;
    d.half_size = b.half_size;
    d.yaw = b.yaw;
    d.endless = b.endless;
    d.absorption = look.absorption;
    d.scatter = look.scatter;
    d.refraction = look.refraction;
    d.shore_fade = look.shore_fade;
    d.roughness = look.roughness;
    d.ripple = look.ripple;
    d.ripple_scale = look.ripple_scale;
    d.wave_height = b.waves.height;
    d.wave_length = b.waves.length;
    d.wave_direction = b.waves.direction;
    d.choppiness = b.waves.choppiness;
    d.depth_map = b.depth;
    d.foam = look.foam;
    d.foam_depth = look.foam_depth;
    d.crest_foam = look.crest_foam;
    d.wet = look.wet;
    d.wet_height = look.wet_height;
    d.reflections = look.reflections;
    d.caustics = look.caustics;
    d.shafts = look.shafts;
    return d;
}

} // namespace noor::game
