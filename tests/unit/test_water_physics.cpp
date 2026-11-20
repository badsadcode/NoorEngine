// Water in the game (Milestone 8D, part 5, ADR-037): bodies float at the depth their density says, characters wade and
// swim, particles splash on the surface; part 6: rivers run, and carry what floats on them. The water here is a World water query of our own (a flat surface, or waves
// made of a sine), so each number can be worked out by hand.
#include "noor/fx/particle_system.h"
#include "noor/fx/particles.h"
#include "noor/game/water.h"
#include "noor/physics/physics.h"
#include "noor/runtime/world.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace noor;
using asset::Json;
using physics::PhysicsSystem;

namespace {

scene::Entity box(std::string name, math::Vec3 pos, math::Vec3 size, Json rigidbody = nullptr) {
    scene::Entity e;
    e.id = Guid::generate();
    e.name = std::move(name);
    e.transform.position = pos;
    e.components["collider"] = {{"shape", "box"}, {"size", {size.x, size.y, size.z}}};
    if (!rigidbody.is_null()) e.components["rigidbody"] = std::move(rigidbody);
    return e;
}

struct Recorder final : runtime::System {
    std::vector<runtime::Event> events;
    std::string_view name() const override { return "recorder"; }
    void late_update(runtime::World& w, double) override { events.insert(events.end(), w.events().begin(), w.events().end()); }
};

// Still water at y = 0 everywhere (or `waves` m of a 6 m sine travelling along +x).
runtime::World::WaterQuery water(float waves = 0.0f) {
    return [waves](const runtime::World& w, float x, float) -> std::optional<runtime::World::WaterSample> {
        const float k = 6.2831853f / 6.0f;
        const float t = static_cast<float>(w.time());
        runtime::World::WaterSample s;
        s.height = waves * std::sin(k * x - 2.0f * t);
        s.normal = math::normalize(math::Vec3{-waves * k * std::cos(k * x - 2.0f * t), 1.0f, 0.0f});
        return s;
    };
}

struct Sim {
    runtime::World world;
    PhysicsSystem* physics;
    Recorder* recorder;
    explicit Sim(scene::Scene s, float waves = 0.0f) : world(std::move(s)) {
        world.set_water_query(water(waves));
        auto p = std::make_unique<PhysicsSystem>(PhysicsSystem::Options{});
        physics = p.get();
        world.add_system(std::move(p));
        auto r = std::make_unique<Recorder>();
        recorder = r.get();
        world.add_system(std::move(r));
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) world.frame(1.0 / 60.0, {});
    }
    math::Vec3 pos(const Guid& id) { return world.find(id)->transform.position; }
};

} // namespace

TEST_CASE("water physics: planks float at the depth their density says, within a centimetre") {
    // Planks 1 x 0.4 x 1 m (flat enough to float level), dropped from a metre up. A plank of density rho floats with
    // rho / 1000 of its 0.4 m under: its middle at 0.2 - 0.4 rho / 1000.
    scene::Scene s;
    s.entities.push_back(box("Sea floor", {0, -6, 0}, {40, 1, 40}));
    const float densities[3] = {300.0f, 500.0f, 800.0f};
    for (int i = 0; i < 3; ++i)
        s.entities.push_back(box("Plank", {-3.0f + 3.0f * static_cast<float>(i), 1.0f, 0}, {1, 0.4f, 1},
                                 {{"type", "dynamic"}, {"mass", densities[i] * 0.4f}}));
    // Heavier than water sinks to the bottom; "buoyancy": 0 sinks whatever it weighs.
    s.entities.push_back(box("Iron", {0, 1, 4}, {1, 0.4f, 1}, {{"type", "dynamic"}, {"mass", 1200.0f * 0.4f}}));
    s.entities.push_back(box("Ballast", {3, 1, 4}, {1, 0.4f, 1}, {{"type", "dynamic"}, {"mass", 40.0f}, {"buoyancy", 0}}));
    const auto ids = [&] {
        std::vector<Guid> v;
        for (const auto& e : s.entities) v.push_back(e.id);
        return v;
    }();
    Sim sim(s);
    sim.run(20.0);
    for (int i = 0; i < 3; ++i) {
        const float expected = 0.2f - 0.4f * densities[i] / 1000.0f;
        CHECK(std::abs(sim.pos(ids[1 + i]).y - expected) < 0.001f);  // measured: under a micrometre
        CHECK(math::length(*sim.physics->velocity(ids[1 + i])) < 0.02f);
    }
    CHECK(sim.pos(ids[4]).y == doctest::Approx(-5.3f).epsilon(0.01));  // on the sea floor (its top at -5.5)
    CHECK(sim.pos(ids[5]).y == doctest::Approx(-5.3f).epsilon(0.01));
}

TEST_CASE("water physics: on waves a plank rides up and down, the same way every run") {
    const auto simulate = [] {
        scene::Scene s;
        s.entities.push_back(box("Sea floor", {0, -6, 0}, {40, 1, 40}));
        s.entities.push_back(box("Plank", {0, 0, 0}, {1, 0.4f, 1}, {{"type", "dynamic"}, {"mass", 200.0f}}));
        const Guid plank = s.entities[1].id;
        Sim sim(std::move(s), 0.3f);
        std::vector<math::Vec3> track;
        float low = 1e9f, high = -1e9f;
        for (int i = 0; i < 600; ++i) {
            sim.world.frame(1.0 / 60.0, {});
            const math::Vec3 p = sim.pos(plank);
            track.push_back(p);
            if (i > 240) {
                low = std::min(low, p.y);
                high = std::max(high, p.y);
            }
        }
        return std::make_tuple(track, low, high);
    };
    const auto [a, low, high] = simulate();
    const auto [b, low2, high2] = simulate();
    CHECK(high - low > 0.3f);  // riding the 0.6 m (crest to trough) waves, not sitting still
    CHECK(high < 0.4f);
    CHECK(low > -0.6f);
    REQUIRE(a.size() == b.size());
    bool same = true;
    for (std::size_t i = 0; i < a.size(); ++i) same = same && a[i].x == b[i].x && a[i].y == b[i].y && a[i].z == b[i].z;
    CHECK(same);  // to the bit
}

TEST_CASE("water physics: a character slows as it wades in and swims where it's deep") {
    // Dry ground (top at y = 0, the water's level) to z = -2, a shallow floor 0.6 m under the water to z = 6, then deep.
    scene::Scene s;
    s.entities.push_back(box("Beach", {0, -0.5f, -6}, {10, 1, 8}));
    s.entities.push_back(box("Shallows", {0, -1.1f, 2}, {10, 1, 8}));
    s.entities.push_back(box("Deep floor", {0, -10.5f, 20}, {10, 1, 30}));
    scene::Entity hero;
    hero.id = Guid::generate();
    hero.name = "Hero";
    hero.transform.position = {0, 0, -8};
    hero.components["character"] = {{"radius", 0.3}, {"height", 1.8}};
    s.entities.push_back(hero);
    Sim sim(s);
    const auto walk = [&](double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            sim.physics->character_move(hero.id, {0, 0, 4.0f});
            sim.world.frame(1.0 / 60.0, {});
        }
    };
    const auto speed_over = [&](double seconds) {
        const float z0 = sim.pos(hero.id).z;
        walk(seconds);
        return (sim.pos(hero.id).z - z0) / static_cast<float>(seconds);
    };
    walk(0.5);
    CHECK(speed_over(0.5) == doctest::Approx(4.0f).epsilon(0.03));  // dry: full speed
    CHECK(sim.physics->character_water(hero.id)->state == PhysicsSystem::WaterState::Dry);
    walk(1.5);  // down into the shallows
    auto w = *sim.physics->character_water(hero.id);
    CHECK(w.state == PhysicsSystem::WaterState::Wading);
    CHECK(w.feet_depth == doctest::Approx(0.6f).epsilon(0.03));
    // 0.6 m of a 1.17 m swimming depth: 1 - 0.5 x 0.6 / 1.17 of the speed.
    CHECK(speed_over(0.5) == doctest::Approx(4.0f * (1.0f - 0.5f * 0.6f / 1.17f)).epsilon(0.04));
    walk(3.0);  // off the shelf into deep water
    w = *sim.physics->character_water(hero.id);
    CHECK(w.state == PhysicsSystem::WaterState::Swimming);
    walk(2.0);
    CHECK(sim.pos(hero.id).y == doctest::Approx(-1.17f).epsilon(0.03));  // held with its chest at the surface
    CHECK(speed_over(0.5) == doctest::Approx(4.0f * 0.6f).epsilon(0.04));
    // The states it went through, as events for scripts: wading, then swimming (it may duck under on the way in).
    std::vector<std::string> states;
    for (const auto& e : sim.recorder->events)
        if (e.kind == runtime::Event::Kind::Water && e.entity == hero.id) states.push_back(e.name);
    REQUIRE(states.size() >= 2);
    CHECK(states.front() == "wading");
    CHECK(states.back() == "swimming");
}

TEST_CASE("water particles: drops end on the water and splash, exactly as many as fell") {
    // Ten drops falling onto water at y = 0: each ends on the surface and its "water" sub-emitter makes 3 particles.
    const Json ten = Json::parse(R"({"rate": 0, "bursts": [{"count": 10}], "lifetime": 5, "speed": 0, "gravity": 1,
        "shape": {"type": "box", "size": [1, 0, 1]}, "collide": {"ground": false, "water": true},
        "sub_emitters": [{"on": "water", "count": 3, "emitter": {"lifetime": 10, "speed": 0}},
                         {"on": "hit", "count": 5, "emitter": {"lifetime": 10, "speed": 0}}]})");
    const fx::GroundQuery still = [](float, float) -> std::optional<fx::GroundHit> { return fx::GroundHit{0.0f, {0, 1, 0}}; };
    fx::EmitterSet set;
    const Guid id = Guid::parse("5b417e00-0008-4000-8000-000000000001").value();
    const fx::EmitterSet::Input in{id, &ten, math::translation({0, 2, 0})};
    for (int i = 0; i < 120; ++i) set.step({&in, 1}, 1.0f / 60.0f, {0, 0, 0}, nullptr, 0xffffffffu, &still);
    REQUIRE(set.find(id) != nullptr);
    CHECK(set.find(id)->particles().empty());  // every drop ended on the water
    CHECK(set.find(id)->splashes() == 10);
    CHECK(set.stats().splashes == 10);
    const auto* splash = set.find(fx::sub_emitter_id(id, 0));
    REQUIRE(splash != nullptr);
    CHECK(splash->particles().size() == 30);
    for (const auto& p : splash->particles()) CHECK(std::abs(p.position.y) < 1e-4f);  // on the surface
    const auto* hits = set.find(fx::sub_emitter_id(id, 1));  // the water isn't a "hit": nothing from that one
    CHECK((hits == nullptr || hits->particles().empty()));
    // No water given (or "water": false): they fall straight through.
    fx::EmitterSet dry;
    for (int i = 0; i < 60; ++i) dry.step({&in, 1}, 1.0f / 60.0f, {0, 0, 0});
    CHECK(dry.find(id)->particles().size() == 10);
    CHECK(dry.stats().splashes == 0);
}

// ---- Rivers (part 6) --------------------------------------------------------------------------------------------

TEST_CASE("rivers: a course from the component's points, and where a point is on it") {
    // Straight down +x for 40 m, falling 2 m (0.05 m a metre), 6 m wide, then 2 m wide at the far end.
    const Json river = Json::parse(R"({"shape": "river", "points": [[0, 2, 0], [20, 1, 0], [40, 0, 0, 2]], "width": 6, "speed": 1.5})");
    const auto path = game::river_path(river, math::translation({10, 0, 5}));
    REQUIRE(path);
    CHECK(path->speed == 1.5f);
    REQUIRE(path->samples.size() >= 40);
    CHECK(path->samples.front().centre.x == 10.0f);  // the entity's place counts
    CHECK(path->samples.back().centre.x == 50.0f);
    CHECK(path->samples.back().along == doctest::Approx(40.0f).epsilon(1e-4));
    CHECK(path->samples[10].tangent.x == doctest::Approx(1.0f));
    CHECK(path->samples[10].slope == doctest::Approx(0.05f).epsilon(1e-3));
    CHECK(path->samples.front().half_width == 3.0f);
    CHECK(path->samples.back().half_width == 1.0f);
    // On it: 10 m along, 2 m to the left (+z is left of +x), the surface 1.5 m up.
    const auto at = game::river_at(*path, {20.0f, 7.0f});
    REQUIRE(at);
    CHECK(at->height == doctest::Approx(1.5f).epsilon(1e-3));
    CHECK(at->along == doctest::Approx(10.0f).epsilon(1e-3));
    CHECK(at->across == doctest::Approx(2.0f).epsilon(1e-3));
    CHECK(at->tangent.x == doctest::Approx(1.0f));
    CHECK(!game::river_at(*path, {20.0f, 9.0f}));   // 4 m out: past the bank (3 m)
    CHECK(!game::river_at(*path, {49.0f, 6.5f}));   // near the narrow end, 1.5 m out of 1
    CHECK(!game::river_at(*path, {70.0f, 5.0f}));   // beyond its end
    // Fewer than two points: no river.
    CHECK(!game::river_path(Json::parse(R"({"shape": "river", "points": [[0, 0, 0]]})"), math::Mat4::identity()));
}

TEST_CASE("rivers: a floating crate is carried downstream at the river's speed, and swimmers drift") {
    scene::Scene s;
    s.entities.push_back(box("River bed", {0, -6, 0}, {200, 1, 40}));
    scene::Entity river;
    river.id = Guid::generate();
    river.name = "River";
    river.components["water"] = Json::parse(R"({"shape": "river", "points": [[-80, 0, 0], [80, 0, 0]], "width": 12, "speed": 1.5})");
    s.entities.push_back(river);
    s.entities.push_back(box("Crate", {-60, 0.5f, 0}, {1, 0.4f, 1}, {{"type", "dynamic"}, {"mass", 200.0f}}));
    const Guid crate = s.entities.back().id;
    scene::Entity hero;
    hero.id = Guid::generate();
    hero.name = "Swimmer";
    hero.transform.position = {-60, -1.2f, 4};
    hero.components["character"] = {{"radius", 0.3}, {"height", 1.8}};
    s.entities.push_back(hero);
    runtime::World world(std::move(s));
    world.add_system(std::make_unique<game::WaterSystem>(game::AssetPath{}));
    auto physics_system = std::make_unique<PhysicsSystem>(PhysicsSystem::Options{});
    PhysicsSystem* physics = physics_system.get();
    world.add_system(std::move(physics_system));
    // Through the World, scripts see the current too.
    world.frame(1.0 / 60.0, {});
    const auto here = world.water_at(0.0f, 0.0f);
    REQUIRE(here);
    CHECK(here->flow.x == doctest::Approx(1.5f));
    CHECK(here->flow.z == doctest::Approx(0.0f).scale(1.0));
    for (int i = 0; i < 600; ++i) world.frame(1.0 / 60.0, {});
    // Ten seconds on: drifting with the water (the drag has had time to match it), floating, still mid-river.
    const auto v = *physics->velocity(crate);
    CHECK(v.x == doctest::Approx(1.5f).epsilon(0.02));
    CHECK(std::abs(v.z) < 0.02f);
    const math::Vec3 p = world.find(crate)->transform.position;
    CHECK(p.x > -60.0f + 1.5f * 8.0f);
    CHECK(p.y == doctest::Approx(0.0f).scale(1.0).epsilon(0.01));
    CHECK(std::abs(p.z) < 0.1f);
    // The swimmer (not steering) drifts at the current's speed too.
    const float x0 = world.find(hero.id)->transform.position.x;
    for (int i = 0; i < 60; ++i) world.frame(1.0 / 60.0, {});
    CHECK(physics->character_water(hero.id)->state == PhysicsSystem::WaterState::Swimming);
    CHECK(world.find(hero.id)->transform.position.x - x0 == doctest::Approx(1.5f).epsilon(0.03));
}
