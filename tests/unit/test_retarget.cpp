// Retargeting (Milestone 8C, part 5, ADR-036): bone maps detected from the names (the four presets and common names),
// corrected from a .noormeta; Ward's clips (1.8 m, Rigify-style names, T-pose) on Mast (2.0 m, Mixamo names, T-pose,
// a centimetre armature scaled 0.01) and Bollard (1.45 m, real Rigify names, A-pose, Blender's rolled bone frames):
// the bones point where Ward's point, within two degrees, at key frames and between them; the hips move by leg length;
// root motion through the animator covers the scaled distance in metres. Mast and Bollard come from
// tools/samples/make_test_characters.py.
#include "noor/asset/anim_controller.h"
#include "noor/asset/animation.h"
#include "noor/asset/gltf_import.h"
#include "noor/asset/retarget.h"
#include "noor/asset/skinning.h"
#include "noor/game/animator.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scene/scene.h"

#include <ostream>
#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace noor;
using asset::HumanBone;
using asset::Json;

namespace {

const std::filesystem::path kChars = std::filesystem::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Characters";

const asset::ImportedModel& load(const char* file) {
    static std::unordered_map<std::string, asset::ImportedModel> cache;
    auto it = cache.find(file);
    if (it == cache.end()) {
        auto r = asset::import_gltf(kChars / file);
        REQUIRE(r.has_value());
        it = cache.emplace(file, std::move(*r)).first;
    }
    return it->second;
}
const asset::ImportedModel& ward() { return load("Ward_Anim.glb"); }
const asset::ImportedModel& mast() { return load("Mast.glb"); }
const asset::ImportedModel& bollard() { return load("Bollard.glb"); }

std::string name_of(const asset::ImportedModel& m, const asset::BoneMap& map, HumanBone b) {
    const int n = map.node(b);
    return n >= 0 ? m.nodes[static_cast<std::size_t>(n)].name : std::string("-");
}

// A bare skeleton from names: each (name, parent index).
asset::ImportedModel skeleton(const std::vector<std::pair<std::string, int>>& bones) {
    asset::ImportedModel m;
    for (const auto& [name, parent] : bones) {
        asset::ImportedNode n;
        n.name = name;
        n.parent = parent;
        n.translation = {0, 0.1f, 0};
        m.nodes.push_back(n);
    }
    return m;
}

// The humanoid's standard hierarchy with these names, in HumanBone order ("" leaves a bone out).
asset::ImportedModel humanoid(const std::array<std::string, asset::kHumanBones>& names) {
    std::vector<std::pair<std::string, int>> bones;
    std::array<int, asset::kHumanBones> index{};
    index.fill(-1);
    const auto parent = [&](std::initializer_list<HumanBone> options) {
        for (const HumanBone b : options)
            if (index[static_cast<std::size_t>(b)] >= 0) return index[static_cast<std::size_t>(b)];
        return -1;
    };
    using H = HumanBone;
    for (std::size_t b = 0; b < asset::kHumanBones; ++b) {
        if (names[b].empty()) continue;
        int p = -1;
        switch (static_cast<H>(b)) {
        case H::Hips: break;
        case H::Spine: p = parent({H::Hips}); break;
        case H::Chest: p = parent({H::Spine}); break;
        case H::UpperChest: p = parent({H::Chest, H::Spine}); break;
        case H::Neck: case H::LeftShoulder: case H::RightShoulder: p = parent({H::UpperChest, H::Chest, H::Spine}); break;
        case H::Head: p = parent({H::Neck}); break;
        case H::LeftUpperArm: p = parent({H::LeftShoulder, H::UpperChest, H::Chest, H::Spine}); break;
        case H::RightUpperArm: p = parent({H::RightShoulder, H::UpperChest, H::Chest, H::Spine}); break;
        case H::LeftUpperLeg: case H::RightUpperLeg: p = parent({H::Hips}); break;
        default: p = static_cast<int>(bones.size()) - 1;  // the previous bone of its chain
        }
        index[b] = static_cast<int>(bones.size());
        bones.emplace_back(names[b], p);
    }
    return skeleton(bones);
}

float degrees_between(math::Vec3 a, math::Vec3 b) {
    const float d = std::clamp(math::dot(math::normalize(a), math::normalize(b)), -1.0f, 1.0f);
    return std::acos(d) * 180.0f / 3.14159265f;
}

math::Vec3 head(const std::vector<math::Mat4>& world, int node) {
    const auto& c = world[static_cast<std::size_t>(node)].m[3];
    return {c.x, c.y, c.z};
}

// The direction a bone points (to the next bone of its chain), in the model's space.
math::Vec3 direction(const asset::ImportedModel&, const std::vector<math::Mat4>& world, const asset::BoneMap& map, HumanBone from, HumanBone to) {
    return head(world, map.node(to)) - head(world, map.node(from));
}

constexpr std::array<std::pair<HumanBone, HumanBone>, 10> kLimbs = {{
    {HumanBone::LeftUpperArm, HumanBone::LeftLowerArm},   {HumanBone::LeftLowerArm, HumanBone::LeftHand},
    {HumanBone::RightUpperArm, HumanBone::RightLowerArm}, {HumanBone::RightLowerArm, HumanBone::RightHand},
    {HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg},   {HumanBone::LeftLowerLeg, HumanBone::LeftFoot},
    {HumanBone::RightUpperLeg, HumanBone::RightLowerLeg}, {HumanBone::RightLowerLeg, HumanBone::RightFoot},
    {HumanBone::Chest, HumanBone::Neck},                  {HumanBone::Hips, HumanBone::Spine},
}};

} // namespace

TEST_CASE("retargeting: bone maps are detected from the names (presets, then common names)") {
    using H = HumanBone;
    {
        const auto map = asset::detect_bone_map(ward());  // Rigify-style but not Rigify's chain: common names
        CHECK(map.preset == "names");
        CHECK(map.usable());
        CHECK(map.warnings.empty());
        CHECK(name_of(ward(), map, H::Hips) == "DEF-hips");
        CHECK(name_of(ward(), map, H::Chest) == "DEF-chest");
        CHECK(name_of(ward(), map, H::LeftUpperArm) == "DEF-upper_arm.L");
        CHECK(name_of(ward(), map, H::RightLowerLeg) == "DEF-shin.R");
        CHECK(map.node(H::LeftToes) == -1);
    }
    {
        const auto map = asset::detect_bone_map(mast());
        CHECK(map.preset == "Mixamo");
        CHECK(map.warnings.empty());
        CHECK(name_of(mast(), map, H::Chest) == "mixamorig:Spine1");
        CHECK(name_of(mast(), map, H::UpperChest) == "mixamorig:Spine2");
        CHECK(name_of(mast(), map, H::LeftUpperArm) == "mixamorig:LeftArm");
        CHECK(name_of(mast(), map, H::LeftLowerLeg) == "mixamorig:LeftLeg");
        CHECK(name_of(mast(), map, H::RightToes) == "mixamorig:RightToeBase");
    }
    {
        const auto map = asset::detect_bone_map(bollard());
        CHECK(map.preset == "Rigify");
        CHECK(map.warnings.empty());
        CHECK(name_of(bollard(), map, H::Hips) == "DEF-spine");
        CHECK(name_of(bollard(), map, H::Head) == "DEF-spine.006");
        CHECK(name_of(bollard(), map, H::LeftUpperArm) == "DEF-upper_arm.L");  // not its twist bone .001
        CHECK(name_of(bollard(), map, H::LeftLowerArm) == "DEF-forearm.L");
        CHECK(name_of(bollard(), map, H::LeftShoulder) == "DEF-shoulder.L");
    }
    const std::array<std::string, asset::kHumanBones> unreal = {
        "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head", "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
        "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r", "thigh_l", "calf_l", "foot_l", "ball_l", "thigh_r", "calf_r", "foot_r", "ball_r"};
    const auto ue = asset::detect_bone_map(humanoid(unreal));
    CHECK(ue.preset == "Unreal mannequin");
    CHECK(ue.warnings.empty());
    for (std::size_t b = 0; b < asset::kHumanBones; ++b) CHECK(ue.nodes[b] >= 0);

    const std::array<std::string, asset::kHumanBones> metarig = {
        "spine", "spine.001", "spine.002", "spine.003", "spine.004", "spine.006", "shoulder.L", "upper_arm.L", "forearm.L", "hand.L",
        "shoulder.R", "upper_arm.R", "forearm.R", "hand.R", "thigh.L", "shin.L", "foot.L", "toe.L", "thigh.R", "shin.R", "foot.R", "toe.R"};
    CHECK(asset::detect_bone_map(humanoid(metarig)).preset == "Blender metarig");

    // Mixamo's prefix varies (a second character in one scene gets "mixamorig1:"); Blender may turn ':' into '_'.
    for (const std::string prefix : {"mixamorig1:", "mixamorig_"}) {
        std::array<std::string, asset::kHumanBones> mx = {
            "Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
            "RightShoulder", "RightArm", "RightForeArm", "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase",
            "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"};
        for (auto& n : mx) n = prefix + n;
        CAPTURE(prefix);
        CHECK(asset::detect_bone_map(humanoid(mx)).preset == "Mixamo");
    }

    // Common names in other spellings: "UpperLeg_L", "Calf.R", "L_Hand"...
    const std::array<std::string, asset::kHumanBones> loose = {
        "Pelvis", "Spine", "", "", "Neck", "Head", "", "UpperArm_L", "LowerArm_L", "L_Hand",
        "", "UpperArm_R", "LowerArm_R", "R_Hand", "UpperLeg_L", "Calf.L", "Foot.L", "", "UpperLeg_R", "Calf.R", "Foot.R", ""};
    const auto names = asset::detect_bone_map(humanoid(loose));
    CHECK(names.preset == "names");
    CHECK(names.usable());

    // Nothing humanoid: said plainly, and not usable.
    const auto none = asset::detect_bone_map(skeleton({{"Root", -1}, {"Tail.001", 0}, {"Tail.002", 1}}));
    CHECK(none.preset == "none");
    CHECK_FALSE(none.usable());
    REQUIRE_FALSE(none.warnings.empty());
    CHECK(none.warnings.front() == "no bone for Hips");
}

TEST_CASE("retargeting: a .noormeta corrects the map") {
    using H = HumanBone;
    const auto map = asset::bone_map(ward(), Json{{"Chest", nullptr}, {"Neck", "DEF-chest"}, {"Tail", "DEF-hips"}, {"LeftToes", "DEF-toe.L"}});
    CHECK(map.preset == "names (edited)");
    CHECK(map.node(H::Chest) == -1);
    CHECK(name_of(ward(), map, H::Neck) == "DEF-chest");
    CHECK(map.usable());
    REQUIRE(map.warnings.size() == 2);
    CHECK(map.warnings[0] == "the bone map names 'Tail', which isn't a humanoid bone");
    CHECK(map.warnings[1] == "the bone map's LeftToes is 'DEF-toe.L', which the model doesn't have");

    // A hierarchy that can't be right is warned about, and a missing required bone makes it unusable.
    const auto odd = asset::bone_map(ward(), Json{{"LeftLowerArm", "DEF-shin.L"}, {"Head", nullptr}});
    CHECK_FALSE(odd.usable());
    bool below = false, head = false;
    for (const auto& w : odd.warnings) {
        below = below || w == "LeftLowerArm ('DEF-shin.L') isn't below LeftUpperArm ('DEF-upper_arm.L')";
        head = head || w == "no bone for Head";
    }
    CHECK(below);
    CHECK(head);

    // From a model file's .noormeta.
    const auto dir = std::filesystem::temp_directory_path() / "noor_retarget_meta";
    std::filesystem::create_directories(dir);
    const auto file = dir / "Ward.glb";
    std::ofstream(file.string() + ".noormeta") << R"({"noor_meta": 1, "settings": {"bone_map": {"Neck": null}}})";
    const auto from_file = asset::bone_map_for(ward(), file);
    CHECK(from_file.preset == "names (edited)");
    CHECK(from_file.node(H::Neck) == -1);
    CHECK(asset::bone_map_for(ward(), dir / "Elsewhere.glb").preset == "names");  // no .noormeta: detected
}

TEST_CASE("retargeting: Mast and Bollard skin to themselves at rest (the generator's bind matrices)") {
    for (const auto* m : {&mast(), &bollard()}) {
        const auto world = asset::posed_world(*m, asset::rest_pose(*m));
        int checked = 0;
        for (const auto& node : m->nodes) {
            if (node.mesh < 0 || node.skin < 0) continue;
            const auto& skin = m->skins[static_cast<std::size_t>(node.skin)];
            std::vector<math::Mat4> joints;
            for (const int j : skin.joints) joints.push_back(world[static_cast<std::size_t>(j)]);
            const auto palette = asset::skin_palette(skin, joints);
            const auto& mesh = m->meshes[static_cast<std::size_t>(node.mesh)];
            for (std::size_t v = 0; v < mesh.vertices.size(); v += 7) {
                const math::Vec3 p = mesh.vertices[v].position, q = asset::skin_point(mesh.skin[v], palette, p);
                CHECK(math::length(q - p) < 1e-4f);
                ++checked;
            }
        }
        CHECK(checked > 100);
    }
}

TEST_CASE("retargeting: bones point where the source's point (T-pose to tall T and stout A), within two degrees") {
    const auto ward_map = asset::detect_bone_map(ward());
    for (const auto* target : {&mast(), &bollard()}) {
        const auto map = asset::detect_bone_map(*target);
        const asset::Retargeter r(ward(), ward_map, *target, map);
        REQUIRE(r.usable());
        CHECK(r.warnings().empty());
        for (const char* clip_name : {"Walk", "Run", "Raise"}) {
            const auto* clip = asset::find_animation(ward(), clip_name);
            REQUIRE(clip != nullptr);
            const auto baked = r.bake(*clip);
            CHECK(baked.name == clip->name);
            CHECK(baked.duration == clip->duration);
            // Key frames (0, a quarter...) and moments between keys.
            for (const double f : {0.0, 0.25, 0.4, 0.5, 0.75, 0.93}) {
                const double t = f * clip->duration;
                CAPTURE(target->nodes.front().name);
                CAPTURE(std::string(clip_name));
                CAPTURE(t);
                auto sp = asset::rest_pose(ward());
                asset::sample(*clip, t, sp);
                auto tp = asset::rest_pose(*target);
                asset::sample(baked, t, tp);
                const auto sw = asset::posed_world(ward(), sp), tw = asset::posed_world(*target, tp);
                for (const auto& [from, to] : kLimbs) {
                    // Raise moves only the right arm: the rest keeps the target's own pose (Bollard's A), tested below.
                    if (std::string_view(clip_name) == "Raise" && from != HumanBone::RightUpperArm && from != HumanBone::RightLowerArm) continue;
                    CAPTURE(asset::human_bone_name(from));
                    CHECK(degrees_between(direction(ward(), sw, ward_map, from, to), direction(*target, tw, map, from, to)) < 2.0f);
                }
            }
        }
    }
}

TEST_CASE("retargeting: an A-pose arm is lifted to the source's T only where the clip moves it") {
    using H = HumanBone;
    const auto ward_map = asset::detect_bone_map(ward()), map = asset::detect_bone_map(bollard());
    const asset::Retargeter r(ward(), ward_map, bollard(), map);
    const auto baked = r.bake(*asset::find_animation(ward(), "Wave"));  // Ward's right arm, 0 to 90 degrees up
    // Only the right arm's chain is baked (and its twist bone isn't: it isn't mapped).
    std::vector<std::string> names;
    for (const auto& ch : baked.channels) names.push_back(bollard().nodes[static_cast<std::size_t>(ch.node)].name);
    CHECK(names == std::vector<std::string>{"DEF-upper_arm.R", "DEF-forearm.R", "DEF-hand.R"});

    auto tp = asset::rest_pose(bollard());
    asset::sample(baked, 0.0, tp);
    auto w = asset::posed_world(bollard(), tp);
    // At 0 s Ward's right arm is straight out (his T-pose): Bollard's too, though he was built with it 45 degrees down.
    CHECK(degrees_between(direction(bollard(), w, map, H::RightUpperArm, H::RightLowerArm), {-1, 0, 0}) < 0.5f);
    CHECK(degrees_between(direction(bollard(), w, map, H::RightLowerArm, H::RightHand), {-1, 0, 0}) < 0.5f);
    // The left arm isn't in the clip: it stays as Bollard was built.
    CHECK(degrees_between(direction(bollard(), w, map, H::LeftUpperArm, H::LeftLowerArm), {1, -1, 0}) < 0.5f);
    // At 1 s Ward's arm hangs straight down (90 degrees about Z from straight out): so does Bollard's.
    tp = asset::rest_pose(bollard());
    asset::sample(baked, 1.0, tp);
    w = asset::posed_world(bollard(), tp);
    CHECK(degrees_between(direction(bollard(), w, map, H::RightUpperArm, H::RightLowerArm), {0, -1, 0}) < 0.5f);
    CHECK(r.hips_scale() == doctest::Approx((0.78 - 0.08) / (0.95 - 0.08)).epsilon(1e-4));
}

TEST_CASE("retargeting: the hips move by the source's movement times the ratio of leg lengths") {
    using H = HumanBone;
    const auto* walk = asset::find_animation(ward(), "WalkForward");
    REQUIRE(walk != nullptr);
    for (const auto* target : {&mast(), &bollard()}) {
        const auto map = asset::detect_bone_map(*target);
        const asset::Retargeter r(ward(), asset::detect_bone_map(ward()), *target, map);
        const float scale = r.hips_scale();
        const auto rest_w = asset::posed_world(*target, asset::rest_pose(*target));
        const math::Vec3 rest_hips = head(rest_w, map.node(H::Hips));
        const auto baked = r.bake(*walk);
        for (const auto& [t, z, dy] : {std::tuple{0.25, 0.35f, -0.02f}, std::tuple{0.5, 0.7f, 0.0f}, std::tuple{1.0, 1.4f, 0.0f}}) {
            CAPTURE(t);
            auto tp = asset::rest_pose(*target);
            asset::sample(baked, t, tp);
            const math::Vec3 hips = head(asset::posed_world(*target, tp), map.node(H::Hips));
            CHECK(hips.z - rest_hips.z == doctest::Approx(z * scale).epsilon(1e-3));
            CHECK(hips.y - rest_hips.y == doctest::Approx(dy * scale).epsilon(1e-3));
            CHECK(std::abs(hips.x - rest_hips.x) < 1e-4f);
        }
        // The retargeted clip's root bone is the target's hips (root motion works on it as on any clip of its own).
        asset::ImportedModel with = *target;
        with.animations = {baked};
        CHECK(asset::root_motion_node(with) == map.node(H::Hips));
    }
}

TEST_CASE("retargeting: baking is deterministic and keeps every source key") {
    const asset::Retargeter r(ward(), asset::detect_bone_map(ward()), bollard(), asset::detect_bone_map(bollard()));
    const auto* run = asset::find_animation(ward(), "Run");
    const auto a = r.bake(*run), b = r.bake(*run);
    REQUIRE(a.channels.size() == b.channels.size());
    for (std::size_t i = 0; i < a.channels.size(); ++i) {
        CHECK(a.channels[i].times == b.channels[i].times);
        REQUIRE(a.channels[i].values.size() == b.channels[i].values.size());
        for (std::size_t k = 0; k < a.channels[i].values.size(); ++k)
            CHECK(std::bit_cast<std::uint32_t>(a.channels[i].values[k]) == std::bit_cast<std::uint32_t>(b.channels[i].values[k]));
    }
    const auto& times = a.channels.front().times;
    for (const float key : {0.0f, 0.15f, 0.3f, 0.45f, 0.6f})
        CHECK(std::find_if(times.begin(), times.end(), [&](float t) { return std::abs(t - key) < 1e-6f; }) != times.end());
    for (std::size_t k = 1; k < times.size(); ++k) CHECK(times[k] - times[k - 1] <= 1.0f / 60.0f + 1e-6f);
    CHECK(times.back() == doctest::Approx(0.6f));
}

TEST_CASE("retargeting: an animator plays Ward's walk on Mast, root motion in metres through his 0.01 armature") {
    const Guid mast_id = Guid::generate(), ward_id = Guid::generate();
    scene::Scene s;
    const Guid root = scene::instantiate_model(s, mast_id, mast(), "Mast", {});
    for (auto& e : s.entities)
        if (e.id == root) {
            CHECK(e.components["model"].contains("nodes"));  // skinned models list their nodes (part 5)
            e.components["animator"] = {{"animation", "WalkForward"}, {"retarget_from", ward_id.to_string()}, {"root_motion", true}};
        }
    runtime::World world(std::move(s));
    world.add_system(std::make_unique<game::AnimatorSystem>([=](const Guid& id) -> std::optional<std::filesystem::path> {
        if (id == mast_id) return kChars / "Mast.glb";
        if (id == ward_id) return kChars / "Ward_Anim.glb";
        return std::nullopt;
    }));
    for (int i = 0; i < 120; ++i) world.frame(1.0 / 60.0, {});  // two cycles
    const asset::Retargeter r(ward(), asset::detect_bone_map(ward()), mast(), asset::detect_bone_map(mast()));
    const auto at = *runtime::api::world_position(world, root);
    CHECK(std::abs(at.z - 2.0f * 1.4f * r.hips_scale()) < 0.01f);
    CHECK(std::abs(at.x) < 1e-3f);
    const auto* sys = world.system<game::AnimatorSystem>();
    CHECK(sys->stats().animators == 1);
    CHECK(sys->stats().nodes == 17);  // every Mast bone any of Ward's clips moves or turns (the toes and shoulders: none)
}

TEST_CASE("retargeting: Ward's controller runs on Mast, its mask's bone names translated") {
    const std::string ctrl_file = (kChars / "Ward.nooranim").string();
    auto ctrl = asset::load_anim_controller(ctrl_file);
    REQUIRE(ctrl.has_value());
    const Guid mast_id = Guid::generate(), ward_id = Guid::generate(), ctrl_id = Guid::generate();
    scene::Scene s;
    const Guid root = scene::instantiate_model(s, mast_id, mast(), "Mast", {});
    for (auto& e : s.entities)
        if (e.id == root)
            e.components["animator"] = {{"controller", ctrl_id.to_string()}, {"retarget_from", ward_id.to_string()},
                                        {"parameters", {{"speed", 1.4}, {"waving", true}}}};
    runtime::World world(std::move(s));
    world.add_system(std::make_unique<game::AnimatorSystem>([=](const Guid& id) -> std::optional<std::filesystem::path> {
        if (id == mast_id) return kChars / "Mast.glb";
        if (id == ward_id) return kChars / "Ward_Anim.glb";
        if (id == ctrl_id) return std::filesystem::path(ctrl_file);
        return std::nullopt;
    }));
    for (int i = 0; i < 60; ++i) world.frame(1.0 / 60.0, {});
    // The Arm layer's mask says "DEF-upper_arm.R" (Ward's name): on Mast that's mixamorig:RightArm. Waving, it's raised:
    // Mast's right arm points where Ward's does in Raise (up and out), not down at his side as in the walk.
    const auto map = asset::detect_bone_map(mast());
    const auto upper = runtime::api::world_position(world, *runtime::api::bone(world, root, "mixamorig:RightArm"));
    const auto lower = runtime::api::world_position(world, *runtime::api::bone(world, root, "mixamorig:RightForeArm"));
    REQUIRE(upper);
    REQUIRE(lower);
    CHECK(lower->y - upper->y > 0.15f);  // raised (Raise holds it 60 degrees up from straight out)

    // Masks may also name humanoid bones, on any model with a bone map.
    auto json = Json::parse(R"({"noor_anim": 1, "layers": [{"name": "Base", "entry": "Idle", "states": [{"name": "Idle", "clip": "Idle"}]},
        {"name": "Arm", "mode": "override", "mask": ["RightUpperArm"], "entry": "Wave", "states": [{"name": "Wave", "clip": "Raise"}]}]})");
    auto by_human = asset::parse_anim_controller(json);
    REQUIRE(by_human.has_value());
    std::unordered_map<std::string, int> aliases;
    const auto ward_map = asset::detect_bone_map(ward());
    for (std::size_t b = 0; b < asset::kHumanBones; ++b)
        if (ward_map.nodes[b] >= 0) aliases.emplace(asset::human_bone_name(static_cast<HumanBone>(b)), ward_map.nodes[b]);
    CHECK(asset::AnimControllerPlayer(*by_human, ward(), &aliases).warnings().empty());
    CHECK(asset::AnimControllerPlayer(*by_human, ward()).warnings().size() == 1);  // without: "isn't in the model"
}
