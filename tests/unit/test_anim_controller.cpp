// Blending and the animation controller (Milestone 8C, part 3, ADR-036): the blend maths against values worked out by
// hand, .nooranim parsing and its errors, transitions exact to the frame, masked and additive layers, blend spaces
// (1D and 2D), cross-fades from scripts, and the same steps giving the same poses bit for bit. Ward_Anim's clips and
// Ward.nooranim (tests/data/render_scenes/Assets/Characters) are the test character and controller.
#include "noor/asset/anim_controller.h"
#include "noor/asset/animation.h"
#include "noor/asset/gltf_import.h"
#include "noor/asset/pose_blend.h"
#include "noor/game/animator.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scene/scene.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <filesystem>

using namespace noor;
using asset::Json;

namespace {

const std::filesystem::path kChars = std::filesystem::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Characters";
constexpr float kPi = 3.14159265358979f;
constexpr double kDt = 1.0 / 60.0;

math::Quat about(math::Vec3 axis, float degrees) { return math::Quat::from_axis_angle(axis, degrees * kPi / 180.0f); }
bool same(math::Quat a, math::Quat b, float eps = 1e-5f) { return std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) > 1.0f - eps; }
bool near(math::Vec3 a, math::Vec3 b, float eps = 1e-5f) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps; }
bool same_pose(const asset::NodePose& a, const asset::NodePose& b, float eps = 1e-5f) {
    return near(a.translation, b.translation, eps) && same(a.rotation, b.rotation, eps) && near(a.scale, b.scale, eps);
}

const asset::ImportedModel& ward() {
    static const asset::ImportedModel m = [] {
        auto r = asset::import_gltf(kChars / "Ward_Anim.glb");
        REQUIRE(r.has_value());
        return *r;
    }();
    return m;
}
const asset::AnimController& ward_controller() {
    static const asset::AnimController c = [] {
        auto r = asset::load_anim_controller(kChars / "Ward.nooranim");
        REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : r.error().message));
        return *r;
    }();
    return c;
}
std::size_t node(const char* name) {
    for (std::size_t i = 0; i < ward().nodes.size(); ++i)
        if (ward().nodes[i].name == name) return i;
    FAIL("no node " << name);
    return 0;
}
// A clip's pose at a time, from the rest pose.
std::vector<asset::NodePose> clip_pose(const char* clip, double t) {
    auto pose = asset::rest_pose(ward());
    asset::sample(*asset::find_animation(ward(), clip), t, pose);
    return pose;
}
std::vector<asset::NodePose> posed(asset::AnimControllerPlayer& p, const asset::AnimParameters& params) {
    std::vector<asset::NodePose> out(ward().nodes.size());
    p.pose(params, out);
    return out;
}
void run(asset::AnimControllerPlayer& p, const asset::AnimParameters& params, int frames) {
    for (int i = 0; i < frames; ++i) p.step(kDt, params);
}

Result<asset::AnimController> parse(const char* text) {
    auto j = asset::parse_json(text, "test");
    REQUIRE(j.has_value());
    return asset::parse_anim_controller(*j, "test.nooranim");
}

} // namespace

TEST_CASE("blend maths: nlerp takes the short way and halves exactly, weighted mixes average") {
    // Halfway between 10 and 50 degrees about one axis is 30, exactly (nlerp's midpoint is slerp's).
    CHECK(same(asset::nlerp(about({1, 0, 0}, 10), about({1, 0, 0}, 50), 0.5f), about({1, 0, 0}, 30), 1e-6f));
    // q and -q are the same turn: blending them goes nowhere.
    const auto q = about({0, 1, 0}, 40);
    CHECK(same(asset::nlerp(q, {-q.x, -q.y, -q.z, -q.w}, 0.5f), q, 1e-6f));
    // A weighted mix: translations average by weight; +20 and -20 degrees equally cancel, even written the long way.
    std::vector<asset::NodePose> a(1), b(1), c(1), out(1);
    a[0].translation = {0, 0, 0}, b[0].translation = {10, 0, 0}, c[0].translation = {0, 100, 0};
    b[0].rotation = about({0, 0, 1}, 20);
    const auto minus = about({0, 0, 1}, -20);
    c[0].rotation = {-minus.x, -minus.y, -minus.z, -minus.w};
    const std::span<const asset::NodePose> poses[] = {a, b, c};
    const float w[] = {0.5f, 0.25f, 0.25f};
    asset::mix(poses, w, out);
    CHECK(out[0].translation.x == doctest::Approx(2.5f));
    CHECK(out[0].translation.y == doctest::Approx(25.0f));
    CHECK(same(out[0].rotation, about({0, 0, 1}, 0), 1e-6f));
    const float only_b[] = {0.0f, 1.0f, 0.0f};  // zero weights don't count
    asset::mix(poses, only_b, out);
    CHECK(out[0].translation.x == doctest::Approx(10.0f));
}

TEST_CASE("blend maths: additive deltas add back exactly, scale with their weight, and add nothing at zero") {
    asset::NodePose ref, pose;
    ref.rotation = about({0, 1, 0}, 30), ref.translation = {0, 1, 0};
    pose.rotation = ref.rotation * about({1, 0, 0}, 20), pose.translation = {0, 1.5f, 0}, pose.scale = {2, 2, 2};
    const auto d = asset::additive_delta(ref, pose);
    CHECK(same(d.rotation, about({1, 0, 0}, 20)));  // in the bone's own space
    std::vector<asset::NodePose> base{ref};
    const float full[] = {1.0f}, half[] = {0.5f}, none[] = {0.0f};
    asset::apply_additive(base, std::span(&d, 1), full);
    CHECK(same_pose(base[0], pose));  // the reference plus its difference is the pose
    std::vector<asset::NodePose> other(1);
    other[0].rotation = about({0, 0, 1}, 90);
    asset::apply_additive(other, std::span(&d, 1), half);
    CHECK(same(other[0].rotation, about({0, 0, 1}, 90) * about({1, 0, 0}, 10)));  // half the difference, on any base
    CHECK(other[0].translation.y == doctest::Approx(0.25f));
    CHECK(other[0].scale.x == doctest::Approx(1.5f));
    std::vector<asset::NodePose> untouched(1);
    asset::apply_additive(untouched, std::span(&d, 1), none);
    CHECK(same_pose(untouched[0], asset::NodePose{}));
}

TEST_CASE("blend maths: 1D and 2D blend-space weights") {
    const float at[] = {0.0f, 4.0f, 1.4f};  // not sorted on purpose
    auto w = asset::blend_weights_1d(at, 0.7f);
    CHECK(w[0] == doctest::Approx(0.5f));
    CHECK(w[2] == doctest::Approx(0.5f));
    CHECK(w[1] == doctest::Approx(0.0f));
    w = asset::blend_weights_1d(at, 2.7f);
    CHECK(w[2] == doctest::Approx(0.5f));
    CHECK(w[1] == doctest::Approx(0.5f));
    CHECK(asset::blend_weights_1d(at, -3.0f)[0] == 1.0f);  // clamped at the ends
    CHECK(asset::blend_weights_1d(at, 9.0f)[1] == 1.0f);
    CHECK(asset::blend_weights_1d(at, 1.4f)[2] == 1.0f);   // exactly on a sample

    const std::array<float, 2> square[] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}, {0, 0}};
    for (std::size_t i = 0; i < 5; ++i) {  // exact at every sample
        const auto ws = asset::blend_weights_2d(square, square[i]);
        for (std::size_t j = 0; j < 5; ++j) CHECK(ws[j] == doctest::Approx(i == j ? 1.0f : 0.0f));
    }
    const std::array<float, 2> corners[] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (const float v : asset::blend_weights_2d(corners, {0, 0})) CHECK(v == doctest::Approx(0.25f));  // the middle: equal
    for (float x = -1.5f; x <= 1.5f; x += 0.25f)
        for (float y = -1.5f; y <= 1.5f; y += 0.25f) {
            float sum = 0.0f;
            for (const float v : asset::blend_weights_2d(square, {x, y})) {
                CHECK(v >= 0.0f);
                sum += v;
            }
            CHECK(sum == doctest::Approx(1.0f));
        }
}

TEST_CASE("controller files: Ward's reads, mistakes are named in plain words") {
    const auto& c = ward_controller();
    REQUIRE(c.layers.size() == 3);
    CHECK(c.layers[0].states[0].samples.size() == 3);
    CHECK(c.layers[1].mask.size() == 1);
    CHECK(c.layers[2].mode == asset::AnimLayer::Mode::Additive);
    CHECK(c.layers[2].weight.parameter == "lean");
    CHECK(c.parameter("nod")->type == asset::AnimParameter::Type::Trigger);
    asset::AnimControllerPlayer p(c, ward());
    CHECK(p.warnings().empty());

    auto error = [](const char* text) {
        auto r = parse(text);
        REQUIRE(!r.has_value());
        return r.error().message;
    };
    CHECK(error(R"({"layers": []})").find("not an animation controller") != std::string::npos);
    CHECK(error(R"({"noor_anim": 2, "layers": []})").find("newer") != std::string::npos);
    CHECK(error(R"({"noor_anim": 1, "layers": [{"name": "Base", "states": [{"name": "A", "clip": "Walk", "speed": "fast"}]}]})")
              .find("there's no parameter 'fast'") != std::string::npos);
    CHECK(error(R"({"noor_anim": 1, "layers": [{"name": "Base", "states": [{"name": "A"}], "transitions": [{"from": "A", "to": "B"}]}]})")
              .find("no such \"to\" state") != std::string::npos);
    CHECK(error(R"({"noor_anim": 1, "layers": [{"name": "Base", "states": [{"name": "A"}, {"name": "A"}]}]})").find("two states called 'A'") !=
          std::string::npos);
    // Clips and bones the model doesn't have are warnings when it's bound, not crashes.
    auto odd = parse(R"({"noor_anim": 1, "layers": [{"name": "Base", "states": [{"name": "A", "clip": "Moonwalk"}]},
                        {"name": "Up", "mask": ["tail"], "states": [{"name": "B"}]}]})");
    REQUIRE(odd.has_value());
    asset::AnimControllerPlayer q(*odd, ward());
    REQUIRE(q.warnings().size() == 2);
    CHECK(q.warnings()[0].find("no clip 'Moonwalk'") != std::string::npos);
    CHECK(q.warnings()[1].find("'tail' isn't in the model") != std::string::npos);
    run(q, {}, 3);
    CHECK(same_pose(posed(q, {})[node("DEF-hips")], asset::rest_pose(ward())[node("DEF-hips")]));  // stands at rest
}

TEST_CASE("controller: a 1D blend space plays its clips in step, weighted by the parameter") {
    asset::AnimControllerPlayer p(ward_controller(), ward());
    const asset::AnimParameters params{{"speed", 0.7f}};  // halfway from Idle (0) to Walk (1.4)
    // Idle is 2 s, Walk 1 s: half and half, the blend lasts 1.5 s. After 45 frames (0.75 s) it's halfway through,
    // so Idle is at 1 s and Walk at 0.5 s.
    run(p, params, 45);
    CHECK(p.phase() == doctest::Approx(0.5));
    const auto idle = clip_pose("Idle", 1.0), walk = clip_pose("Walk", 0.5);
    std::vector<asset::NodePose> expect(idle.size());
    const std::span<const asset::NodePose> poses[] = {idle, walk};
    const float w[] = {0.5f, 0.5f};
    asset::mix(poses, w, expect);
    const auto got = posed(p, params);
    for (const char* bone : {"DEF-hips", "DEF-thigh.L", "DEF-upper_arm.R", "DEF-chest"}) {
        CAPTURE(bone);
        CHECK(same_pose(got[node(bone)], expect[node(bone)], 1e-4f));
    }
    // Speed 4 is all Run.
    asset::AnimControllerPlayer r(ward_controller(), ward());
    run(r, {{"speed", 4.0f}}, 9);  // 0.15 s of a 0.6 s run: its second key
    CHECK(same_pose(posed(r, {{"speed", 4.0f}})[node("DEF-thigh.L")], clip_pose("Run", 0.15)[node("DEF-thigh.L")], 1e-4f));
}

TEST_CASE("controller: a trigger's transition fires that frame, fades in exactly its frames, and comes back at exit time") {
    asset::AnimControllerPlayer p(ward_controller(), ward());
    asset::AnimParameters params{{"speed", 0.0f}};
    run(p, params, 10);
    CHECK(p.state() == "Move");
    params["nod"] = 1.0f;
    std::vector<std::string> consumed;
    p.step(kDt, params, &consumed);
    CHECK(p.state() == "Nod");  // the frame it was set
    CHECK(consumed == std::vector<std::string>{"nod"});
    params["nod"] = 0.0f;  // (the system resets it in the component)
    // A 0.1 s fade at 60 frames a second: 1/6 after the first frame, done after the sixth.
    CHECK(p.fade_weight() == doctest::Approx(1.0f / 6.0f));
    for (int k = 2; k <= 5; ++k) {
        p.step(kDt, params);
        CHECK(p.fade_weight() == doctest::Approx(k / 6.0f));
    }
    p.step(kDt, params);
    CHECK(p.fade_weight() == 1.0f);
    // Nod lasts 1 s: it ends on its 60th frame (one event), and exit time 1 takes it back on the 61st. (Every layer's
    // one-shots report their end: Lean, the additive layer's, ends on frame 60 of the run, too.)
    int ended_at = -1, back_at = -1, ends = 0, lean_ends = 0;
    for (int f = 7; f <= 70; ++f) {
        std::vector<asset::AnimControllerPlayer::Ended> ended;
        p.step(kDt, params, nullptr, &ended);
        for (const auto& e : ended) {
            if (e.layer == 2) {
                ++lean_ends;
                CHECK(e.state == "Lean");
                continue;
            }
            ++ends;
            ended_at = f;
            CHECK(e.layer == 0);
            CHECK(e.state == "Nod");
        }
        if (back_at < 0 && p.state() == "Move") back_at = f;
    }
    CHECK(ends == 1);
    CHECK(lean_ends == 1);
    CHECK(ended_at == 60);
    CHECK(back_at == 61);
}

TEST_CASE("controller: a masked override layer moves only its bones, an additive layer adds by its weight") {
    asset::AnimControllerPlayer plain(ward_controller(), ward()), waving(ward_controller(), ward()), leaning(ward_controller(), ward());
    const asset::AnimParameters walk{{"speed", 1.4f}}, wave{{"speed", 1.4f}, {"waving", 1.0f}}, lean{{"speed", 1.4f}, {"lean", 1.0f}};
    run(plain, walk, 90);
    run(waving, wave, 90);  // the 0.3 s fade long over
    run(leaning, lean, 90);  // Lean's 1 s clip done: its whole difference
    const auto a = posed(plain, walk), b = posed(waving, wave), c = posed(leaning, lean);
    // Waving: the right arm (and what hangs from it) is Raise's, the rest still walks.
    CHECK(same(b[node("DEF-upper_arm.R")].rotation, about({0, 0, 1}, -60), 1e-4f));
    CHECK(same(b[node("DEF-forearm.R")].rotation, clip_pose("Raise", 0.5)[node("DEF-forearm.R")].rotation, 1e-4f));
    for (const char* bone : {"DEF-thigh.L", "DEF-upper_arm.L", "DEF-hips", "DEF-chest"}) {
        CAPTURE(bone);
        CHECK(same_pose(a[node(bone)], b[node(bone)]));
    }
    // Leaning: the spine (which nothing else moves) turns 20 degrees forward; the chest adds 10 to the walk's.
    CHECK(same(c[node("DEF-spine")].rotation, about({1, 0, 0}, 20), 1e-4f));
    CHECK(same(c[node("DEF-chest")].rotation, a[node("DEF-chest")].rotation * about({1, 0, 0}, 10), 1e-4f));
    CHECK(same_pose(a[node("DEF-thigh.R")], c[node("DEF-thigh.R")]));
    // Half the weight, half the lean.
    asset::AnimControllerPlayer half(ward_controller(), ward());
    const asset::AnimParameters lean_half{{"speed", 1.4f}, {"lean", 0.5f}};
    run(half, lean_half, 90);
    CHECK(same(posed(half, lean_half)[node("DEF-spine")].rotation, about({1, 0, 0}, 10), 1e-4f));
}

TEST_CASE("controller: a 2D blend space is exact at its samples, and the same steps give the same pose bit for bit") {
    auto c = parse(R"({"noor_anim": 1, "parameters": {"x": {}, "y": {}},
        "layers": [{"name": "Base", "states": [{"name": "Pad", "blend": {"x": "x", "y": "y", "samples": [
            {"clip": "Idle", "x": 0, "y": 0}, {"clip": "Walk", "x": 0, "y": 1}, {"clip": "Run", "x": 0, "y": 2},
            {"clip": "Raise", "x": 1, "y": 0}]}}]}]})");
    REQUIRE(c.has_value());
    asset::AnimControllerPlayer p(*c, ward());
    const asset::AnimParameters on_raise{{"x", 1.0f}, {"y", 0.0f}};
    p.step(0.25, on_raise);  // Raise alone: 1 s long, a quarter through
    CHECK(same_pose(posed(p, on_raise)[node("DEF-forearm.R")], clip_pose("Raise", 0.25)[node("DEF-forearm.R")], 1e-4f));

    asset::AnimControllerPlayer x(ward_controller(), ward()), y(ward_controller(), ward());
    asset::AnimParameters params{{"speed", 0.0f}, {"lean", 0.3f}, {"waving", 1.0f}};
    for (int f = 0; f < 200; ++f) {
        params["speed"] = static_cast<float>(f) * 0.02f;
        if (f == 120) params["nod"] = 1.0f;
        if (f == 121) params["nod"] = 0.0f;
        x.step(kDt, params);
        y.step(kDt, params);
    }
    const auto px = posed(x, params), py = posed(y, params);
    CHECK(std::memcmp(px.data(), py.data(), px.size() * sizeof(asset::NodePose)) == 0);
}

namespace {

// Ward placed in a world, with a controller (or none), and an animator system that finds both files.
struct World {
    std::unique_ptr<runtime::World> world;
    Guid root;
    math::Quat rotation(const char* name) const {
        for (const auto& e : world->scene().entities)
            if (e.name == name) return e.transform.rotation;
        FAIL("no entity " << name);
        return {};
    }
    const Json& animator() const { return world->find(root)->components.at("animator"); }
};

// `scripts` go in first, as game::make_world adds them: their update runs before the animator's.
World place(Json animator, std::unique_ptr<runtime::System> scripts = nullptr) {
    const Guid model_id = Guid::generate(), controller_id = Guid::generate();
    scene::Scene s;
    const Guid root = scene::instantiate_model(s, model_id, ward(), "Ward", {});
    if (animator.contains("controller")) animator["controller"] = controller_id.to_string();
    for (auto& e : s.entities)
        if (e.id == root) e.components["animator"] = std::move(animator);
    World w{std::make_unique<runtime::World>(std::move(s)), root};
    if (scripts) w.world->add_system(std::move(scripts));
    w.world->add_system(std::make_unique<game::AnimatorSystem>([=](const Guid& id) -> std::optional<std::filesystem::path> {
        if (id == model_id) return kChars / "Ward_Anim.glb";
        if (id == controller_id) return kChars / "Ward.nooranim";
        return std::nullopt;
    }));
    return w;
}

} // namespace

TEST_CASE("animator with a controller: scripts set parameters and triggers, hear a state end, and read the state") {
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/ward.luau", R"(
        local Ward = {}
        function Ward:on_update(dt)
            self.frames = (self.frames or 0) + 1
            if self.frames == 30 then self.entity:set_animation_trigger("nod") end
            if self.frames == 40 then self.entity:set_animation_parameter("waving", true) end
        end
        function Ward:on_animation_end(name)
            self.entity:set("story", { ended = name, frame = self.frames, state = self.entity:animation() })
        end
        return Ward
    )");
    auto* scripts = lua.get();
    auto w = place({{"controller", ""}, {"parameters", {{"speed", 1.4}}}}, std::move(lua));
    w.world->find(w.root)->components["script"] = {{"file", "Scripts/ward.luau"}};
    for (int i = 0; i < 29; ++i) w.world->frame(kDt, {});
    CHECK(w.animator()["state"] == "Move");
    w.world->frame(kDt, {});
    CHECK(w.animator()["state"] == "Nod");                  // the frame the trigger was set
    CHECK(w.animator()["parameters"]["nod"] == false);      // used up
    for (int i = 0; i < 100; ++i) w.world->frame(kDt, {});
    CHECK(scripts->errors().empty());
    const auto& story = w.world->find(w.root)->components.at("story");
    CHECK(story["ended"] == "Nod");
    CHECK(story["frame"] == 89);  // entered on 30, one second long
    CHECK(story["state"] == "Nod");
    CHECK(w.animator()["state"] == "Move");
    CHECK(same(w.rotation("DEF-upper_arm.R"), about({0, 0, 1}, -60), 1e-4f));  // still waving on top
    CHECK(runtime::api::current_animation(*w.world, w.root) == "Move");
    // play_animation goes to a state now; stop freezes the controller.
    CHECK(runtime::api::play_animation(*w.world, w.root, "Nod"));
    w.world->frame(kDt, {});
    CHECK(w.animator()["state"] == "Nod");
    runtime::api::stop_animation(*w.world, w.root);
    w.world->frame(kDt, {});
    const auto frozen = w.rotation("DEF-head");
    for (int i = 0; i < 40; ++i) w.world->frame(kDt, {});
    CHECK(same(w.rotation("DEF-head"), frozen));
}

TEST_CASE("animator without a controller: a script cross-fade blends the two clips on their own clocks, then forgets") {
    auto w = place({{"animation", "Walk"}});
    for (int i = 0; i < 30; ++i) w.world->frame(kDt, {});  // 0.5 s into the walk
    const double at = w.world->time();
    REQUIRE(runtime::api::cross_fade(*w.world, w.root, "Run", 0.25f));
    for (int i = 0; i < 6; ++i) w.world->frame(kDt, {});  // 0.1 s into a 0.25 s fade: 40 %
    const double now = w.world->time();
    auto walk = clip_pose("Walk", std::fmod(now, 1.0)), run_pose = clip_pose("Run", std::fmod(now - at, 0.6));
    asset::blend(walk, run_pose, static_cast<float>((now - at) / 0.25), walk);
    CHECK(same(w.rotation("DEF-thigh.L"), walk[node("DEF-thigh.L")].rotation, 1e-4f));
    CHECK(w.animator().contains("from"));
    for (int i = 0; i < 12; ++i) w.world->frame(kDt, {});
    CHECK(!w.animator().contains("from"));  // done: what it faded from is forgotten
    CHECK(same(w.rotation("DEF-thigh.L"), clip_pose("Run", std::fmod(w.world->time() - at, 0.6))[node("DEF-thigh.L")].rotation, 1e-4f));
}
