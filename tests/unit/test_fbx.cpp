// FBX import (Milestone 8C, part 7): Blender's FBX exports of our characters import as the glTF exported from the same
// Blender scene does: the same skeleton, every joint in the same place at rest and through every clip, the same drawn
// shape (skinning included). The files are made by tools/samples/make_fbx_test_files.py (tests/data/animation/FBX).
// Blender's FBX exporter simplifies its curves by default (about 1.5 mm on Ward's walk); with that off, the two agree to
// a thousandth of a millimetre (the research note, docs/animation/FBX_RESEARCH.md).
#include "noor/asset/animation.h"
#include "noor/asset/fbx_import.h"
#include "noor/asset/model_compare.h"
#include "noor/asset/model_import.h"
#include "noor/asset/registry.h"
#include "noor/asset/project.h"
#include "noor/asset/retarget.h"
#include "noor/foundation/guid.h"
#include "noor/game/animation_assets.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace stdfs = std::filesystem;
using namespace noor;

namespace {

const stdfs::path kFbx = stdfs::path(NOOR_TEST_DATA_DIR) / "animation" / "FBX";

asset::ImportedModel load(const stdfs::path& p) {
    auto m = asset::import_model(p);
    REQUIRE_MESSAGE(m.has_value(), (m ? std::string() : m.error().message));
    return std::move(*m);
}

const asset::ModelComparison::Clip* clip(const asset::ModelComparison& r, const std::string& name) {
    for (const auto& c : r.clips)
        if (c.name == name) return &c;
    return nullptr;
}

} // namespace

TEST_CASE("fbx: Blender's export of Ward imports as its glTF export does: skeleton, skin and nine clips") {
    const auto fbx = load(kFbx / "blender52" / "Ward.fbx");
    const auto ref = load(kFbx / "blender52" / "Ward.ref.glb");
    const auto r = asset::compare_models(fbx, ref);
    INFO(r.to_json().dump(1));
    CHECK(r.matched == ref.nodes.size());  // every node of the glTF export, by name
    CHECK(r.only_a.empty());               // Blender's five "_end" leaf bones aside
    CHECK(r.only_b.empty());
    CHECK(r.triangles_a == r.triangles_b);
    CHECK(r.triangles_a == 2856);
    CHECK(r.skins_a == 1);
    CHECK(r.joints_a == 17);
    CHECK(fbx.animations.size() == 9);  // "Ward|Walk" and so on, called "Walk"
    CHECK(r.clips.size() == 9);
    CHECK(r.clips_only_a.empty());
    // At rest: exact (the skin's inverse binds, the axis and unit conversion, the bind pose as the rest pose).
    CHECK(r.rest_joints < 1e-5f);
    CHECK(r.rest_shape < 1e-5f);
    // Through every clip: within Blender's curve simplification (its FBX exporter thins keys; its glTF exporter samples).
    for (const auto& c : r.clips) {
        INFO(c.name);
        CHECK(c.duration_a == doctest::Approx(c.duration_b).epsilon(1e-4));
        CHECK(c.joints < 0.002f);
        CHECK(c.shape < 0.002f);
    }
    // Clips that the simplification doesn't touch (straight lines) agree to a hundredth of a millimetre.
    REQUIRE(clip(r, "Wave") != nullptr);
    CHECK(clip(r, "Wave")->joints < 1e-5f);
    CHECK(clip(r, "Raise")->joints < 1e-5f);
}

TEST_CASE("fbx: Mixamo names and a centimetre armature, Rigify names and an A-pose, Blender 3.3's exporter") {
    SUBCASE("Mast: mixamorig: names kept, centimetres become metres, the Mixamo preset found") {
        const auto fbx = load(kFbx / "blender52" / "Mast.fbx");
        const auto r = asset::compare_models(fbx, load(kFbx / "blender52" / "Mast.ref.glb"));
        CHECK(r.only_b.empty());
        CHECK(r.rest_joints < 1e-5f);
        CHECK(r.rest_shape < 1e-5f);
        const asset::BoneMap map = asset::detect_bone_map(fbx);
        CHECK(map.preset == "Mixamo");
        CHECK(map.usable());
    }
    SUBCASE("Bollard: Rigify's twist bones and the A-pose, exactly") {
        const auto fbx = load(kFbx / "blender52" / "Bollard.fbx");
        const auto r = asset::compare_models(fbx, load(kFbx / "blender52" / "Bollard.ref.glb"));
        CHECK(r.only_b.empty());
        CHECK(r.joints_a == 25);
        CHECK(r.rest_joints < 1e-5f);
        CHECK(r.rest_shape < 1e-5f);
        CHECK(asset::detect_bone_map(fbx).preset == "Rigify");
    }
    SUBCASE("Ward through Blender 3.3's FBX exporter: the same as 5.2's glTF export") {
        // Blender 3.3's own glTF export of this scene gets some clips wrong (its research note): the reference is 5.2's.
        const auto fbx = load(kFbx / "blender33" / "Ward.fbx");
        const auto r = asset::compare_models(fbx, load(kFbx / "blender52" / "Ward.ref.glb"));
        CHECK(r.clips.size() == 9);  // 3.3 names them "Walk_Ward": matched with "Walk"
        CHECK(r.rest_joints < 1e-5f);
        CHECK(r.worst_joints() < 0.002f);
        CHECK(r.worst_shape() < 0.002f);
    }
}

TEST_CASE("fbx: a character's clips from a file of its own: copied by bone name, retargeted only when the bones differ") {
    // As FBX packs ship a character: the model in one file, its clips in others (the armature alone, no mesh, no bind
    // pose). Ward_clips.fbx is that, from the same Blender scene as Ward.fbx; it lends its clips to Ward.fbx with his own
    // clips taken away.
    const auto lender = load(kFbx / "blender52" / "Ward_clips.fbx");
    CHECK(lender.meshes.empty());
    CHECK(lender.animations.size() == 9);
    const auto ward = load(kFbx / "blender52" / "Ward.fbx");
    asset::ImportedModel bare = ward;
    bare.animations.clear();
    const auto copied = game::same_skeleton_clips(lender, bare);
    REQUIRE(copied.has_value());
    CHECK(copied->size() == 9);
    for (const auto& c : *copied)
        for (const auto& ch : c.channels) {
            CHECK(ch.node >= 0);
            CHECK(static_cast<std::size_t>(ch.node) < bare.nodes.size());
        }
    // Played on the bare model, they put every joint where his own clips do, exactly (the same scene's bake).
    bare.animations = *copied;
    const auto r = asset::compare_models(bare, ward);
    INFO(r.to_json().dump(1));
    CHECK(r.clips.size() == 9);
    CHECK(r.worst_joints() < 1e-5f);
    CHECK(r.worst_shape() < 1e-5f);
    // Not the same skeleton, so not copied (retargeting's job): Mast (other names); the glTF export of the same scene
    // (Blender's FBX exporter gives the armature a x100 scale and a quarter turn, its glTF exporter doesn't: inside the
    // armature the FBX's bones are in centimetres, Z up); Blender 3.3's Ward (its glTF importer turned
    // the leg bones half round: the same names, other rest rotations and offsets).
    CHECK_FALSE(game::same_skeleton_clips(lender, load(kFbx / "blender52" / "Mast.fbx")).has_value());
    CHECK_FALSE(game::same_skeleton_clips(ward, load(kFbx / "blender52" / "Ward.ref.glb")).has_value());
    const auto old33 = load(kFbx / "blender33" / "Ward.fbx");  // 3.3's glTF importer turned his leg bones round
    CHECK_FALSE(game::same_skeleton_clips(old33, bare).has_value());
}

TEST_CASE("fbx: broken files are refused in words, and the registry calls an FBX a model") {
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_fbx_" + Guid::generate().to_string());
    stdfs::create_directories(dir);
    {
        std::ofstream(dir / "empty.fbx", std::ios::binary);
        std::ofstream junk(dir / "junk.fbx", std::ios::binary);
        junk << "this is not an FBX file at all, just words pretending";
        // Half of a real one.
        std::ifstream in(kFbx / "blender52" / "Mast.fbx", std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
        std::ofstream half(dir / "half.fbx", std::ios::binary);
        half.write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
    }
    for (const char* name : {"empty.fbx", "junk.fbx", "half.fbx", "missing.fbx"}) {
        INFO(name);
        const auto m = asset::import_fbx(dir / name);
        CHECK_FALSE(m.has_value());
        if (!m) CHECK(m.error().message.find(name) != std::string::npos);  // says which file
    }
    CHECK_FALSE(asset::import_fbx(kFbx / "blender52" / "Mast.fbx", 1000).has_value());  // over the size limit

    CHECK(asset::is_model_file("a/b/Fish.FBX"));
    CHECK(asset::AssetRegistry::is_importable("Assets/Fish.fbx"));
    // In a project: scanned as a model, read by the "fbx" importer.
    auto project = asset::Project::create(dir / "Proj", "FbxTest");
    REQUIRE(project.has_value());
    stdfs::create_directories(dir / "Proj" / "Assets");
    stdfs::copy_file(kFbx / "blender52" / "Mast.fbx", dir / "Proj" / "Assets" / "Mast.fbx");
    asset::AssetRegistry registry;
    REQUIRE(registry.scan(*project).has_value());
    const auto* rec = registry.find_path("Assets/Mast.fbx");
    REQUIRE(rec != nullptr);
    CHECK(rec->importer == "fbx");
    std::error_code ec;
    stdfs::remove_all(dir, ec);
}
