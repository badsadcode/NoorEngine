#pragma once
// Effect particles (Milestone 8B, part 1, ADR-035): an emitter's settings and its simulation.
//
// An emitter spawns particles (a steady rate, timed bursts) from a shape, gives each a lifetime and a velocity, and
// moves them in fixed steps under gravity and drag towards the wind; their size, colour and alpha follow curves over
// their life. Everything is deterministic: a seeded random stream per emitter, fixed steps, and maths that gives the
// same bits in Debug and Release on any machine (no library sine or exp, which an optimiser may swap for vector
// versions with different rounding; sqrt is exact by IEEE). So the same scene replays the same particles, which is
// what goldens and built-game checks rely on.
//
// The `emitter` component (or a .noorfx asset's "emitter", fx_asset.h) is JSON:
//
//   {"rate": 20, "bursts": [{"time": 0, "count": 30, "interval": 0, "cycles": 1}],
//    "duration": 0 (seconds; 0 = runs forever), "loop": true, "max_particles": 1000, "seed": 1,
//    "shape": {"type": "point" | "sphere" | "box" | "cone", "radius": 0.5, "size": [1, 1, 1], "angle": 25,
//              "surface": false},
//    "lifetime": 1.5 or [1, 2], "speed": 2 or [1, 3], "gravity": 0 (x 9.81 m/s^2 down), "drag": 0 (per second),
//    "wind": 1 (how much of the scene's wind drags them), "space": "world" | "local",
//    "size": 0.1 or [[0, 0.1], [1, 0.4]] (metres over life), "color": "#FFFFFF" or [[0, "#FFD080"], [1, "#802010"]],
//    "alpha": 1 or [[0, 1], [1, 0]],
//    "collide": {"ground": true, "planes": [{"y": 0} or {"point": [x, y, z], "normal": [x, y, z]}], "bounce": 0.3,
//                "friction": 0.2, "radius": 0, "kill": false, "hit_speed": 0.5} (part 7; world-space emitters),
//    "sub_emitters": [{"on": "hit" | "death", "emitter": {...} or {"asset": id, ...}, "count": 4,
//                      "inherit_velocity": 0}] (part 7),
//    "render": {...} (drawing, part 2: kept as it is)}
//
// Collisions (part 7): a particle that ends a step below the ground (the scene's terrains, through a GroundQuery) or
// behind a plane is put back on the surface; the part of its velocity into the surface comes back times `bounce`, the
// part along it loses `friction`. A hit faster than `hit_speed` (m/s into the surface) is an event: it can kill the
// particle ("kill") and feed sub-emitters "on": "hit". Particles that die feed "on": "death" ones. A sub-emitter is an
// emitter of its own (world space, no rate unless given) that bursts `count` particles where the event happened.
//
// Shapes, in the emitter's own axes: a point emits in every direction; a sphere (or its surface) emits outwards;
// a box emits along +Y; a cone emits along +Y within `angle` degrees, from a disc of `radius` at its base.

#include "noor/asset/json.h"
#include "noor/foundation/math.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace noor::fx {

inline constexpr float kGravity = 9.81f;  // m/s^2

// ---- Deterministic maths ----
[[nodiscard]] float det_sin(float x) noexcept;  // within 1e-6 of sin on any angle; the same bits everywhere
[[nodiscard]] float det_cos(float x) noexcept;

// A small random stream (SplitMix64): the same seed gives the same numbers on any machine.
struct Random {
    std::uint64_t state = 0;
    [[nodiscard]] std::uint32_t next() noexcept;
    [[nodiscard]] float uniform() noexcept;                    // [0, 1)
    [[nodiscard]] float range(float lo, float hi) noexcept { return lo + (hi - lo) * uniform(); }
};

// A value over a particle's life (t from 0 to 1): linear between keys; one key is a constant.
struct Curve {
    std::vector<std::pair<float, float>> keys;
    [[nodiscard]] float at(float t) const noexcept;
    // A number, or [[t, value], ...]; `fallback` when missing or malformed.
    [[nodiscard]] static Curve parse(const asset::Json& j, float fallback);
};

// A colour over life: keys in linear light ("#RRGGBB" in the JSON is sRGB, converted), linear between keys.
struct Gradient {
    std::vector<std::pair<float, math::Vec3>> keys;
    [[nodiscard]] math::Vec3 at(float t) const noexcept;
    [[nodiscard]] static Gradient parse(const asset::Json& j, math::Vec3 fallback);
};

struct Burst {
    float time = 0.0f;      // seconds into the emitter's cycle
    int count = 10;
    float interval = 0.0f;  // repeat every this many seconds (0: once)
    int cycles = 1;         // how many times (0 with an interval: forever)
};

enum class Shape : std::uint8_t { Point, Sphere, Box, Cone };

// The scene's ground under a point (x, z, world space): its height and its normal there; nullopt where there's none.
struct GroundHit {
    float height = 0.0f;
    math::Vec3 normal{0, 1, 0};
};
using GroundQuery = std::function<std::optional<GroundHit>(float x, float z)>;

struct CollisionPlane {
    math::Vec3 point{0, 0, 0};
    math::Vec3 normal{0, 1, 0};  // unit; the side particles stay on
};

struct Collision {
    bool enabled = false;
    bool ground = true;             // the scene's ground (terrains)
    std::vector<CollisionPlane> planes;
    float bounce = 0.3f;            // how much of the speed into the surface comes back (0: no bounce)
    float friction = 0.2f;          // how much of the speed along the surface is lost per hit
    float radius = 0.0f;            // particles stop this far from the surface (m)
    bool kill = false;              // a hit ends the particle
    float hit_speed = 0.5f;         // slower contacts (resting, rolling) aren't events
};

struct SubEmitter {
    enum class On : std::uint8_t { Hit, Death } on = On::Hit;
    asset::Json emitter = asset::Json::object();  // its settings (resolved: an asset already merged in)
    int count = 4;                                 // particles per event
    float inherit_velocity = 0.0f;                 // how much of the parent particle's velocity they start with
};

struct EmitterSettings {
    float rate = 10.0f;
    std::vector<Burst> bursts;
    float duration = 0.0f;  // 0: forever
    bool loop = true;
    std::uint32_t max_particles = 1000;
    std::uint32_t seed = 1;
    Shape shape = Shape::Point;
    float radius = 0.5f;
    math::Vec3 size{1, 1, 1};
    float angle_deg = 25.0f;
    bool surface = false;
    float lifetime_min = 1.0f, lifetime_max = 1.0f;
    float speed_min = 1.0f, speed_max = 1.0f;
    float gravity = 0.0f;
    float drag = 0.0f;
    float wind = 1.0f;
    bool local_space = false;
    Curve size_over_life;
    Gradient color_over_life;
    Curve alpha_over_life;
    Collision collision;
    std::vector<SubEmitter> sub_emitters;  // at most 4
    asset::Json render = asset::Json::object();

    // Every field optional (the defaults above); clamped to sane ranges (no negative lifetimes or rates, at most
    // 100,000 particles an emitter).
    [[nodiscard]] static EmitterSettings parse(const asset::Json& j);
};

struct Particle {
    math::Vec3 position;  // world space, or the emitter's own space when local
    math::Vec3 velocity;  // the same space
    float age = 0.0f;     // seconds
    float lifetime = 1.0f;
    std::uint32_t random = 0;  // a number of its own (drawing: a flipbook frame, a spin)
};

class Emitter {
public:
    // `salt` makes two emitters with the same settings differ (the system passes a hash of the entity's id).
    Emitter(EmitterSettings settings, std::uint64_t salt);

    // One fixed step of `dt` seconds: ages and moves the particles (gravity, drag towards `wind`, m/s, world space),
    // removes the dead, collides (with `ground` if given, and the settings' planes), then spawns this step's new ones
    // from the emitter at `world` (its entity's world matrix). `budget`, if given, is how many more particles the
    // scene allows: spawning takes from it and stops at 0.
    void step(float dt, const math::Mat4& world, math::Vec3 wind, const GroundQuery* ground = nullptr, std::uint32_t* budget = nullptr);
    // Spawns `count` particles now (scripts, part 6; sub-emitters, part 7), within max_particles and the budget,
    // each with `extra` added to its velocity.
    void burst(int count, const math::Mat4& world, math::Vec3 extra = {0, 0, 0}, std::uint32_t* budget = nullptr);

    // What happened to particles in the last step (sub-emitters): hits and deaths, in particle order, world space.
    struct Event {
        SubEmitter::On on;
        math::Vec3 position, velocity;
    };
    [[nodiscard]] std::span<const Event> events() const noexcept { return events_; }
    [[nodiscard]] std::uint64_t hits() const noexcept { return hits_; }            // collision events so far
    [[nodiscard]] std::uint64_t throttled() const noexcept { return throttled_; }  // held back by the scene's budget
    // New settings (a script or the editor changed the component). Keeps the particles and the random stream.
    void set_settings(EmitterSettings settings) { settings_ = std::move(settings); }

    [[nodiscard]] const EmitterSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] std::span<const Particle> particles() const noexcept { return particles_; }
    [[nodiscard]] double time() const noexcept { return time_; }
    [[nodiscard]] std::uint64_t spawned() const noexcept { return spawned_; }
    [[nodiscard]] bool finished() const noexcept;  // a one-shot emitter past its duration with no particles left

    // What a particle looks like now (its life's curves): size in metres, linear colour, alpha.
    [[nodiscard]] float size_of(const Particle& p) const noexcept;
    [[nodiscard]] math::Vec3 color_of(const Particle& p) const noexcept;
    [[nodiscard]] float alpha_of(const Particle& p) const noexcept;

private:
    void spawn(int count, const math::Mat4& world, math::Vec3 extra = {0, 0, 0}, std::uint32_t* budget = nullptr);
    void collide(const math::Mat4& world, const GroundQuery* ground);
    [[nodiscard]] int bursts_due(double from, double to) const noexcept;

    EmitterSettings settings_;
    Random random_;
    std::vector<Particle> particles_;
    double time_ = 0.0;
    double spawn_carry_ = 0.0;
    std::uint64_t spawned_ = 0;
    std::vector<Event> events_;
    std::uint64_t hits_ = 0, throttled_ = 0;
};

// A fingerprint of particles (count, positions, velocities, ages, bit for bit): equal fingerprints, equal states.
[[nodiscard]] std::uint64_t fingerprint(std::span<const Particle> particles, std::uint64_t seed = 0xcbf29ce484222325ull) noexcept;

} // namespace noor::fx
