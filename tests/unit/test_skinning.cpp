// Skinned meshes (Milestone 8C, part 1, ADR-036): skins, weights and inverse bind matrices from glTF, and linear blend
// skinning against hand-worked values. SimpleSkin is the glTF tutorial's two-joint strip; RobotExpressive a real file
// (Quaternius, CC0): rigid parts on bones, skinned hands, an armature scaled x100.
#include "noor/asset/gltf_import.h"
#include "noor/asset/skinning.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>

using namespace noor;

namespace {

const std::filesystem::path kData = std::filesystem::path(NOOR_TEST_DATA_DIR) / "animation";

// World matrices of a skin's joints with the model's own node transforms (its rest pose).
std::vector<math::Mat4> rest_worlds(const asset::ImportedModel& m, const asset::ImportedSkin& skin) {
    std::vector<math::Mat4> out;
    for (int j : skin.joints) out.push_back(m.world_matrix(static_cast<std::size_t>(j)));
    return out;
}

bool near(math::Vec3 a, math::Vec3 b, float eps = 1e-5f) { return math::length(a - b) < eps; }

} // namespace

TEST_CASE("skinning: SimpleSkin's skin, weights and inverse bind matrices come in as the file has them") {
    auto model = asset::import_gltf(kData / "SimpleSkin" / "SimpleSkin.gltf");
    REQUIRE(model.has_value());
    REQUIRE(model->skins.size() == 1);
    const auto& skin = model->skins[0];
    REQUIRE(skin.joints.size() == 2);
    CHECK(skin.inverse_bind[0].m[3].y == doctest::Approx(0.0f));
    CHECK(skin.inverse_bind[1].m[3].y == doctest::Approx(-1.0f));  // the second joint was bound 1 m up
    // The skinned node and its mesh.
    int skinned = -1;
    for (std::size_t i = 0; i < model->nodes.size(); ++i)
        if (model->nodes[i].skin == 0) skinned = static_cast<int>(i);
    REQUIRE(skinned >= 0);
    const auto& mesh = model->meshes[static_cast<std::size_t>(model->nodes[static_cast<std::size_t>(skinned)].mesh)];
    REQUIRE(mesh.skin.size() == mesh.vertices.size());
    REQUIRE(mesh.vertices.size() == 10);
    // Weights sorted strongest first: the bottom pair all joint 0, the top pair all joint 1, the middle half and half.
    CHECK(mesh.skin[0].weights.x == doctest::Approx(1.0f));
    CHECK(mesh.skin[0].joints[0] == 0);
    CHECK(mesh.skin[9].joints[0] == 1);
    CHECK(mesh.skin[9].weights.x == doctest::Approx(1.0f));
    CHECK(mesh.skin[4].weights.x == doctest::Approx(0.5f));
    CHECK(mesh.skin[4].weights.y == doctest::Approx(0.5f));
    CHECK(model->warnings.empty());
}

TEST_CASE("skinning: bending SimpleSkin's top joint moves its vertices to the hand-worked places") {
    auto model = asset::import_gltf(kData / "SimpleSkin" / "SimpleSkin.gltf");
    REQUIRE(model.has_value());
    const auto& skin = model->skins[0];
    int skinned = 0;
    for (std::size_t i = 0; i < model->nodes.size(); ++i)
        if (model->nodes[i].skin == 0) skinned = static_cast<int>(i);
    const auto& mesh = model->meshes[static_cast<std::size_t>(model->nodes[static_cast<std::size_t>(skinned)].mesh)];

    // At rest the palette is identity: every vertex stays where it was bound.
    const auto rest = asset::skin_palette(skin, rest_worlds(*model, skin));
    for (std::size_t v = 0; v < mesh.vertices.size(); ++v)
        CHECK(near(asset::skin_point(mesh.skin[v], rest, mesh.vertices[v].position), mesh.vertices[v].position));

    // Turn joint 1 (1 m up) by 90 degrees about +Z, as the tutorial's animation does.
    auto worlds = rest_worlds(*model, skin);
    worlds[1] = math::translation({0, 1, 0}) * math::rotation({0, 0, 1}, 3.14159265f / 2.0f);
    const auto bent = asset::skin_palette(skin, worlds);
    // The top right vertex (0.5, 2), all joint 1: 1 m above the joint, turned, ends at (-1, 1.5).
    CHECK(near(asset::skin_point(mesh.skin[9], bent, mesh.vertices[9].position), {-1.0f, 1.5f, 0.0f}));
    // (0.5, 1), half and half: halfway between staying put (0.5, 1) and turning about the joint (0, 1.5).
    CHECK(near(asset::skin_point(mesh.skin[5], bent, mesh.vertices[5].position), {0.25f, 1.25f, 0.0f}));
    // The bottom pair, all joint 0, doesn't move.
    CHECK(near(asset::skin_point(mesh.skin[0], bent, mesh.vertices[0].position), mesh.vertices[0].position));
    // Directions turn without moving: +X at the top becomes +Y.
    CHECK(near(asset::skin_direction(mesh.skin[9], bent, {1, 0, 0}), {0, 1, 0}));
}

TEST_CASE("skinning: a real file, rigid parts on bones and skinned hands under a x100 armature") {
    auto model = asset::import_gltf(kData / "RobotExpressive" / "RobotExpressive.glb");
    REQUIRE(model.has_value());
    REQUIRE(model->skins.size() == 2);  // each hand has its own skin over the whole skeleton
    for (const auto& s : model->skins) {
        CHECK(s.joints.size() == 43);
        CHECK(s.inverse_bind.size() == 43);
    }
    int skinned_nodes = 0, rigid_meshes = 0;
    for (const auto& n : model->nodes) {
        if (n.skin >= 0) {
            ++skinned_nodes;
            CHECK(!model->meshes[static_cast<std::size_t>(n.mesh)].skin.empty());
        } else if (n.mesh >= 0) {
            ++rigid_meshes;
        }
    }
    CHECK(skinned_nodes == 2);    // the hands
    CHECK(rigid_meshes >= 10);    // head, torso, arms, legs, feet: riding on their bones
    // Each skinned hand follows the joints that carry it: in the file's rest pose its skinned centre sits among them
    // (weighted by how much each pulls), on its own side. Its node's own transform would put it elsewhere: glTF ignores
    // a skinned mesh's node transform, and so do we. (The rest pose itself isn't a tidy one: viewers play Idle.)
    for (std::size_t i = 0; i < model->nodes.size(); ++i) {
        const auto& n = model->nodes[i];
        if (n.skin < 0) continue;
        const auto& skin = model->skins[static_cast<std::size_t>(n.skin)];
        const auto worlds = rest_worlds(*model, skin);
        const auto palette = asset::skin_palette(skin, worlds);
        const auto& mesh = model->meshes[static_cast<std::size_t>(n.mesh)];
        math::Vec3 centre{0, 0, 0}, joints{0, 0, 0};
        float total = 0.0f;
        for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
            centre = centre + asset::skin_point(mesh.skin[v], palette, mesh.vertices[v].position);
            const float* w = &mesh.skin[v].weights.x;
            for (int k = 0; k < 4; ++k) {
                joints = joints + worlds[mesh.skin[v].joints[static_cast<std::size_t>(k)]].transform_point({0, 0, 0}) * w[k];
                total += w[k];
            }
        }
        centre = centre * (1.0f / static_cast<float>(mesh.vertices.size()));
        joints = joints * (1.0f / total);
        CHECK(math::length(centre - joints) < 0.3f);       // among its joints
        CHECK((centre.x > 0.0f) == (n.name == "Hand.L"));  // on its own side
    }
}

TEST_CASE("skinning: Ward posed by his joints' own transforms: arms down, a knee bent, still 1.8 m of person") {
    // tools/samples/make_test_characters.py: bound in a T-pose, the file's own pose has arms down, the left elbow and
    // right knee bent, the head turned.
    for (const char* file : {"Ward_Bind.glb", "Ward_Posed.glb"}) {
        auto model = asset::import_gltf(kData / "Ward" / file);
        REQUIRE(model.has_value());
        REQUIRE(model->skins.size() == 1);
        const auto& skin = model->skins[0];
        CHECK(skin.joints.size() == 17);
        int node = -1;
        for (std::size_t i = 0; i < model->nodes.size(); ++i)
            if (model->nodes[i].skin == 0) node = static_cast<int>(i);
        REQUIRE(node >= 0);
        const auto& mesh = model->meshes[static_cast<std::size_t>(model->nodes[static_cast<std::size_t>(node)].mesh)];
        const auto palette = asset::skin_palette(skin, rest_worlds(*model, skin));
        math::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
            const auto p = asset::skin_point(mesh.skin[v], palette, mesh.vertices[v].position);
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        const bool posed = std::string(file) == "Ward_Posed.glb";
        MESSAGE(file << ": " << lo.x << " " << lo.y << " " << lo.z << " .. " << hi.x << " " << hi.y << " " << hi.z);
        CHECK(hi.y == doctest::Approx(1.8f).epsilon(0.03));          // the top of his head
        if (posed) {
            CHECK(hi.x - lo.x < 0.9f);  // arms down: much narrower than the 1.72 m T-pose span
            CHECK(lo.y > -0.05f);       // the standing foot stays on the ground (its cap a touch below)
        } else {
            CHECK(hi.x - lo.x == doctest::Approx(1.72f).epsilon(0.03));  // fingertip to fingertip
            CHECK(lo.y == doctest::Approx(0.0f).epsilon(0.05));
        }
    }
}
