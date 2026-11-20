#include "noor/game/water.h"

#include "noor/asset/registry.h"
#include "noor/fx/particles.h"
#include "noor/game/game.h"
#include "noor/runtime/script_api.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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

std::array<float, kWaveCount> Waves::phases(double time) const noexcept {
    std::array<float, kWaveCount> out{};
    for (int i = 0; i < kWaveCount; ++i) {
        const double k = kTwoPi / (static_cast<double>(length) * kWaveLength[i]);
        const double omega = std::sqrt(static_cast<double>(kGravity) * k);
        out[i] = static_cast<float>(std::fmod(omega * time, kTwoPi));
    }
    return out;
}

math::Vec3 Waves::offset(math::Vec2 rest, const std::array<float, kWaveCount>& phases) const noexcept {
    if (flat()) return {0, 0, 0};
    math::Vec3 out{0, 0, 0};
    const auto waves = waves_of(*this);
    for (int i = 0; i < kWaveCount; ++i) {
        const Wave& w = waves[i];
        const float theta = w.k * (w.dir.x * rest.x + w.dir.y * rest.y) - phases[i];
        const float c = fx::det_cos(theta), s = fx::det_sin(theta);
        out.x += w.steepness * w.amplitude * w.dir.x * c;
        out.y += w.amplitude * s;
        out.z += w.steepness * w.amplitude * w.dir.y * c;
    }
    return out;
}

float Waves::height_at(math::Vec2 xz, const std::array<float, kWaveCount>& phases, math::Vec3* normal) const noexcept {
    if (flat()) {
        if (normal) *normal = {0, 1, 0};
        return 0.0f;
    }
    // Which point at rest ends up at xz: step back from the sideways motion until it settles (a contraction while the
    // waves' total steepness stays under 1).
    math::Vec2 rest = xz;
    for (int step = 0; step < 12; ++step) {
        const math::Vec3 o = offset(rest, phases);
        rest = {xz.x - o.x, xz.y - o.z};
    }
    const math::Vec3 o = offset(rest, phases);
    if (normal) {
        // The surface's two tangents at that point (d/dx and d/dz of where the waves move it), crossed: its exact normal.
        // (GPU Gems' shorter formula drops the waves' cross terms: centimetres a metre off on choppy crests.)
        math::Vec3 tx{1, 0, 0}, tz{0, 0, 1};
        const auto waves = waves_of(*this);
        for (int i = 0; i < kWaveCount; ++i) {
            const Wave& w = waves[i];
            const float theta = w.k * (w.dir.x * rest.x + w.dir.y * rest.y) - phases[i];
            const float wa = w.k * w.amplitude;
            const float c = fx::det_cos(theta), s = fx::det_sin(theta);
            const float qs = w.steepness * wa * s;
            tx.x -= qs * w.dir.x * w.dir.x;
            tx.y += wa * w.dir.x * c;
            tx.z -= qs * w.dir.x * w.dir.y;
            tz.x -= qs * w.dir.x * w.dir.y;
            tz.y += wa * w.dir.y * c;
            tz.z -= qs * w.dir.y * w.dir.y;
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
        s.height = b.center.y + b.waves.height_at(xz, b.waves.phases(time), &s.normal);
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
    for (const auto& e : world.scene().entities) {
        if (!e.has("water") || !e.components.at("water").is_object()) continue;
        const auto m = runtime::api::world_matrix(world, e.id);
        bodies_.push_back(water_body(e.components.at("water"), m ? *m : math::Mat4::identity(), make_waves(water_look(e.components.at("water"), asset_path_), wind)));
        bodies_.back().entity = e.id;
    }
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
    return d;
}

} // namespace noor::game
