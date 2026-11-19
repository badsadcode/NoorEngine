#include "noor/fx/particles.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

namespace noor::fx {
namespace {

constexpr double kPi = 3.14159265358979323846;
using math::Vec3;

// sin on [-pi/2, pi/2]: the Taylor series to x^15 (error under 1e-9 there), in doubles: plain + - * only, so
// every compiler and optimisation level gives the same bits.
double sin_quarter(double x) noexcept {
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n <= 7; ++n) {
        term *= -x2 / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

double det_sin_d(double x) noexcept {
    x -= 2.0 * kPi * std::floor(x / (2.0 * kPi) + 0.5);  // [-pi, pi]
    if (x > 0.5 * kPi) x = kPi - x;                       // sin(x) = sin(pi - x)
    if (x < -0.5 * kPi) x = -kPi - x;
    return sin_quarter(x);
}

float number(const asset::Json& j, const char* key, float fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

// A number, or [lo, hi]: the range a value is drawn from.
std::pair<float, float> range_of(const asset::Json& j, const char* key, float fallback) {
    if (!j.is_object() || !j.contains(key)) return {fallback, fallback};
    const auto& v = j[key];
    if (v.is_number()) return {v.get<float>(), v.get<float>()};
    if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
        const float a = v[0].get<float>(), b = v[1].get<float>();
        return {std::min(a, b), std::max(a, b)};
    }
    return {fallback, fallback};
}

bool parse_hex(const asset::Json& j, Vec3& out) {
    if (!j.is_string()) return false;
    const std::string s = j.get<std::string>();
    if (s.size() != 7 || s[0] != '#') return false;
    const auto channel = [&](std::size_t at) -> float {
        const unsigned v = std::stoul(s.substr(at, 2), nullptr, 16);
        const float c = static_cast<float>(v) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);  // sRGB -> linear
    };
    try {
        out = {channel(1), channel(3), channel(5)};
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

Vec3 unit_direction(Random& r) {  // uniform over the sphere
    const float z = 2.0f * r.uniform() - 1.0f;
    const float phi = 2.0f * static_cast<float>(kPi) * r.uniform();
    const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {s * det_cos(phi), s * det_sin(phi), z};
}

} // namespace

float det_sin(float x) noexcept { return static_cast<float>(det_sin_d(static_cast<double>(x))); }
float det_cos(float x) noexcept { return static_cast<float>(det_sin_d(static_cast<double>(x) + 0.5 * kPi)); }

std::uint32_t Random::next() noexcept {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
}

float Random::uniform() noexcept { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

float Curve::at(float t) const noexcept {
    if (keys.empty()) return 0.0f;
    if (t <= keys.front().first) return keys.front().second;
    if (t >= keys.back().first) return keys.back().second;
    for (std::size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].first) {
            const auto& [t0, v0] = keys[i - 1];
            const auto& [t1, v1] = keys[i];
            const float f = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0f;
            return v0 + (v1 - v0) * f;
        }
    return keys.back().second;
}

Curve Curve::parse(const asset::Json& j, float fallback) {
    Curve c;
    if (j.is_number()) {
        c.keys.emplace_back(0.0f, j.get<float>());
    } else if (j.is_array()) {
        for (const auto& k : j)
            if (k.is_array() && k.size() == 2 && k[0].is_number() && k[1].is_number())
                c.keys.emplace_back(std::clamp(k[0].get<float>(), 0.0f, 1.0f), k[1].get<float>());
        std::ranges::stable_sort(c.keys, {}, &std::pair<float, float>::first);
    }
    if (c.keys.empty()) c.keys.emplace_back(0.0f, fallback);
    return c;
}

Vec3 Gradient::at(float t) const noexcept {
    if (keys.empty()) return {1, 1, 1};
    if (t <= keys.front().first) return keys.front().second;
    if (t >= keys.back().first) return keys.back().second;
    for (std::size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].first) {
            const auto& [t0, c0] = keys[i - 1];
            const auto& [t1, c1] = keys[i];
            const float f = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0f;
            return c0 + (c1 - c0) * f;
        }
    return keys.back().second;
}

Gradient Gradient::parse(const asset::Json& j, Vec3 fallback) {
    Gradient g;
    Vec3 c;
    if (parse_hex(j, c)) {
        g.keys.emplace_back(0.0f, c);
    } else if (j.is_array()) {
        for (const auto& k : j)
            if (k.is_array() && k.size() == 2 && k[0].is_number() && parse_hex(k[1], c)) g.keys.emplace_back(std::clamp(k[0].get<float>(), 0.0f, 1.0f), c);
        std::ranges::stable_sort(g.keys, {}, &std::pair<float, Vec3>::first);
    }
    if (g.keys.empty()) g.keys.emplace_back(0.0f, fallback);
    return g;
}

EmitterSettings EmitterSettings::parse(const asset::Json& j) {
    EmitterSettings s;
    s.rate = std::clamp(number(j, "rate", s.rate), 0.0f, 100000.0f);
    if (j.is_object() && j.contains("bursts") && j["bursts"].is_array())
        for (const auto& b : j["bursts"])
            if (b.is_object())
                s.bursts.push_back({std::max(0.0f, number(b, "time", 0.0f)), std::clamp(static_cast<int>(number(b, "count", 10.0f)), 0, 100000),
                                    std::max(0.0f, number(b, "interval", 0.0f)), std::max(0, static_cast<int>(number(b, "cycles", 1.0f)))});
    s.duration = std::max(0.0f, number(j, "duration", s.duration));
    if (j.is_object() && j.contains("loop") && j["loop"].is_boolean()) s.loop = j["loop"].get<bool>();
    s.max_particles = static_cast<std::uint32_t>(std::clamp(number(j, "max_particles", 1000.0f), 0.0f, 100000.0f));
    s.seed = static_cast<std::uint32_t>(std::max(0.0f, number(j, "seed", 1.0f)));
    if (j.is_object() && j.contains("shape") && j["shape"].is_object()) {
        const auto& sh = j["shape"];
        const std::string type = sh.value("type", std::string("point"));
        s.shape = type == "sphere" ? Shape::Sphere : type == "box" ? Shape::Box : type == "cone" ? Shape::Cone : Shape::Point;
        s.radius = std::max(0.0f, number(sh, "radius", s.radius));
        if (sh.contains("size") && sh["size"].is_array() && sh["size"].size() == 3)
            for (int i = 0; i < 3; ++i)
                if (sh["size"][static_cast<std::size_t>(i)].is_number())
                    (&s.size.x)[i] = std::max(0.0f, sh["size"][static_cast<std::size_t>(i)].get<float>());
        s.angle_deg = std::clamp(number(sh, "angle", s.angle_deg), 0.0f, 180.0f);
        if (sh.contains("surface") && sh["surface"].is_boolean()) s.surface = sh["surface"].get<bool>();
    }
    std::tie(s.lifetime_min, s.lifetime_max) = range_of(j, "lifetime", 1.0f);
    s.lifetime_min = std::max(0.001f, s.lifetime_min), s.lifetime_max = std::max(s.lifetime_min, s.lifetime_max);
    std::tie(s.speed_min, s.speed_max) = range_of(j, "speed", 1.0f);
    s.gravity = number(j, "gravity", s.gravity);
    s.drag = std::max(0.0f, number(j, "drag", s.drag));
    s.wind = number(j, "wind", s.wind);
    s.local_space = j.is_object() && j.value("space", std::string("world")) == "local";
    s.size_over_life = Curve::parse(j.is_object() && j.contains("size") ? j["size"] : asset::Json(), 0.1f);
    s.color_over_life = Gradient::parse(j.is_object() && j.contains("color") ? j["color"] : asset::Json(), {1, 1, 1});
    s.alpha_over_life = Curve::parse(j.is_object() && j.contains("alpha") ? j["alpha"] : asset::Json(), 1.0f);
    if (j.is_object() && j.contains("render") && j["render"].is_object()) s.render = j["render"];
    return s;
}

Emitter::Emitter(EmitterSettings settings, std::uint64_t salt) : settings_(std::move(settings)) {
    random_.state = static_cast<std::uint64_t>(settings_.seed) * 0x9e3779b97f4a7c15ull ^ salt;
}

int Emitter::bursts_due(double from, double to) const noexcept {
    int total = 0;
    const auto& s = settings_;
    // Looping with a duration: the bursts repeat every cycle. Otherwise one timeline.
    const bool cycling = s.loop && s.duration > 0.0f;
    const double length = cycling ? static_cast<double>(s.duration) : 0.0;
    const double first_cycle = cycling ? std::floor(from / length) : 0.0, last_cycle = cycling ? std::floor(to / length) : 0.0;
    for (double c = first_cycle; c <= last_cycle; c += 1.0) {
        const double base = c * length;
        for (const Burst& b : s.bursts) {
            const double interval = static_cast<double>(b.interval);
            const int limit = interval > 0.0 ? b.cycles : 1;  // 0 with an interval: forever
            double k = interval > 0.0 ? std::max(0.0, std::ceil((from - base - b.time) / interval)) : 0.0;
            for (;; k += 1.0) {
                if (limit > 0 && k >= static_cast<double>(limit)) break;
                const double t = base + b.time + k * interval;
                if (cycling && t >= base + length) break;
                if (t >= to) break;
                if (t >= from) total += b.count;
                if (interval <= 0.0) break;
            }
        }
    }
    return total;
}

void Emitter::spawn(int count, const math::Mat4& world) {
    const auto& s = settings_;
    const int room = static_cast<int>(s.max_particles) - static_cast<int>(particles_.size());
    count = std::min(count, std::max(0, room));
    for (int i = 0; i < count; ++i) {
        Vec3 pos{0, 0, 0}, dir{0, 1, 0};
        switch (s.shape) {
        case Shape::Point:
            dir = unit_direction(random_);
            break;
        case Shape::Sphere:
            if (s.surface) {
                dir = unit_direction(random_);
                pos = dir * s.radius;
            } else {
                Vec3 q{0, 0, 0};
                for (int tries = 0; tries < 32; ++tries) {  // uniform in the ball: rejection from the cube
                    q = {2.0f * random_.uniform() - 1.0f, 2.0f * random_.uniform() - 1.0f, 2.0f * random_.uniform() - 1.0f};
                    if (math::dot(q, q) <= 1.0f) break;
                }
                const float len = std::sqrt(math::dot(q, q));
                pos = q * s.radius;
                dir = len > 1e-6f ? q * (1.0f / len) : unit_direction(random_);
            }
            break;
        case Shape::Box: {
            pos = {(random_.uniform() - 0.5f) * s.size.x, (random_.uniform() - 0.5f) * s.size.y, (random_.uniform() - 0.5f) * s.size.z};
            if (s.surface) {  // onto one of the six faces
                const auto face = random_.next() % 6u;
                const float* half_of = &s.size.x;
                (&pos.x)[face / 2] = (face % 2 ? 0.5f : -0.5f) * half_of[face / 2];
            }
            break;
        }
        case Shape::Cone: {
            const float cos_a = det_cos(s.angle_deg * static_cast<float>(kPi) / 180.0f);
            const float z = 1.0f - random_.uniform() * (1.0f - cos_a);
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float phi = 2.0f * static_cast<float>(kPi) * random_.uniform();
            dir = {r * det_cos(phi), z, r * det_sin(phi)};
            const float rr = s.surface ? s.radius : s.radius * std::sqrt(random_.uniform());
            const float psi = 2.0f * static_cast<float>(kPi) * random_.uniform();
            pos = {rr * det_cos(psi), 0.0f, rr * det_sin(psi)};
            break;
        }
        }
        Particle p;
        p.lifetime = random_.range(s.lifetime_min, s.lifetime_max);
        const float speed = random_.range(s.speed_min, s.speed_max);
        p.random = random_.next();
        if (!s.local_space) {
            pos = world.transform_point(pos);
            const Vec3 d = math::transform_vector(world, dir);
            const float len = std::sqrt(math::dot(d, d));
            dir = len > 1e-9f ? d * (1.0f / len) : Vec3{0, 1, 0};
        }
        p.position = pos;
        p.velocity = dir * speed;
        particles_.push_back(p);
        ++spawned_;
    }
}

void Emitter::step(float dt, const math::Mat4& world, Vec3 wind) {
    const auto& s = settings_;
    Vec3 gravity{0.0f, -kGravity * s.gravity, 0.0f};
    Vec3 air = wind * s.wind;
    if (s.local_space) {  // the forces in the emitter's own axes
        const math::Mat4 inv = math::inverse(world);
        gravity = math::transform_vector(inv, gravity);
        air = math::transform_vector(inv, air);
    }
    // Ageing, and the dead out (in order: the same order every run).
    for (Particle& p : particles_) p.age += dt;
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.lifetime; });
    // Gravity, and drag pulling the velocity towards the air's (implicit: stable for any drag and step).
    const float keep = 1.0f / (1.0f + s.drag * dt);
    const Vec3 push = (gravity + air * s.drag) * dt;
    for (Particle& p : particles_) {
        p.velocity = (p.velocity + push) * keep;
        p.position = p.position + p.velocity * dt;
    }
    // This step's new particles.
    const double from = time_, to = time_ + static_cast<double>(dt);
    const bool running = s.duration <= 0.0f || s.loop || from < static_cast<double>(s.duration);
    if (running) {
        int count = 0;
        if (s.rate > 0.0f) {
            spawn_carry_ += static_cast<double>(s.rate) * static_cast<double>(dt);
            const double whole = std::floor(spawn_carry_);
            spawn_carry_ -= whole;
            count = static_cast<int>(whole);
        }
        count += bursts_due(from, std::min(to, s.loop || s.duration <= 0.0f ? to : static_cast<double>(s.duration)));
        spawn(count, world);
    }
    time_ = to;
}

void Emitter::burst(int count, const math::Mat4& world) { spawn(std::max(0, count), world); }

bool Emitter::finished() const noexcept {
    return !settings_.loop && settings_.duration > 0.0f && time_ >= static_cast<double>(settings_.duration) && particles_.empty();
}

float Emitter::size_of(const Particle& p) const noexcept { return settings_.size_over_life.at(p.age / p.lifetime); }
math::Vec3 Emitter::color_of(const Particle& p) const noexcept { return settings_.color_over_life.at(p.age / p.lifetime); }
float Emitter::alpha_of(const Particle& p) const noexcept { return settings_.alpha_over_life.at(p.age / p.lifetime); }

std::uint64_t fingerprint(std::span<const Particle> particles, std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    const auto mix = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (8 * i)) & 0xffu;
            h *= 0x100000001b3ull;
        }
    };
    mix(static_cast<std::uint32_t>(particles.size()));
    for (const Particle& p : particles) {
        for (float f : {p.position.x, p.position.y, p.position.z, p.velocity.x, p.velocity.y, p.velocity.z, p.age, p.lifetime})
            mix(std::bit_cast<std::uint32_t>(f));
    }
    return h;
}

} // namespace noor::fx
