// runtime::World (ADR-027): frame order, fixed step, lifecycle, input actions, built-in behaviours.
#include "noor/runtime/behaviours.h"
#include "noor/runtime/input.h"
#include "noor/runtime/world.h"

#include <ostream>
#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace noor;
using namespace noor::runtime;

namespace {

scene::Entity entity(std::string name, std::optional<Guid> parent = std::nullopt) {
    scene::Entity e;
    e.id = Guid::generate();
    e.name = std::move(name);
    e.parent = parent;
    return e;
}

// Records every call so tests can check the order.
struct Recorder final : System {
    std::vector<std::string>* log;
    explicit Recorder(std::vector<std::string>* l) : log(l) {}
    std::string_view name() const override { return "recorder"; }
    void entity_started(World&, scene::Entity& e) override { log->push_back("start " + e.name); }
    void update(World&, double) override { log->push_back("update"); }
    void fixed_update(World&, double) override { log->push_back("fixed"); }
    void late_update(World&, double) override { log->push_back("late"); }
    void entity_destroyed(World&, const scene::Entity& e) override { log->push_back("destroy " + e.name); }
};

} // namespace

TEST_CASE("runtime: frame order is start, update, fixed steps, late, destroy") {
    scene::Scene s;
    s.entities = {entity("a")};
    World w(s);
    std::vector<std::string> log;
    w.add_system(std::make_unique<Recorder>(&log));

    w.frame(1.0 / 30.0, {});  // two fixed steps at 60 Hz
    CHECK(log == std::vector<std::string>{"start a", "update", "fixed", "fixed", "late"});
    log.clear();
    w.frame(1.0 / 30.0, {});
    CHECK(log == std::vector<std::string>{"update", "fixed", "fixed", "late"});  // started only once
}

TEST_CASE("runtime: the fixed step accumulates time and caps runaway frames") {
    World w(scene::Scene{});
    w.frame(0.01, {});
    CHECK(w.fixed_step_count() == 0);  // 10 ms < 16.7 ms
    w.frame(0.01, {});
    CHECK(w.fixed_step_count() == 1);  // 20 ms
    CHECK(w.fixed_alpha() == doctest::Approx((0.02 - 1.0 / 60.0) * 60.0));
    w.frame(5.0, {});  // a 5 s hitch is capped at max_dt (0.1 s): 6 steps due, 5 allowed
    CHECK(w.fixed_step_count() == 6);
    CHECK(w.time() == doctest::Approx(0.12));
}

TEST_CASE("runtime: spawned entities start next frame, destroy removes whole subtrees at frame end") {
    scene::Scene s;
    auto parent = entity("parent");
    auto child = entity("child", parent.id);
    auto other = entity("other");
    s.entities = {parent, child, other};
    World w(s);
    std::vector<std::string> log;
    w.add_system(std::make_unique<Recorder>(&log));
    w.frame(0.0, {});
    log.clear();

    w.spawn(entity("late"));
    w.destroy(parent.id);
    CHECK(w.find(parent.id) != nullptr);  // still there until the frame ends
    w.frame(0.0, {});
    CHECK(log == std::vector<std::string>{"start late", "update", "late", "destroy parent", "destroy child"});
    CHECK(w.find(parent.id) == nullptr);
    CHECK(w.find(child.id) == nullptr);
    CHECK(w.find(other.id) != nullptr);
    CHECK(w.scene().entities.size() == 2);
}

TEST_CASE("runtime input: actions report down, pressed and released edges") {
    Input input;
    InputState s;
    s.set(Key::Space);
    input.begin_frame(s);
    CHECK(input.down("jump"));
    CHECK(input.pressed("jump"));
    input.begin_frame(s);
    CHECK(input.down("jump"));
    CHECK_FALSE(input.pressed("jump"));
    input.begin_frame({});
    CHECK(input.released("jump"));
    CHECK_FALSE(input.down("jump"));
    CHECK_FALSE(input.down("no such action"));
}

TEST_CASE("runtime input: axes combine keys, look axes are mouse movement") {
    Input input;
    InputState s;
    s.set(Key::D);
    s.set(Key::W);
    s.mouse_delta = {12, -3};
    input.begin_frame(s);
    CHECK(input.axis("move_x") == 1.0f);
    CHECK(input.axis("move_y") == 1.0f);
    CHECK(input.axis("look_x") == 12.0f);
    s.set(Key::A);  // both directions cancel
    input.begin_frame(s);
    CHECK(input.axis("move_x") == 0.0f);
}

TEST_CASE("runtime input: input.json overrides defaults and rejects unknown keys") {
    auto map = InputMap::from_json(asset::Json::parse(R"({"actions": {"jump": ["J", "space"]},
        "axes": {"turn": {"negative": ["Q"], "positive": ["E"]}}})"));
    REQUIRE(map);
    CHECK(map->actions["jump"] == std::vector<Key>{Key::J, Key::Space});
    CHECK(map->actions.contains("fire"));  // defaults kept
    CHECK(map->axes["turn"].positive == std::vector<Key>{Key::E});
    CHECK_FALSE(InputMap::from_json(asset::Json::parse(R"({"actions": {"jump": ["Hyperdrive"]}})")));
    CHECK(key_from_name("mouseleft") == Key::MouseLeft);
    CHECK(key_name(Key::Num7) == "7");
    CHECK(InputMap::load("definitely/missing/input.json"));  // missing file: defaults
}

TEST_CASE("runtime behaviours: rotator turns, input_mover follows the move axes") {
    scene::Scene s;
    auto spinner = entity("spinner");
    spinner.components["rotator"] = {{"axis", {0, 1, 0}}, {"degrees_per_second", 90}};
    auto walker = entity("walker");
    walker.components["input_mover"] = {{"speed", 4}};
    s.entities = {spinner, walker};
    World w(s);
    w.add_system(std::make_unique<BehavioursSystem>());

    InputState keys;
    keys.set(Key::W);
    keys.set(Key::D);
    for (int i = 0; i < 10; ++i) w.frame(0.1, keys);  // 1 s

    const auto x = math::rotate(w.find(spinner.id)->transform.rotation, {1, 0, 0});
    CHECK(x.z == doctest::Approx(-1.0f).epsilon(1e-4));  // +90 degrees about Y: +X -> -Z
    const auto p = w.find(walker.id)->transform.position;
    CHECK(p.x == doctest::Approx(4.0f).epsilon(1e-4));   // D: +X
    CHECK(p.z == doctest::Approx(-4.0f).epsilon(1e-4));  // W: forward = -Z
}

TEST_CASE("runtime: script systems start an entity after the engine systems prepared it") {
    std::vector<std::string> log;
    struct Sys final : System {
        std::vector<std::string>* log;
        std::string label;
        bool late;
        Sys(std::vector<std::string>* l, std::string n, bool lt) : log(l), label(std::move(n)), late(lt) {}
        std::string_view name() const override { return label; }
        bool starts_late() const override { return late; }
        void entity_started(World&, scene::Entity&) override { log->push_back(label); }
    };
    scene::Scene s;
    s.entities = {entity("a")};
    World w(s);
    w.add_system(std::make_unique<Sys>(&log, "script", true));
    w.add_system(std::make_unique<Sys>(&log, "audio", false));
    w.frame(0.0, {});
    CHECK(log == std::vector<std::string>{"audio", "script"});
}
