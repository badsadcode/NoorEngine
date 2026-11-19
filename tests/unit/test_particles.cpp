// Effect particles (Milestone 8B, part 1, ADR-035): the simulation against its maths, and its determinism.
#include "noor/fx/fx_asset.h"
#include "noor/fx/particle_system.h"
#include "noor/fx/particles.h"
#include "noor/runtime/world.h"

#include <ostream>
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <format>

using namespace noor;
using asset::Json;
using math::Vec3;

namespace {

fx::Emitter make(const Json& settings, std::uint64_t salt = 1) { return fx::Emitter(fx::EmitterSettings::parse(settings), salt); }

void run(fx::Emitter& e, float seconds, const math::Mat4& world = math::Mat4::identity(), Vec3 wind = {0, 0, 0}) {
    const int steps = static_cast<int>(std::lround(seconds * 60.0f));
    for (int i = 0; i < steps; ++i) e.step(1.0f / 60.0f, world, wind);
}

} // namespace

TEST_CASE("particles: deterministic sine and cosine, and the random stream") {
    for (float x = -20.0f; x <= 20.0f; x += 0.137f) {
        CHECK(fx::det_sin(x) == doctest::Approx(std::sin(x)).epsilon(1e-5));
        CHECK(fx::det_cos(x) == doctest::Approx(std::cos(x)).epsilon(1e-5));
    }
    fx::Random a{42}, b{42};
    double sum = 0;
    for (int i = 0; i < 10000; ++i) {
        const float u = a.uniform();
        CHECK(u == b.uniform());
        CHECK(u >= 0.0f);
        CHECK(u < 1.0f);
        sum += u;
    }
    CHECK(sum / 10000.0 == doctest::Approx(0.5).epsilon(0.02));
}

TEST_CASE("particles: curves and colour gradients over life") {
    const auto c = fx::Curve::parse(Json::parse("[[0, 0.1], [0.5, 0.5], [1, 0]]"), 0.0f);
    CHECK(c.at(0.0f) == doctest::Approx(0.1f));
    CHECK(c.at(0.25f) == doctest::Approx(0.3f));
    CHECK(c.at(0.75f) == doctest::Approx(0.25f));
    CHECK(c.at(2.0f) == doctest::Approx(0.0f));
    CHECK(fx::Curve::parse(Json(2.5), 0.0f).at(0.7f) == 2.5f);
    CHECK(fx::Curve::parse(Json("nonsense"), 0.3f).at(0.5f) == 0.3f);
    // Colours are sRGB in the file and blend in linear light.
    const auto g = fx::Gradient::parse(Json::parse(R"([[0, "#000000"], [1, "#FFFFFF"]])"), {0, 0, 0});
    CHECK(g.at(0.5f).x == doctest::Approx(0.5f));
    CHECK(fx::Gradient::parse(Json("#FF0000"), {0, 0, 0}).at(0.3f).x == doctest::Approx(1.0f));
}

TEST_CASE("particles: a steady rate spawns exactly rate x time, and lifetime sets how many are alive") {
    auto e = make(Json{{"rate", 30}, {"lifetime", 100}});
    run(e, 2.0f);
    CHECK(e.spawned() == 60);
    CHECK(e.particles().size() == 60);
    // Rate 20 for half a second each: about 10 alive at any time once it has filled up.
    auto f = make(Json{{"rate", 20}, {"lifetime", 0.5}});
    run(f, 3.0f);
    CHECK(f.particles().size() >= 9);
    CHECK(f.particles().size() <= 11);
    for (const auto& p : f.particles()) CHECK(p.age < p.lifetime);
}

TEST_CASE("particles: gravity is ballistic, drag gives a terminal velocity, and wind drags particles along") {
    // One particle straight up at 5 m/s: after half a second, y = 5 t - g t^2 / 2 (within the fixed step's error).
    auto up = make(Json{{"rate", 0}, {"bursts", {{{"time", 0}, {"count", 1}}}}, {"shape", {{"type", "cone"}, {"angle", 0}, {"radius", 0}}},
                        {"speed", 5}, {"gravity", 1}, {"lifetime", 10}});
    up.step(1.0f / 60.0f, math::Mat4::identity(), {0, 0, 0});  // spawned at the end of the first step
    REQUIRE(up.particles().size() == 1);
    run(up, 0.5f);
    const float t = 0.5f, y = 5.0f * t - 0.5f * fx::kGravity * t * t;
    CHECK(up.particles()[0].position.y == doctest::Approx(y).epsilon(0.02));
    // Falling with drag 2: the terminal velocity is g / drag.
    auto fall = make(Json{{"rate", 0}, {"bursts", {{{"count", 1}}}}, {"speed", 0}, {"gravity", 1}, {"drag", 2}, {"lifetime", 100}});
    run(fall, 8.0f);
    CHECK(fall.particles()[0].velocity.y == doctest::Approx(-fx::kGravity / 2.0f).epsilon(0.01));
    // No gravity, drag 1, a 3 m/s wind along +X: they end up moving with the air.
    auto blown = make(Json{{"rate", 0}, {"bursts", {{{"count", 5}}}}, {"speed", 0}, {"drag", 1}, {"lifetime", 100}});
    run(blown, 10.0f, math::Mat4::identity(), {3, 0, 0});
    for (const auto& p : blown.particles()) CHECK(p.velocity.x == doctest::Approx(3.0f).epsilon(0.01));
    // Without drag, wind does nothing (it acts through the air's drag).
    auto still = make(Json{{"rate", 0}, {"bursts", {{{"count", 1}}}}, {"speed", 0}, {"lifetime", 100}});
    run(still, 2.0f, math::Mat4::identity(), {3, 0, 0});
    CHECK(still.particles()[0].velocity.x == 0.0f);
}

TEST_CASE("particles: shapes put particles where they say") {
    const auto burst = [](const Json& shape) {
        auto e = make(Json{{"rate", 0}, {"bursts", {{{"count", 500}}}}, {"speed", 1}, {"lifetime", 100}, {"max_particles", 1000}, {"shape", shape}});
        e.step(1e-6f, math::Mat4::identity(), {0, 0, 0});
        return e;
    };
    // (Each emitter kept in a variable: looping over a temporary's particles would read freed memory.)
    const auto ball = burst(Json{{"type", "sphere"}, {"radius", 2}});
    for (const auto& p : ball.particles()) CHECK(math::length(p.position) <= 2.0001f);
    const auto shell = burst(Json{{"type", "sphere"}, {"radius", 2}, {"surface", true}});
    for (const auto& p : shell.particles())
        CHECK(math::length(p.position) == doctest::Approx(2.0f).epsilon(1e-4));
    const auto box = burst(Json{{"type", "box"}, {"size", {4, 1, 2}}});
    for (const auto& p : box.particles()) {
        CHECK(std::abs(p.position.x) <= 2.0001f);
        CHECK(std::abs(p.position.y) <= 0.5001f);
        CHECK(std::abs(p.position.z) <= 1.0001f);
        CHECK(p.velocity.y == doctest::Approx(1.0f));  // a box emits along +Y
    }
    const float cos25 = std::cos(25.0f * 3.14159265f / 180.0f);
    const auto cone = burst(Json{{"type", "cone"}, {"angle", 25}, {"radius", 0.3}});
    for (const auto& p : cone.particles()) {
        CHECK(p.velocity.y >= cos25 - 1e-4f);  // within 25 degrees of +Y
        CHECK(std::hypot(p.position.x, p.position.z) <= 0.3001f);
    }
    // A point emits every way: the directions average out.
    Vec3 mean{0, 0, 0};
    const auto points = burst(Json{{"type", "point"}});
    for (const auto& p : points.particles()) mean = mean + p.velocity * (1.0f / 500.0f);
    CHECK(math::length(mean) < 0.12f);
}

TEST_CASE("particles: bursts repeat as told, a cap holds, and a one-shot finishes") {
    auto e = make(Json{{"rate", 0}, {"bursts", {{{"time", 0.5}, {"count", 10}, {"interval", 1}, {"cycles", 3}}}}, {"lifetime", 100}});
    run(e, 0.4f);
    CHECK(e.spawned() == 0);
    run(e, 0.2f);
    CHECK(e.spawned() == 10);   // at 0.5 s
    run(e, 5.0f);
    CHECK(e.spawned() == 30);   // 0.5, 1.5, 2.5: three cycles, then no more
    auto capped = make(Json{{"rate", 1000}, {"lifetime", 100}, {"max_particles", 50}});
    run(capped, 2.0f);
    CHECK(capped.particles().size() == 50);
    auto once = make(Json{{"rate", 0}, {"bursts", {{{"count", 5}}}}, {"lifetime", 0.5}, {"duration", 1}, {"loop", false}});
    run(once, 0.3f);
    CHECK(!once.finished());
    run(once, 1.0f);
    CHECK(once.finished());
    // Looping with a duration: the burst comes back every cycle.
    auto loop = make(Json{{"rate", 0}, {"bursts", {{{"time", 0.1}, {"count", 4}}}}, {"lifetime", 100}, {"duration", 1}, {"loop", true}});
    run(loop, 3.05f);
    CHECK(loop.spawned() == 12);
}

TEST_CASE("particles: world-space particles stay behind a moving emitter, local ones travel with it") {
    const Json settings{{"rate", 0}, {"bursts", {{{"count", 20}}}}, {"speed", 0}, {"lifetime", 100}};
    auto world_e = make(settings);
    Json local_settings = settings;
    local_settings["space"] = "local";
    auto local_e = make(local_settings);
    world_e.step(1.0f / 60.0f, math::Mat4::identity(), {0, 0, 0});
    local_e.step(1.0f / 60.0f, math::Mat4::identity(), {0, 0, 0});
    const math::Mat4 moved = math::translation({10, 0, 0});
    run(world_e, 1.0f, moved);
    run(local_e, 1.0f, moved);
    for (const auto& p : world_e.particles()) CHECK(std::abs(p.position.x) < 1e-5f);  // still where they were born
    for (const auto& p : local_e.particles()) CHECK(std::abs(p.position.x) < 1e-5f);  // in the emitter's own space...
    CHECK(moved.transform_point(local_e.particles()[0].position).x == doctest::Approx(10.0f));  // ...so they're drawn at it
}

TEST_CASE("particles: the same seed gives the same particles to the bit, and the fingerprint is pinned") {
    const Json fire{{"rate", 40}, {"bursts", {{{"time", 0.2}, {"count", 25}, {"interval", 0.7}, {"cycles", 0}}}},
                    {"shape", {{"type", "cone"}, {"angle", 30}, {"radius", 0.2}}}, {"lifetime", {0.6, 1.4}}, {"speed", {1, 3}},
                    {"gravity", -0.2}, {"drag", 0.8}, {"seed", 7}, {"max_particles", 2000}};
    auto a = make(fire, 99), b = make(fire, 99);
    const math::Mat4 at = math::compose({1, 2, 3}, math::Quat::from_axis_angle({0, 0, 1}, 0.3f), {1, 1, 1});
    run(a, 4.0f, at, {1.5f, 0, -0.5f});
    run(b, 4.0f, at, {1.5f, 0, -0.5f});
    CHECK(fx::fingerprint(a.particles()) == fx::fingerprint(b.particles()));
    // Another salt (another entity) or another seed: other particles.
    auto c = make(fire, 100);
    run(c, 4.0f, at, {1.5f, 0, -0.5f});
    CHECK(fx::fingerprint(c.particles()) != fx::fingerprint(a.particles()));
    // Pinned: Debug and Release, this machine and the next, must all give exactly this.
    MESSAGE(std::format("fingerprint {:016x}, {} particles", fx::fingerprint(a.particles()), a.particles().size()));
    CHECK(std::format("{:016x}", fx::fingerprint(a.particles())) == "eb88930c407abfab");
}

TEST_CASE("particles: the runtime system steps emitter components, follows changes, and reads .noorfx overrides") {
    scene::Scene s;
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Sparks";
    e.transform.position = {0, 1, 0};
    e.components["emitter"] = Json{{"rate", 60}, {"lifetime", 100}};
    s.entities.push_back(e);
    scene::Entity sky;
    sky.id = Guid::generate();
    sky.components["environment"] = Json{{"wind_speed", 4}, {"wind_direction", 90}};  // towards +X
    s.entities.push_back(sky);
    runtime::World world(std::move(s));
    world.add_system(std::make_unique<fx::ParticleSystem>());
    for (int i = 0; i < 60; ++i) world.frame(1.0 / 60.0, {});
    auto* system = world.system<fx::ParticleSystem>();
    REQUIRE(system);
    CHECK(system->stats().emitters == 1);
    CHECK(system->stats().particles == doctest::Approx(60).epsilon(0.05));
    // A script turns it off: no new particles.
    world.find(e.id)->components["emitter"]["rate"] = 0;
    const auto before = system->find(e.id)->spawned();
    for (int i = 0; i < 30; ++i) world.frame(1.0 / 60.0, {});
    CHECK(system->find(e.id)->spawned() == before);
    // The component removed: the emitter goes.
    world.find(e.id)->components.erase("emitter");
    world.frame(1.0 / 60.0, {});
    CHECK(system->stats().emitters == 0);
    // An asset's settings with a component's override on top.
    const Json file = fx::fx_file_json("Smoke", Json{{"rate", 5}, {"lifetime", 3}, {"gravity", -0.1}});
    const Json merged = fx::merge_emitter(file["emitter"], Json{{"asset", Guid::generate().to_string()}, {"rate", 12}});
    CHECK(merged["rate"] == 12);
    CHECK(merged["lifetime"] == 3);
    CHECK(!merged.contains("asset"));
}

TEST_CASE("particles: 10,000 particles step in well under a millisecond") {
    auto e = make(Json{{"rate", 0}, {"bursts", {{{"count", 10000}}}}, {"max_particles", 10000}, {"lifetime", 100}, {"gravity", 1}, {"drag", 0.5},
                       {"shape", {{"type", "sphere"}, {"radius", 3}}}});
    e.step(1.0f / 60.0f, math::Mat4::identity(), {1, 0, 0});
    REQUIRE(e.particles().size() == 10000);
    double best = 1e9;
    for (int run_i = 0; run_i < 50; ++run_i) {
        const auto t0 = std::chrono::steady_clock::now();
        e.step(1.0f / 60.0f, math::Mat4::identity(), {1, 0, 0});
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    MESSAGE(std::format("10,000 particles: {:.3f} ms a step (fastest of 50)", best));
    CHECK(best < 1.0);
}
