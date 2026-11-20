// Animation clips (Milestone 8C, part 2, ADR-036): clips from glTF and the sampler, against values worked out by hand.
// Ward_Anim's clips (tools/samples/make_test_characters.py) each test one interpolation; the CC0 robot is a real file.
// Then the runtime: an animator on a placed Ward poses its bone entities, scripts play clips and hear them end.
#include "noor/asset/animation.h"
#include "noor/asset/gltf_import.h"
#include "noor/game/animator.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scene/scene.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>

using namespace noor;

namespace {

const std::filesystem::path kData = std::filesystem::path(NOOR_TEST_DATA_DIR) / "animation";
constexpr float kPi = 3.14159265358979f;

asset::ImportedModel ward() {
    auto m = asset::import_gltf(kData / "Ward" / "Ward_Anim.glb");
    REQUIRE(m.has_value());
    return *m;
}

int node(const asset::ImportedModel& m, const char* name) {
    for (std::size_t i = 0; i < m.nodes.size(); ++i)
        if (m.nodes[i].name == name) return static_cast<int>(i);
    FAIL("no node " << name);
    return -1;
}

math::Quat about(math::Vec3 axis, float degrees) { return math::Quat::from_axis_angle(axis, degrees * kPi / 180.0f); }

bool same(math::Quat a, math::Quat b, float eps = 1e-5f) {
    const float d = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);  // q and -q are the same turn
    return d > 1.0f - eps;
}

asset::NodePose at(const asset::ImportedModel& m, const char* clip, double t, const char* bone) {
    auto pose = asset::rest_pose(m);
    const auto* c = asset::find_animation(m, clip);
    REQUIRE(c != nullptr);
    asset::sample(*c, t, pose);
    return pose[static_cast<std::size_t>(node(m, bone))];
}

} // namespace

TEST_CASE("animation: Ward's clips come in with their channels, keys and lengths") {
    const auto m = ward();
    REQUIRE(m.animations.size() == 4);
    CHECK(asset::find_animation(m, "Walk")->channels.size() == 7);
    CHECK(asset::find_animation(m, "Walk")->duration == doctest::Approx(1.0f));
    CHECK(asset::find_animation(m, "Glide")->channels[0].interpolation == asset::AnimationChannel::Interpolation::CubicSpline);
    CHECK(asset::find_animation(m, "Glide")->channels[0].values.size() == 2 * 3 * 3);  // two keys of in, value, out
    CHECK(asset::find_animation(m, "Nod")->channels[0].interpolation == asset::AnimationChannel::Interpolation::Step);
    CHECK(asset::find_animation(m, "nothing") == nullptr);
    CHECK(m.warnings.empty());
}

TEST_CASE("animation: LINEAR rotation is spherical, halfway is half the angle, and the ends hold") {
    const auto m = ward();
    CHECK(same(at(m, "Wave", 0.0, "DEF-upper_arm.R").rotation, about({0, 0, 1}, 0)));
    CHECK(same(at(m, "Wave", 0.5, "DEF-upper_arm.R").rotation, about({0, 0, 1}, 45)));
    CHECK(same(at(m, "Wave", 0.25, "DEF-upper_arm.R").rotation, about({0, 0, 1}, 22.5f)));  // spherical: even in angle
    CHECK(same(at(m, "Wave", 1.0, "DEF-upper_arm.R").rotation, about({0, 0, 1}, 90)));
    CHECK(same(at(m, "Wave", 7.0, "DEF-upper_arm.R").rotation, about({0, 0, 1}, 90)));      // after the last key: held
    CHECK(same(at(m, "Wave", -1.0, "DEF-upper_arm.R").rotation, about({0, 0, 1}, 0)));      // before the first: held
    // Nodes the clip doesn't touch keep their rest pose.
    CHECK(at(m, "Wave", 0.5, "DEF-head").translation.y == doctest::Approx(m.nodes[static_cast<std::size_t>(node(m, "DEF-head"))].translation.y));
}

TEST_CASE("animation: STEP holds each key until the next") {
    const auto m = ward();
    CHECK(same(at(m, "Nod", 0.49, "DEF-head").rotation, about({0, 1, 0}, 0)));
    CHECK(same(at(m, "Nod", 0.5, "DEF-head").rotation, about({0, 1, 0}, 30)));
    CHECK(same(at(m, "Nod", 0.9, "DEF-head").rotation, about({0, 1, 0}, 30)));
}

TEST_CASE("animation: CUBICSPLINE follows glTF's Hermite curve through the keys and tangents") {
    const auto m = ward();
    // From z 0 at 2 m/s to z 1 standing still, over 1 s: z(u) = h10(u) * 2 + h01(u).
    CHECK(at(m, "Glide", 0.0, "DEF-hips").translation.z == doctest::Approx(0.0f));
    CHECK(at(m, "Glide", 0.25, "DEF-hips").translation.z == doctest::Approx(0.4375f).epsilon(1e-5));
    CHECK(at(m, "Glide", 0.5, "DEF-hips").translation.z == doctest::Approx(0.75f).epsilon(1e-5));
    CHECK(at(m, "Glide", 1.0, "DEF-hips").translation.z == doctest::Approx(1.0f));
    CHECK(at(m, "Glide", 0.5, "DEF-hips").translation.y == doctest::Approx(0.95f));  // no tangent in y: stays
}

TEST_CASE("animation: a looping clip's first and last keys agree, and the shorter way round is taken") {
    const auto m = ward();
    const auto a = at(m, "Walk", 0.0, "DEF-thigh.L"), b = at(m, "Walk", 1.0, "DEF-thigh.L");
    CHECK(same(a.rotation, b.rotation));
    CHECK(at(m, "Walk", 0.25, "DEF-hips").translation.y == doctest::Approx(0.93f));
    // slerp from q to -q (the same turn written both ways) doesn't spin all the way round: it stays put.
    const auto q = about({0, 1, 0}, 40);
    const auto half = asset::slerp(q, {-q.x, -q.y, -q.z, -q.w}, 0.5f);
    CHECK(same(half, q));
    CHECK(same(asset::slerp(about({1, 0, 0}, 10), about({1, 0, 0}, 50), 0.5f), about({1, 0, 0}, 30)));
}

TEST_CASE("animation: the robot's 14 clips come in, its face morphs skipped with a word, every pose sound") {
    auto m = asset::import_gltf(kData / "RobotExpressive" / "RobotExpressive.glb");
    REQUIRE(m.has_value());
    CHECK(m->animations.size() == 14);
    const auto* walking = asset::find_animation(*m, "Walking");
    REQUIRE(walking != nullptr);
    CHECK(walking->duration == doctest::Approx(0.9583f).epsilon(0.01));
    bool told = false;
    for (const auto& w : m->warnings) told |= w.find("morph-target") != std::string::npos;
    CHECK(told);
    auto pose = asset::rest_pose(*m);
    for (double t = 0.0; t <= walking->duration; t += 0.05) {
        asset::sample(*walking, t, pose);
        for (const auto& p : pose) {
            const float len = std::sqrt(p.rotation.x * p.rotation.x + p.rotation.y * p.rotation.y + p.rotation.z * p.rotation.z + p.rotation.w * p.rotation.w);
            REQUIRE(len == doctest::Approx(1.0f).epsilon(1e-4));
            REQUIRE(std::isfinite(p.translation.x));
        }
    }
}

namespace {

// Ward placed in a world with an animator system that finds his file.
struct Placed {
    std::unique_ptr<runtime::World> world;
    Guid root;
    Guid bone(const char* name) const {
        for (const auto& e : world->scene().entities)
            if (e.name == name) return e.id;
        FAIL("no entity " << name);
        return {};
    }
    math::Quat rotation(const char* name) const { return world->find(bone(name))->transform.rotation; }
};

Placed place_ward(asset::Json animator, bool drop_node_list = false) {
    const auto m = ward();
    const Guid asset_id = Guid::generate();
    scene::Scene s;
    const Guid root = scene::instantiate_model(s, asset_id, m, "Ward", {});
    auto* r = s.find(root);
    REQUIRE(r != nullptr);
    CHECK(r->components.at("model")["nodes"].size() == m.nodes.size());
    if (drop_node_list) const_cast<scene::Entity*>(r)->components["model"].erase("nodes");
    if (!animator.is_null()) const_cast<scene::Entity*>(r)->components["animator"] = std::move(animator);
    Placed p{std::make_unique<runtime::World>(std::move(s)), root};
    p.world->add_system(std::make_unique<game::AnimatorSystem>([asset_id](const Guid& id) -> std::optional<std::filesystem::path> {
        if (id == asset_id) return kData / "Ward" / "Ward_Anim.glb";
        return std::nullopt;
    }));
    return p;
}

} // namespace

TEST_CASE("animator: a placed Ward's bones follow the clip on the scene's clock, by node list or by name") {
    for (const bool by_name : {false, true}) {
        CAPTURE(by_name);
        auto p = place_ward({{"animation", "Wave"}, {"loop", false}}, by_name);
        for (int i = 0; i < 30; ++i) p.world->frame(1.0 / 60.0, {});  // 0.5 s
        CHECK(p.world->time() == doctest::Approx(0.5));
        CHECK(same(p.rotation("DEF-upper_arm.R"), about({0, 0, 1}, 45)));
        // Every bone any of Ward's clips moves is posed (8), so changing clip puts the last one's bones back to rest;
        // the others (the mesh, his spine, hands...) are left alone.
        CHECK(p.world->system<game::AnimatorSystem>()->stats().nodes == 8);
        for (int i = 0; i < 60; ++i) p.world->frame(1.0 / 60.0, {});
        CHECK(same(p.rotation("DEF-upper_arm.R"), about({0, 0, 1}, 90)));  // not looping: holds the end
        CHECK(p.world->system<game::AnimatorSystem>()->stats().ended == 0);   // the scene's clock sends no end events
    }
}

TEST_CASE("animator: looping wraps, speed and time_offset shift the clock, equal runs give equal fingerprints") {
    auto t = [](asset::Json c, double now) { return game::animator_time(c, now, 1.0); };
    CHECK(t({{"animation", "Walk"}}, 2.25) == doctest::Approx(0.25));
    CHECK(t({{"animation", "Walk"}, {"speed", 2.0}}, 0.75) == doctest::Approx(0.5));
    CHECK(t({{"animation", "Walk"}, {"time_offset", -0.25}}, 0.0) == doctest::Approx(0.75));  // wraps backwards too
    CHECK(t({{"animation", "Walk"}, {"loop", false}}, 3.0) == doctest::Approx(1.0));
    std::uint64_t prints[2]{};
    for (auto& f : prints) {
        auto p = place_ward({{"animation", "Walk"}, {"time_offset", 0.3}});
        for (int i = 0; i < 47; ++i) p.world->frame(1.0 / 60.0, {});
        f = p.world->system<game::AnimatorSystem>()->stats().fingerprint;
        CHECK(p.world->system<game::AnimatorSystem>()->stats().nodes == 8);
    }
    CHECK(prints[0] == prints[1]);
}

TEST_CASE("animator: a script plays clips, changes speed, and hears a one-shot end once") {
    auto p = place_ward({{"animation", "Walk"}});
    p.world->find(p.root)->components["script"] = {{"file", "Scripts/ward.luau"}};
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/ward.luau", R"(
        local Ward = {}
        function Ward:on_update(dt)
            self.t = (self.t or 0) + dt
            if self.t >= 0.25 and not self.waved then
                self.waved = true
                self.entity:set("animator", { animation = "Walk", loop = false })
                self.entity:play_animation("Wave", 2)
            end
        end
        function Ward:on_animation_end(name)
            self.ends = (self.ends or 0) + 1
            self.entity:set("story", { ended = name, count = self.ends, at = noor.time(), now = self.entity:animation() })
        end
        return Ward
    )");
    auto* scripts = lua.get();
    p.world->add_system(std::move(lua));
    for (int i = 0; i < 120; ++i) p.world->frame(1.0 / 60.0, {});  // 2 s
    CHECK(scripts->errors().empty());
    const auto& root = *p.world->find(p.root);
    REQUIRE(root.components.contains("story"));
    CHECK(root.components.at("story")["ended"] == "Wave");
    CHECK(root.components.at("story")["count"] == 1);
    // Played at ~0.25 s at double speed: a 1 s clip ends half a second later (give or take the frame it's noticed in).
    CHECK(root.components.at("story")["at"].get<double>() == doctest::Approx(0.75).epsilon(0.05));
    CHECK(same(p.rotation("DEF-upper_arm.R"), about({0, 0, 1}, 90)));
    CHECK(p.world->system<game::AnimatorSystem>()->stats().ended == 1);
    // Stop freezes the pose where it is; speed 0 does the same.
    runtime::api::play_animation(*p.world, p.root, "Wave", 1.0f);
    for (int i = 0; i < 15; ++i) p.world->frame(1.0 / 60.0, {});
    runtime::api::stop_animation(*p.world, p.root);
    const auto frozen = p.rotation("DEF-upper_arm.R");
    for (int i = 0; i < 15; ++i) p.world->frame(1.0 / 60.0, {});
    CHECK(same(p.rotation("DEF-upper_arm.R"), frozen));
    CHECK(runtime::api::current_animation(*p.world, p.root) == "Wave");
}
