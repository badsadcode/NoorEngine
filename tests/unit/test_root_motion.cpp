// Root motion, animation events and bones for scripts (Milestone 8C, part 4, ADR-036): the root's movement worked out
// by hand (whole cycles across the loop, clamped one-shots), a walk covering its clip's distance (within a centimetre)
// whichever way the character faces, through a character controller that a wall stops, events heard by a script the
// right number of times, and a prop on a hand bone. Ward_Anim's WalkForward moves his hips 1.4 m per 1 s cycle.
#include "noor/asset/anim_controller.h"
#include "noor/asset/animation.h"
#include "noor/asset/gltf_import.h"
#include "noor/game/animator.h"
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scene/scene.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace noor;
using asset::Json;

namespace {

const std::filesystem::path kChars = std::filesystem::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Characters";
constexpr double kDt = 1.0 / 60.0;

const asset::ImportedModel& ward() {
    static const asset::ImportedModel m = [] {
        auto r = asset::import_gltf(kChars / "Ward_Anim.glb");
        REQUIRE(r.has_value());
        return *r;
    }();
    return m;
}

struct Placed {
    std::unique_ptr<runtime::World> world;
    Guid model, player;
    math::Vec3 position(const Guid& id) const { return *runtime::api::world_position(*world, id); }
};

// Ward with `animator`, optionally inside a Player with a character controller on a floor (and a wall at `wall_z`),
// with scripts first and physics before the animator, as game::make_world orders them.
Placed place(Json animator, bool character, float turn_degrees = 0.0f, std::optional<float> wall_z = std::nullopt,
             std::unique_ptr<runtime::System> scripts = nullptr, std::string controller_json = {}) {
    const Guid model_id = Guid::generate(), controller_id = Guid::generate();
    scene::Scene s;
    Placed p;
    const math::Quat turn = math::Quat::from_axis_angle({0, 1, 0}, turn_degrees * 3.14159265f / 180.0f);
    if (character) {
        scene::Entity floor;
        floor.id = Guid::generate();
        floor.name = "Floor";
        floor.transform.position = {0, -0.5f, 0};
        floor.components["collider"] = {{"shape", "box"}, {"size", {40, 1, 40}}};
        s.entities.push_back(floor);
        if (wall_z) {
            scene::Entity wall;
            wall.id = Guid::generate();
            wall.name = "Wall";
            wall.transform.position = {0, 1, *wall_z + 0.1f};
            wall.components["collider"] = {{"shape", "box"}, {"size", {6, 2, 0.2}}};
            s.entities.push_back(wall);
        }
        scene::Entity player;
        player.id = Guid::generate();
        player.name = "Player";
        player.transform.position = {0, 0.05f, 0};
        player.transform.rotation = turn;
        player.components["character"] = {{"radius", 0.3}, {"height", 1.8}, {"step_height", 0.4}};
        p.player = player.id;
        s.entities.push_back(player);
    }
    p.model = scene::instantiate_model(s, model_id, ward(), "Ward", {});
    if (!controller_json.empty()) animator["controller"] = controller_id.to_string();
    for (auto& e : s.entities)
        if (e.id == p.model) {
            e.components["animator"] = std::move(animator);
            if (character) e.parent = p.player;
            else e.transform.rotation = turn;
        }
    std::filesystem::path controller_file;
    if (!controller_json.empty()) {
        controller_file = std::filesystem::temp_directory_path() / ("noor_root_motion_" + controller_id.to_string() + ".nooranim");
        std::ofstream(controller_file) << controller_json;
    }
    p.world = std::make_unique<runtime::World>(std::move(s));
    if (scripts) p.world->add_system(std::move(scripts));
    if (character) p.world->add_system(std::make_unique<physics::PhysicsSystem>(physics::PhysicsSystem::Options{}));
    p.world->add_system(std::make_unique<game::AnimatorSystem>([=](const Guid& id) -> std::optional<std::filesystem::path> {
        if (id == model_id) return kChars / "Ward_Anim.glb";
        if (id == controller_id) return controller_file;
        return std::nullopt;
    }));
    return p;
}

void frames(Placed& p, int n) {
    for (int i = 0; i < n; ++i) p.world->frame(kDt, {});
}

} // namespace

TEST_CASE("root motion: the root bone, and how far it moves, worked out by hand") {
    const auto& m = ward();
    const int hips = asset::root_motion_node(m);
    REQUIRE(hips >= 0);
    CHECK(m.nodes[static_cast<std::size_t>(hips)].name == "DEF-hips");
    const auto rest = asset::rest_pose(m)[static_cast<std::size_t>(hips)];
    const auto* walk = asset::find_animation(m, "WalkForward");
    REQUIRE(walk != nullptr);
    auto z = [&](double t0, double t1, bool loop) { return asset::root_delta(*walk, hips, rest, t0, t1, loop).z; };
    CHECK(z(0.0, 1.0, true) == doctest::Approx(1.4f));
    CHECK(z(0.5, 1.5, true) == doctest::Approx(1.4f));     // across the loop: no jump back
    CHECK(z(0.0, 2.25, true) == doctest::Approx(3.15f));   // two cycles and a quarter
    CHECK(z(0.9, 1.1, true) == doctest::Approx(0.28f));
    CHECK(asset::root_delta(*walk, hips, rest, 0.0, 1.0, true).y == doctest::Approx(0.0f));  // the bob comes back up
    const auto* glide = asset::find_animation(m, "Glide");  // one-shot: 1 m, then it stays
    CHECK(asset::root_delta(*glide, hips, rest, 0.0, 5.0, false).z == doctest::Approx(1.0f));
    CHECK(asset::root_delta(*glide, hips, rest, 2.0, 5.0, false).z == doctest::Approx(0.0f));

    CHECK(asset::crossings(0.0, 1.0, 0.25, 1.0, true) == 1);
    CHECK(asset::crossings(0.0, 2.3, 0.25, 1.0, true) == 3);
    CHECK(asset::crossings(0.25, 0.5, 0.25, 1.0, true) == 0);  // after t0, up to and including t1
    CHECK(asset::crossings(0.0, 0.25, 0.25, 1.0, true) == 1);
    CHECK(asset::crossings(0.0, 5.0, 0.5, 1.0, false) == 1);   // a one-shot passes it once
    CHECK(asset::crossings(0.6, 5.0, 0.5, 1.0, false) == 0);
}

TEST_CASE("root motion: a walk covers its clip's distance, within a centimetre, the way the model faces") {
    for (const float turn : {0.0f, 90.0f}) {
        CAPTURE(turn);
        auto p = place({{"animation", "WalkForward"}, {"root_motion", true}}, false, turn);
        frames(p, 150);  // 2.5 s at 1.4 m/s: 3.5 m
        const auto at = p.position(p.model);
        if (turn == 0.0f) {
            CHECK(std::abs(at.z - 3.5f) < 0.01f);
            CHECK(std::abs(at.x) < 0.01f);
        } else {  // turned a quarter left: its forward (+Z) is the world's +X
            CHECK(std::abs(at.x - 3.5f) < 0.01f);
            CHECK(std::abs(at.z) < 0.01f);
        }
        // The hips stay over the model's origin (their movement went to the model), bobbing up and down as authored.
        const auto hips = *runtime::api::bone(*p.world, p.model, "DEF-hips");
        CHECK(std::abs(p.world->find(hips)->transform.position.z) < 1e-6f);
    }
    // Without root motion the clip moves the hips, and pops back every loop: the model stays where it is.
    auto still = place({{"animation", "WalkForward"}}, false);
    frames(still, 150);
    CHECK(still.position(still.model).z == doctest::Approx(0.0f));
}

TEST_CASE("root motion: through a character controller, the walk covers its distance, and a wall stops it") {
    auto open = place({{"animation", "WalkForward"}, {"root_motion", true}}, true);
    frames(open, 180);  // 3 s: 4.2 m
    CHECK(std::abs(open.position(open.player).z - 4.2f) < 0.01f);
    CHECK(std::abs(open.position(open.model).z - 4.2f) < 0.01f);  // the model rides along inside it

    auto blocked = place({{"animation", "WalkForward"}, {"root_motion", true}}, true, 0.0f, 2.5f);
    frames(blocked, 240);
    const float z = blocked.position(blocked.player).z;
    CHECK(z < 2.5f - 0.3f + 0.02f);  // its capsule (radius 0.3) against the wall's face
    CHECK(z > 2.0f);                 // having walked right up to it
}

TEST_CASE("animation events: a script hears each named moment each time it's passed") {
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/steps.luau", R"(
        local Steps = {}
        function Steps:on_animation_event(name)
            self.count = self.count or {}
            self.count[name] = (self.count[name] or 0) + 1
            self.entity:set("steps", { left = self.count.left or 0, right = self.count.right or 0, step = self.count.step or 0 })
        end
        return Steps
    )");
    auto* scripts = lua.get();
    auto p = place({{"animation", "WalkForward"},
                    {"events", Json::array({{{"clip", "WalkForward"}, {"at", 0.25}, {"name", "left"}},
                                            {{"clip", "WalkForward"}, {"at", 0.75}, {"name", "right"}},
                                            {{"clip", "Walk"}, {"at", 0.5}, {"name", "step"}}})}},
                   false, 0.0f, std::nullopt, std::move(lua));
    p.world->find(p.model)->components["script"] = {{"file", "Scripts/steps.luau"}};
    frames(p, 120);  // 2 s: left at 0.25 and 1.25, right at 0.75 and 1.75; the Walk clip's event isn't this clip's
    CHECK(scripts->errors().empty());
    const auto& steps = p.world->find(p.model)->components.at("steps");
    CHECK(steps["left"] == 2);
    CHECK(steps["right"] == 2);
    CHECK(steps["step"] == 0);
}

TEST_CASE("root motion and events through a controller, and a prop on a hand bone follows the hand") {
    const std::string controller = R"({"noor_anim": 1, "parameters": {"go": {"type": "float", "default": 1}},
        "layers": [{"name": "Base", "entry": "Walk", "states": [
            {"name": "Walk", "clip": "WalkForward", "speed": "go", "events": [{"at": 0.5, "name": "step"}]}]}]})";
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/steps.luau", R"(
        local Steps = {}
        function Steps:on_start()
            local hand = self.entity:bone("DEF-hand.R")
            self.entity:set("hand", { found = hand ~= nil })
        end
        function Steps:on_animation_event(name)
            self.steps = (self.steps or 0) + 1
            self.entity:set("steps", { count = self.steps })
        end
        return Steps
    )");
    auto p = place({{"root_motion", true}, {"parameters", {{"go", 2.0}}}}, false, 0.0f, std::nullopt, std::move(lua), controller);
    p.world->find(p.model)->components["script"] = {{"file", "Scripts/steps.luau"}};
    // A sword in his right hand: an entity parented to the hand's bone, 10 cm along it.
    const auto hand = runtime::api::bone(*p.world, p.model, "DEF-hand.R");
    REQUIRE(hand.has_value());
    scene::Entity sword;
    sword.id = Guid::generate();
    sword.name = "Sword";
    sword.parent = *hand;
    sword.transform.position = {-0.1f, 0, 0};
    const Guid sword_id = sword.id;
    p.world->spawn(std::move(sword));
    frames(p, 60);  // 1 s at double speed: two cycles, 2.8 m, two steps
    CHECK(std::abs(p.position(p.model).z - 2.8f) < 0.01f);
    CHECK(p.world->find(p.model)->components.at("steps")["count"] == 2);
    CHECK(p.world->find(p.model)->components.at("hand")["found"] == true);
    const auto h = *runtime::api::world_matrix(*p.world, *hand);
    const auto expect = h.transform_point({-0.1f, 0, 0});
    const auto got = p.position(sword_id);
    CHECK(got.x == doctest::Approx(expect.x));
    CHECK(got.y == doctest::Approx(expect.y));
    CHECK(got.z == doctest::Approx(expect.z));
    CHECK(got.z > 2.5f);  // it went along with him
}
