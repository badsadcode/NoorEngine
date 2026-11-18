// Milestone 7, part 6 (ADR-032): levels of detail for placed models, the scene view that finds a
// mesh's model, world matrices for a whole scene at once, and cutout mipmaps that keep their
// coverage. (Instanced drawing itself needs a GPU: tests/forest_check.cmake and the golden render.)

#include "noor/asset/gltf_import.h"
#include "noor/asset/texture.h"
#include "noor/game/game.h"
#include "noor/game/model_lod.h"
#include "noor/scene/scene.h"

#include <cmath>
#include <filesystem>
#include <ostream>
#include <doctest/doctest.h>

namespace stdfs = std::filesystem;
using namespace noor;

namespace {

const stdfs::path kOak = stdfs::path(NOOR_TEST_DATA_DIR) / "noorform_oak" / "Oak_01.glb";
const float kFov60 = math::radians(60.0f);

} // namespace

TEST_CASE("lod: the oak's levels come from its manifest, and are chosen by distance, view and size") {
    const auto lods = game::read_model_lods(kOak);
    REQUIRE(lods.switches());
    REQUIRE(lods.levels.size() == 4);
    CHECK(lods.levels[0].file == kOak);
    CHECK(lods.levels[3].file.filename() == "Oak_01.lod3.glb");
    CHECK(lods.levels[3].billboard);

    // The manifest's distances (29.01, 72.52, 181.29 m) at a 60-degree view.
    CHECK(game::choose_lod(lods, 10.0f, kFov60) == 0);
    CHECK(game::choose_lod(lods, 29.0f, kFov60) == 0);
    CHECK(game::choose_lod(lods, 30.0f, kFov60) == 1);
    CHECK(game::choose_lod(lods, 100.0f, kFov60) == 2);
    CHECK(game::choose_lod(lods, 181.0f, kFov60) == 2);
    CHECK(game::choose_lod(lods, 500.0f, kFov60) == 3);
    // Zoomed in (a narrower view) keeps detail further away; a tree twice the size too.
    CHECK(game::choose_lod(lods, 50.0f, math::radians(20.0f)) == 0);
    CHECK(game::choose_lod(lods, 50.0f, kFov60, 2.0f) == 0);
    CHECK(game::choose_lod(lods, 50.0f, kFov60, 0.5f) == 2);
}

TEST_CASE("lod: no switching without a usable manifest") {
    const auto tmp = stdfs::temp_directory_path() / ("noor_lod_" + Guid::generate().to_string());
    stdfs::create_directories(tmp);
    for (const auto& e : stdfs::directory_iterator(kOak.parent_path())) stdfs::copy_file(e.path(), tmp / e.path().filename());
    CHECK(game::read_model_lods(tmp / "Oak_01.glb").levels.size() == 4);
    // A missing middle level ends the list there: levels are never skipped.
    stdfs::remove(tmp / "Oak_01.lod2.glb");
    CHECK(game::read_model_lods(tmp / "Oak_01.glb").levels.size() == 2);
    // No manifest: nothing switches (the model is always drawn whole).
    stdfs::remove(tmp / "Oak_01.noor.json");
    CHECK_FALSE(game::read_model_lods(tmp / "Oak_01.glb").switches());
    std::error_code ec;
    stdfs::remove_all(tmp, ec);
}

TEST_CASE("lod: the scene view knows which placed model a mesh belongs to") {
    auto model = asset::import_gltf(kOak);
    REQUIRE(model);
    const Guid asset = *Guid::parse("719beb3e-327d-56ed-8d54-be8882f405fd");
    scene::Scene s;
    const Guid a = scene::instantiate_model(s, asset, *model, "Oak A", {{5, 0, 0}, {}, {1, 1, 1}});
    const Guid b = scene::instantiate_model(s, asset, *model, "Oak B", {{-5, 0, 0}, {}, {1, 1, 1}});
    const auto view = game::collect_view(s, [](const Guid&) -> std::optional<stdfs::path> { return kOak; });
    REQUIRE(view.models.size() == 2);
    CHECK(view.models[0].entity == a);
    CHECK(view.models[1].entity == b);
    CHECK(view.models[0].world.transform_point({0, 0, 0}).x == doctest::Approx(5.0f));
    REQUIRE(view.meshes.size() == 4);  // trunk and leaves, twice (the capsule is collision, not a mesh)
    int in_a = 0, in_b = 0;
    for (const auto& m : view.meshes) {
        in_a += m.model_root == a ? 1 : 0;
        in_b += m.model_root == b ? 1 : 0;
    }
    CHECK(in_a == 2);
    CHECK(in_b == 2);
}

TEST_CASE("scene: world_matrices gives every entity's matrix, the same as world_matrix") {
    scene::Scene s;
    auto add = [&](std::optional<Guid> parent, math::Vec3 at, float turn) {
        scene::Entity e;
        e.id = Guid::generate();
        e.parent = parent;
        e.transform.position = at;
        e.transform.rotation = math::Quat::from_axis_angle({0, 1, 0}, turn);
        s.entities.push_back(e);
        return e.id;
    };
    const Guid root = add(std::nullopt, {1, 2, 3}, 0.5f);
    const Guid child = add(root, {0, 1, 0}, 0.25f);
    add(child, {2, 0, 0}, 1.0f);
    add(std::nullopt, {-4, 0, 0}, 0.0f);
    add(Guid::generate(), {0, 0, 7}, 0.0f);  // a parent that doesn't exist: treated as a root
    const auto all = s.world_matrices();
    REQUIRE(all.size() == s.entities.size());
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        const math::Vec3 p = all[i].transform_point({0.3f, 0.2f, 0.1f});
        const math::Vec3 q = s.world_matrix(i).transform_point({0.3f, 0.2f, 0.1f});
        CHECK(p.x == doctest::Approx(q.x));
        CHECK(p.y == doctest::Approx(q.y));
        CHECK(p.z == doctest::Approx(q.z));
    }
}

TEST_CASE("texture: cutout mipmaps keep their coverage, so far leaves don't thin out") {
    // A 64x64 RGBA TGA: alpha is fine noise (a 1-texel leaf pattern) with a quarter of the texels solid.
    const std::uint32_t n = 64;
    std::vector<std::uint8_t> tga(18 + n * n * 4, 0);
    tga[2] = 2;  // uncompressed true colour
    tga[12] = static_cast<std::uint8_t>(n);
    tga[14] = static_cast<std::uint8_t>(n);
    tga[16] = 32;
    tga[17] = 8 | 0x20;  // 8 alpha bits, top-left origin
    for (std::uint32_t i = 0; i < n * n; ++i) {
        const std::uint32_t x = i % n, y = i / n;
        tga[18 + i * 4 + 1] = 160;                                      // green
        tga[18 + i * 4 + 3] = ((x * 7 + y * 13) % 4 == 0) ? 255 : 0;  // alpha: 1 in 4
    }
    auto coverage_of = [](const asset::TextureMip& m) {
        std::size_t in = 0;
        for (std::size_t i = 3; i < m.bytes.size(); i += 4) in += m.bytes[i] >= 128 ? 1 : 0;
        return static_cast<double>(in) / static_cast<double>(m.bytes.size() / 4);
    };
    asset::TextureSettings plain;
    plain.compress = false;
    asset::TextureSettings cutout = plain;
    cutout.alpha_coverage = 0.5f;
    auto a = asset::import_texture_memory(tga, plain, "noise");
    auto b = asset::import_texture_memory(tga, cutout, "noise");
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->mips.size() >= 4);
    CHECK(coverage_of(a->mips[0]) == doctest::Approx(0.25).epsilon(0.02));
    // Averaging 1-in-4 alpha gives 0.25 everywhere: nothing passes a 0.5 cutoff, the leaves vanish.
    CHECK(coverage_of(a->mips[3]) < 0.05);
    // With coverage kept, a quarter still passes at every level.
    for (std::size_t m = 1; m + 2 < b->mips.size(); ++m) {
        INFO("mip ", m);
        CHECK(coverage_of(b->mips[m]) >= 0.2);
    }
    // And the setting is written only when used (older .noormeta files stay as they are).
    CHECK_FALSE(plain.to_json().contains("alpha_coverage"));
    CHECK(asset::TextureSettings::from_json(cutout.to_json()).alpha_coverage == doctest::Approx(0.5f));
}
