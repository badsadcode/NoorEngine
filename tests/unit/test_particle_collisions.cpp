// Gameplay particles (Milestone 8B, part 7, ADR-035): collisions with planes and terrains, sub-emitters on hits and
// deaths, and budgets. Everything here is deterministic like the rest of NoorFX: the same scene, the same particles.
#include "noor/fx/particle_system.h"
#include "noor/fx/particles.h"
#include "noor/game/particle_ground.h"
#include "noor/runtime/world.h"
#include "noor/terrain/terrain.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <format>

using namespace noor;
using asset::Json;

namespace {

fx::Emitter make(const Json& settings, std::uint64_t salt = 7) { return fx::Emitter(fx::EmitterSettings::parse(settings), salt); }

void run(fx::Emitter& e, int steps, const fx::GroundQuery* ground = nullptr, math::Mat4 world = math::Mat4::identity()) {
    for (int i = 0; i < steps; ++i) e.step(1.0f / 60.0f, world, {0, 0, 0}, ground);
}

// A hilly terrain (noise, 40 m from low to high over 512 m) centred at `at`.
std::shared_ptr<const terrain::Heights> hills() {
    terrain::Settings s;
    s.tiles_x = s.tiles_z = 2;
    s.cell_size = 1.0f;
    s.height = 40.0f;
    s.scale = 120.0f;
    auto h = terrain::Heights::shared(s);
    REQUIRE(h.has_value());
    return *h;
}

} // namespace

TEST_CASE("particle collisions: dropped particles stop on a plane, and bounce by `bounce`") {
    // A 2 m drop onto y = 0 with no bounce: they all end resting on it, none below.
    auto e = make(Json::parse(R"({"rate": 0, "bursts": [{"count": 50}], "lifetime": 10, "speed": [0, 1], "gravity": 1,
                                  "shape": {"type": "box", "size": [2, 0, 2]}, "collide": {"planes": [{"y": 0}], "bounce": 0, "friction": 1}})"));
    run(e, 1, nullptr, math::translation({0, 2, 0}));  // spawn 2 m up (the box is flat at the emitter)
    run(e, 180);
    REQUIRE(e.particles().size() == 50);
    for (const auto& p : e.particles()) {
        CHECK(p.position.y >= 0.0f);
        CHECK(p.position.y < 1e-4f);
        CHECK(std::abs(p.velocity.y) < 0.2f);  // at rest (gravity's one step at most)
    }
    CHECK(e.hits() >= 50);  // each landed once at least (faster than hit_speed)

    // One particle dropped from 5 m with bounce 0.5: it comes back up with half the speed, so a quarter of the height.
    auto b = make(Json::parse(R"({"rate": 0, "bursts": [{"count": 1}], "lifetime": 10, "speed": 0, "gravity": 1,
                                  "collide": {"planes": [{"y": 0}], "bounce": 0.5, "friction": 0}})"));
    run(b, 1, nullptr, math::translation({0, 5, 0}));
    float peak = 0.0f;
    bool landed = false;
    for (int i = 0; i < 240; ++i) {
        run(b, 1);
        const float y = b.particles()[0].position.y;
        if (b.hits() > 0) landed = true;
        if (landed && b.hits() == 1) peak = std::max(peak, y);
    }
    CHECK(landed);
    CHECK(peak == doctest::Approx(1.25f).epsilon(0.08));  // 5 m x 0.5^2 (within a fixed step's error)

    // A sloped plane: they end on its surface, never behind it.
    auto s = make(Json::parse(R"({"rate": 0, "bursts": [{"count": 30}], "lifetime": 10, "speed": [0, 2], "gravity": 1,
                                  "collide": {"planes": [{"point": [0, 0, 0], "normal": [0.5, 1, 0]}], "bounce": 0.2}})"));
    run(s, 1, nullptr, math::translation({0, 3, 0}));
    run(s, 300);
    const math::Vec3 n = math::normalize(math::Vec3{0.5f, 1, 0});
    for (const auto& p : s.particles()) CHECK(math::dot(p.position, n) >= -1e-4f);
}

TEST_CASE("particle collisions: particles stop on the terrain, not through it") {
    game::ParticleGround ground;
    ground.set({{{10, 0, -20}, hills()}});
    const fx::GroundQuery query = ground.query();
    // Rain-like: from a 60 m square 60 m up, falling fast, for 6 s.
    const Json settings = Json::parse(R"({"rate": 400, "lifetime": 20, "speed": [0, 0.5], "gravity": 1, "max_particles": 5000,
        "shape": {"type": "box", "size": [60, 0, 60]}, "collide": {"bounce": 0.1, "friction": 0.5, "radius": 0.02}})");
    auto e = make(settings);
    run(e, 360, &query, math::translation({10, 60, -20}));
    REQUIRE(e.particles().size() > 1000);
    int resting = 0;
    for (const auto& p : e.particles()) {
        const auto g = ground.at(p.position.x, p.position.z);
        REQUIRE(g.has_value());
        CHECK(p.position.y >= g->height + 0.02f - 1e-3f);  // on the surface (plus the radius), never under it
        if (p.position.y < g->height + 0.05f) ++resting;
    }
    CHECK(resting > 500);  // most have landed
    // The same surface the terrain is drawn with: Heights::height at the terrain's own coordinates.
    const auto h = hills();
    CHECK(ground.at(10.0f + 3.25f, -20.0f - 7.5f)->height == doctest::Approx(h->height(3.25f, -7.5f)));
    CHECK(!ground.at(1000.0f, 0.0f).has_value());  // off the terrain: nothing to land on
    // Without "collide" they fall straight through.
    auto loose = make(Json::parse(R"({"rate": 400, "lifetime": 20, "gravity": 1, "shape": {"type": "box", "size": [60, 0, 60]}})"));
    run(loose, 360, &query, math::translation({10, 60, -20}));
    CHECK(std::ranges::any_of(loose.particles(), [&](const fx::Particle& p) { return p.position.y < ground.at(p.position.x, p.position.z)->height - 1.0f; }));
}

TEST_CASE("sub-emitters: splashes where drops hit, sparks where embers die, the same every run") {
    // Drops that die on the ground and splash: 3 splash particles per hit, in a sub-emitter of their own.
    const Json rain = Json::parse(R"({"rate": 60, "lifetime": 5, "speed": 0, "gravity": 1, "shape": {"type": "box", "size": [4, 0, 4]},
        "collide": {"planes": [{"y": 0}], "kill": true},
        "sub_emitters": [{"on": "hit", "count": 3, "emitter": {"lifetime": 0.3, "speed": [1, 2], "shape": {"type": "cone", "angle": 40}}},
                         {"on": "death", "count": 1, "emitter": {"lifetime": 2, "speed": 0}}]})");
    const auto simulate = [&](int steps) {
        fx::EmitterSet set;
        const Guid id = Guid::parse("5b417e00-0007-4000-8000-000000000001").value();
        const math::Mat4 at = math::translation({0, 3, 0});
        const fx::EmitterSet::Input in{id, &rain, at};
        for (int i = 0; i < steps; ++i) set.step({&in, 1}, 1.0f / 60.0f, {0, 0, 0});
        return std::make_pair(set.stats(), set.emitters().size());
    };
    const auto [stats, count] = simulate(180);
    CHECK(count == 3);  // the rain and its two sub-emitters
    CHECK(stats.hits > 50);
    // Every hit made 3 splash particles; they live 0.3 s, so the ones alive are the last 0.3 s's hits. And each drop
    // that died on the ground left a mark (the death sub-emitter, 2 s): at least as many as hits in the last 2 s.
    CHECK(stats.particles > 0);
    const auto [again, count2] = simulate(180);
    CHECK(again.fingerprint == stats.fingerprint);  // deterministic, sub-emitters included
    CHECK(again.hits == stats.hits);

    // Exact counts on one emitter: 10 drops landing at once make exactly 30 splash particles.
    const Json ten = Json::parse(R"({"rate": 0, "bursts": [{"count": 10}], "lifetime": 5, "speed": 0, "gravity": 1,
        "shape": {"type": "box", "size": [1, 0, 1]}, "collide": {"planes": [{"y": 0}], "kill": true},
        "sub_emitters": [{"on": "hit", "count": 3, "emitter": {"lifetime": 10, "speed": 0}}]})");
    fx::EmitterSet set;
    const Guid id = Guid::parse("5b417e00-0007-4000-8000-000000000002").value();
    const fx::EmitterSet::Input in{id, &ten, math::translation({0, 1, 0})};
    for (int i = 0; i < 120; ++i) set.step({&in, 1}, 1.0f / 60.0f, {0, 0, 0});
    REQUIRE(set.find(id) != nullptr);
    CHECK(set.find(id)->particles().empty());  // every drop died on the ground
    const auto* splash = set.find(fx::sub_emitter_id(id, 0));
    REQUIRE(splash != nullptr);
    CHECK(splash->particles().size() == 30);
    for (const auto& p : splash->particles()) CHECK(std::abs(p.position.y) < 0.05f);  // where they hit
}

TEST_CASE("particle budgets: 50 emitters asking for 150,000 particles get the scene's 20,000, the same way every run") {
    scene::Scene s;
    for (int i = 0; i < 50; ++i) {
        scene::Entity e;
        e.id = Guid::parse(std::format("5b417e00-0007-4000-8000-{:012d}", 100 + i)).value();
        e.transform.position = {static_cast<float>(i % 10) * 3.0f, 0, static_cast<float>(i / 10) * 3.0f};
        e.components["emitter"] = {{"rate", 1000}, {"lifetime", 3}, {"max_particles", 3000}, {"speed", {1, 2}}, {"seed", i}};
        s.entities.push_back(e);
    }
    const auto play = [&](int frames, double* worst_ms) {
        runtime::World world(s);
        fx::ParticleSystem::Options o;
        o.max_particles = 20000;
        world.add_system(std::make_unique<fx::ParticleSystem>(o));
        std::size_t most = 0;
        for (int i = 0; i < frames; ++i) {
            world.frame(1.0 / 60.0, {});
            const auto st = world.system<fx::ParticleSystem>()->stats();
            most = std::max(most, st.particles);
            if (worst_ms && i > 60) *worst_ms = std::max(*worst_ms, st.step_ms);
        }
        return std::make_pair(world.system<fx::ParticleSystem>()->stats(), most);
    };
    double worst = 0.0;
    const auto [st, most] = play(300, &worst);  // 5 s
    CHECK(most <= 20000);         // never over the budget
    CHECK(st.particles >= 19000); // and it's used (they live 3 s, so it stays near full)
    CHECK(st.throttled > 0);      // some were held back
    MESSAGE("50 emitters, budget 20000: " << st.particles << " alive, " << st.throttled << " held back, worst step " << worst << " ms");
    const auto [again, most2] = play(300, nullptr);
    CHECK(again.fingerprint == st.fingerprint);  // the budget is shared out the same way every run
}
