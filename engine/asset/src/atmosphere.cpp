#include "noor/asset/atmosphere.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <mutex>
#include <thread>

namespace noor::asset {
namespace {

using math::Vec3;
constexpr float kPi = 3.14159265358979f;

Vec3 mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
Vec3 exp3(Vec3 v) { return {std::exp(v.x), std::exp(v.y), std::exp(v.z)}; }
Vec3 div_safe(Vec3 a, Vec3 b) {
    return {b.x > 1e-12f ? a.x / b.x : 0.0f, b.y > 1e-12f ? a.y / b.y : 0.0f, b.z > 1e-12f ? a.z / b.z : 0.0f};
}

// Distance along a ray from radius `r` (cosine `mu` with the local up) to a sphere of radius `radius`;
// negative if it doesn't reach it going forward. `nearest`: the first crossing (ground hits).
float ray_sphere(float r, float mu, float radius, bool nearest) {
    const float disc = r * r * (mu * mu - 1.0f) + radius * radius;
    if (disc < 0.0f) return -1.0f;
    const float s = std::sqrt(disc);
    const float t = nearest ? -r * mu - s : -r * mu + s;
    return t;
}

// Does a ray from radius `r` with cosine `mu` hit the planet (radius `bottom`)?
bool hits_ground(float r, float mu, float bottom) { return mu < 0.0f && r * r * (mu * mu - 1.0f) + bottom * bottom >= 0.0f; }

float phase_rayleigh(float c) { return 3.0f / (16.0f * kPi) * (1.0f + c * c); }
float phase_mie(float c, float g) {  // Cornette-Shanks
    const float g2 = g * g;
    const float k = 3.0f / (8.0f * kPi) * (1.0f - g2) / (2.0f + g2);
    return k * (1.0f + c * c) / std::pow(std::max(1e-6f, 1.0f + g2 - 2.0f * g * c), 1.5f);
}

// The transmittance table's parametrisation: more rows near the ground, more columns near the horizon.
float mu_of(float u) {
    const float x = 2.0f * u - 1.0f;
    return x * std::abs(x);
}
float u_of(float mu) {
    const float x = mu < 0.0f ? -std::sqrt(-mu) : std::sqrt(mu);
    return 0.5f * (x + 1.0f);
}

} // namespace

Vec3 solar_direction(float hours, float latitude_deg, float day_of_year) noexcept {
    constexpr float kDeg = kPi / 180.0f;
    const float declination = -23.44f * kDeg * std::cos(2.0f * kPi / 365.0f * (day_of_year + 10.0f));
    const float hour_angle = 15.0f * kDeg * (hours - 12.0f);  // negative in the morning (sun in the east)
    const float lat = std::clamp(latitude_deg, -90.0f, 90.0f) * kDeg;
    const float east = -std::cos(declination) * std::sin(hour_angle);
    const float north = std::cos(lat) * std::sin(declination) - std::sin(lat) * std::cos(declination) * std::cos(hour_angle);
    const float up = std::sin(lat) * std::sin(declination) + std::cos(lat) * std::cos(declination) * std::cos(hour_angle);
    return math::normalize(Vec3{east, up, -north});
}

std::string AtmosphereSettings::key() const {
    return std::format("{} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}", planet_radius_km, atmosphere_height_km, rayleigh_scattering.x,
                       rayleigh_scattering.y, rayleigh_scattering.z, rayleigh_height_km, mie_scattering, mie_absorption, mie_height_km, mie_g,
                       ozone_absorption.x, ozone_absorption.y, ozone_absorption.z, ozone_centre_km, ozone_half_width_km, ground_albedo.x,
                       ground_albedo.y, ground_albedo.z, turbidity, altitude_m);
}

Atmosphere::Medium Atmosphere::medium(float h) const {
    h = std::max(0.0f, h);
    const float rayleigh = std::exp(-h / s_.rayleigh_height_km);
    const float mie = std::exp(-h / s_.mie_height_km) * s_.turbidity;
    const float ozone = std::max(0.0f, 1.0f - std::abs(h - s_.ozone_centre_km) / s_.ozone_half_width_km);
    Medium m;
    m.scattering_rayleigh = s_.rayleigh_scattering * rayleigh;
    m.scattering_mie = Vec3{1, 1, 1} * (s_.mie_scattering * mie);
    m.extinction = m.scattering_rayleigh + Vec3{1, 1, 1} * ((s_.mie_scattering + s_.mie_absorption) * mie) + s_.ozone_absorption * ozone;
    return m;
}

Vec3 Atmosphere::transmittance_exact(float h, float mu) const {
    const float r = bottom_ + std::clamp(h, 0.0f, s_.atmosphere_height_km);
    if (hits_ground(r, mu, bottom_)) return {0, 0, 0};
    const float length = std::max(0.0f, ray_sphere(r, mu, top_, false));
    constexpr int kSteps = 64;
    const float dt = length / kSteps;
    Vec3 depth{0, 0, 0};
    for (int i = 0; i < kSteps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) * dt;
        const float ri = std::sqrt(r * r + t * t + 2.0f * r * mu * t);
        depth = depth + medium(ri - bottom_).extinction * dt;
    }
    return exp3(depth * -1.0f);
}

Vec3 Atmosphere::transmittance(float h, float mu) const {
    const float r = bottom_ + std::clamp(h, 0.0f, s_.atmosphere_height_km);
    if (hits_ground(r, mu, bottom_)) return {0, 0, 0};
    const float u = std::clamp(u_of(std::clamp(mu, -1.0f, 1.0f)), 0.0f, 1.0f) * (kTW - 1);
    const float v = std::sqrt(std::clamp(h / s_.atmosphere_height_km, 0.0f, 1.0f)) * (kTH - 1);
    const auto x0 = std::min(static_cast<std::uint32_t>(u), kTW - 2), y0 = std::min(static_cast<std::uint32_t>(v), kTH - 2);
    const float fx = u - static_cast<float>(x0), fy = v - static_cast<float>(y0);
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return transmittance_[static_cast<std::size_t>(y) * kTW + x]; };
    const Vec3 a = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
    const Vec3 b = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
    return a * (1 - fy) + b * fy;
}

Vec3 Atmosphere::multiple_scattering(float h, float mu_s) const {
    const float u = std::clamp(0.5f * (mu_s + 1.0f), 0.0f, 1.0f) * (kMS - 1);
    const float v = std::clamp(h / s_.atmosphere_height_km, 0.0f, 1.0f) * (kMS - 1);
    const auto x0 = std::min(static_cast<std::uint32_t>(u), kMS - 2), y0 = std::min(static_cast<std::uint32_t>(v), kMS - 2);
    const float fx = u - static_cast<float>(x0), fy = v - static_cast<float>(y0);
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return multiple_[static_cast<std::size_t>(y) * kMS + x]; };
    const Vec3 a = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
    const Vec3 b = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
    return a * (1 - fy) + b * fy;
}

Atmosphere::Atmosphere(const AtmosphereSettings& settings) : s_(settings) {
    s_.turbidity = std::clamp(s_.turbidity, 0.0f, 50.0f);
    bottom_ = s_.planet_radius_km;
    top_ = s_.planet_radius_km + s_.atmosphere_height_km;

    // 1. Transmittance table (exact marches).
    transmittance_.resize(static_cast<std::size_t>(kTW) * kTH);
    for (std::uint32_t y = 0; y < kTH; ++y) {
        const float fy = static_cast<float>(y) / (kTH - 1);
        const float h = fy * fy * s_.atmosphere_height_km;
        // Below the horizon the ray hits the ground (transmittance() answers 0 before reading the table);
        // those texels hold the horizon's value so bilinear reads just above the horizon stay right.
        const float r = bottom_ + h;
        const float mu_horizon = -std::sqrt(std::max(0.0f, 1.0f - (bottom_ / r) * (bottom_ / r)));
        for (std::uint32_t x = 0; x < kTW; ++x) {
            const float mu = std::max(mu_of(static_cast<float>(x) / (kTW - 1)), mu_horizon + 1e-5f);
            transmittance_[static_cast<std::size_t>(y) * kTW + x] = transmittance_exact(h, mu);
        }
    }

    // 2. Multiple scattering (Hillaire 2020, section 5.5): at each height and sun angle, the light that
    // arrives from all around after one more scattering, assuming isotropic scattering for the extra
    // bounces, summed as a geometric series: psi = L2 / (1 - f_ms).
    multiple_.resize(static_cast<std::size_t>(kMS) * kMS);
    constexpr int kDirs = 64, kSteps = 20;
    std::vector<Vec3> dirs;
    for (int i = 0; i < kDirs; ++i) {  // a Fibonacci sphere: even, deterministic
        const float yv = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / kDirs;
        const float rad = std::sqrt(std::max(0.0f, 1.0f - yv * yv));
        const float phi = static_cast<float>(i) * 2.39996323f;
        dirs.push_back({rad * std::cos(phi), yv, rad * std::sin(phi)});
    }
    for (std::uint32_t y = 0; y < kMS; ++y) {
        const float h = static_cast<float>(y) / (kMS - 1) * s_.atmosphere_height_km;
        const float r = bottom_ + h;
        for (std::uint32_t x = 0; x < kMS; ++x) {
            const float mu_s = static_cast<float>(x) / (kMS - 1) * 2.0f - 1.0f;
            const Vec3 to_sun{std::sqrt(std::max(0.0f, 1.0f - mu_s * mu_s)), mu_s, 0.0f};
            Vec3 l2{0, 0, 0}, fms{0, 0, 0};
            for (const Vec3& d : dirs) {
                const float mu = d.y;
                const bool ground = hits_ground(r, mu, bottom_);
                const float length = ground ? ray_sphere(r, mu, bottom_, true) : ray_sphere(r, mu, top_, false);
                const float dt = std::max(0.0f, length) / kSteps;
                Vec3 t_acc{1, 1, 1}, l{0, 0, 0}, f{0, 0, 0};
                for (int i = 0; i < kSteps; ++i) {
                    const float t = (static_cast<float>(i) + 0.5f) * dt;
                    const Vec3 p{d.x * t, r + d.y * t, d.z * t};
                    const float rp = math::length(p);
                    const Vec3 up = p * (1.0f / rp);
                    const Medium m = medium(rp - bottom_);
                    const Vec3 scattering = m.scattering_rayleigh + m.scattering_mie;
                    const Vec3 t_sun = transmittance(rp - bottom_, math::dot(up, to_sun));
                    const Vec3 step_t = exp3(m.extinction * -dt);
                    const Vec3 integral = div_safe(Vec3{1, 1, 1} - step_t, m.extinction);  // analytic over the step
                    l = l + mul(mul(t_acc, mul(scattering, t_sun) * (1.0f / (4.0f * kPi))), integral);
                    f = f + mul(t_acc, mul(scattering, integral));
                    t_acc = mul(t_acc, step_t);
                }
                if (ground) {
                    const Vec3 p{d.x * length, r + d.y * length, d.z * length};
                    const Vec3 up = math::normalize(p);
                    const float mu_g = math::dot(up, to_sun);
                    l = l + mul(t_acc, mul(transmittance(0.0f, mu_g), s_.ground_albedo)) * (std::max(0.0f, mu_g) / kPi);
                }
                l2 = l2 + l * (1.0f / kDirs);
                fms = fms + f * (1.0f / kDirs);
            }
            const Vec3 denom{std::max(1e-3f, 1.0f - fms.x), std::max(1e-3f, 1.0f - fms.y), std::max(1e-3f, 1.0f - fms.z)};
            multiple_[static_cast<std::size_t>(y) * kMS + x] = div_safe(l2, denom);
        }
    }
}

Vec3 Atmosphere::sun_transmittance(Vec3 to_sun) const {
    const float h = std::clamp(s_.altitude_m * 0.001f, 0.0f, s_.atmosphere_height_km);
    return transmittance(h, std::clamp(to_sun.y, -1.0f, 1.0f));
}

Vec3 Atmosphere::sky_radiance(Vec3 view, Vec3 to_sun) const {
    const float r = bottom_ + std::clamp(s_.altitude_m * 0.001f, 0.0f, s_.atmosphere_height_km - 0.01f);
    const float mu = std::clamp(view.y, -1.0f, 1.0f);
    const bool ground = hits_ground(r, mu, bottom_);
    // Below the horizon the background stands for distant land, not the ground at the observer's feet
    // (the scene draws that): at least kDistantLandKm of air lies in front of it, so the horizon doesn't
    // end in a hard dark line. (Aerial perspective for real geometry: part 3.)
    constexpr float kDistantLandKm = 20.0f;
    const float length = ground ? std::max(kDistantLandKm, ray_sphere(r, mu, bottom_, true)) : std::max(0.0f, ray_sphere(r, mu, top_, false));
    const float c = math::dot(view, to_sun);
    const float pr = phase_rayleigh(c), pm = phase_mie(c, s_.mie_g);
    // Samples bunched near the observer, where the air is dense and changes fastest (Hillaire's choice).
    constexpr int kSteps = 40;
    Vec3 t_acc{1, 1, 1}, l{0, 0, 0};
    float t_prev = 0.0f;
    for (int i = 0; i < kSteps; ++i) {
        const float a = (static_cast<float>(i) + 1.0f) / kSteps;
        const float t_next = length * a * a;
        const float t = 0.5f * (t_prev + t_next), dt = t_next - t_prev;
        t_prev = t_next;
        const Vec3 p{view.x * t, r + view.y * t, view.z * t};
        const float rp = math::length(p);
        const Vec3 up = p * (1.0f / rp);
        const float h = rp - bottom_;
        const Medium m = medium(h);
        const float mu_s = math::dot(up, to_sun);
        const Vec3 t_sun = transmittance(h, mu_s);
        const Vec3 single = mul(m.scattering_rayleigh * pr + m.scattering_mie * pm, t_sun);
        const Vec3 multi = mul(m.scattering_rayleigh + m.scattering_mie, multiple_scattering(h, mu_s));
        const Vec3 step_t = exp3(m.extinction * -dt);
        l = l + mul(t_acc, mul(single + multi, div_safe(Vec3{1, 1, 1} - step_t, m.extinction)));
        t_acc = mul(t_acc, step_t);
    }
    if (ground) {  // the lit ground at the end of the ray, seen through the air in front of it
        const Vec3 p{view.x * length, r + view.y * length, view.z * length};
        const Vec3 up = math::normalize(p);
        const float mu_g = math::dot(up, to_sun);
        l = l + mul(t_acc, mul(transmittance(0.0f, mu_g), s_.ground_albedo)) * (std::max(0.0f, mu_g) / kPi);
    }
    return l;
}

EquirectImage Atmosphere::sky_image(Vec3 to_sun, std::uint32_t width, std::uint32_t height) const {
    EquirectImage img;
    img.width = std::max(8u, width);
    img.height = std::max(4u, height);
    img.rgb.resize(static_cast<std::size_t>(img.width) * img.height * 3);
    const Vec3 sun = math::normalize(to_sun);
    // Rows in parallel (each pixel is independent, so the result doesn't depend on the threads).
    const unsigned workers = std::max(1u, std::min(std::thread::hardware_concurrency(), 16u));
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; ++w) {
        pool.emplace_back([&, w] {
            for (std::uint32_t y = w; y < img.height; y += workers)
                for (std::uint32_t x = 0; x < img.width; ++x) {
                    const Vec3 d = equirect_direction((static_cast<float>(x) + 0.5f) / img.width, (static_cast<float>(y) + 0.5f) / img.height);
                    const Vec3 l = sky_radiance(d, sun);
                    float* out = &img.rgb[(static_cast<std::size_t>(y) * img.width + x) * 3];
                    out[0] = l.x, out[1] = l.y, out[2] = l.z;
                }
        });
    }
    for (auto& t : pool) t.join();
    return img;
}

std::shared_ptr<const Atmosphere> Atmosphere::shared(const AtmosphereSettings& settings) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const Atmosphere>> cache;  // a handful of settings at most
    const std::string key = settings.key();
    {
        std::scoped_lock lock(mutex);
        if (const auto it = cache.find(key); it != cache.end()) return it->second;
    }
    auto built = std::make_shared<const Atmosphere>(settings);
    std::scoped_lock lock(mutex);
    if (cache.size() > 16) cache.clear();
    return cache.emplace(key, std::move(built)).first->second;
}

} // namespace noor::asset
