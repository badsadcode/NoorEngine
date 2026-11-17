// Materials (ADR-030): .noormat parsing, instances, loops, texture sets, GLB materials.
#include "noor/asset/gltf_import.h"
#include "noor/asset/material.h"
#include "noor/foundation/guid.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>

using namespace noor;
namespace stdfs = std::filesystem;

namespace {

// A temp folder with files and an id -> file table for the resolver.
struct Folder {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_mat_" + Guid::generate().to_string());
    std::map<Guid, stdfs::path> ids;
    Folder() { stdfs::create_directories(dir); }
    ~Folder() { stdfs::remove_all(dir); }
    Guid add(const std::string& name, const std::string& text) {
        const auto path = dir / name;
        std::ofstream(path) << text;
        const Guid id = Guid::generate();
        ids[id] = path;
        return id;
    }
    void touch(const std::string& name) { std::ofstream(dir / name) << "png"; }
    asset::AssetFileResolver resolver() const {
        return [this](const Guid& id) -> std::optional<stdfs::path> {
            const auto it = ids.find(id);
            return it == ids.end() ? std::nullopt : std::optional(it->second);
        };
    }
};

} // namespace

TEST_CASE("material: a .noormat's fields, with defaults for what's missing") {
    Folder f;
    const Guid tex = f.add("bricks_normal.png", "png");
    const Guid id = f.add("brick.noormat", R"({"noor_material": 1, "base_color": [0.5, 0.25, 0.125, 1], "roughness": 0.9,
        "normal_map": ")" + tex.to_string() + R"(", "alpha": "cutout", "double_sided": true, "uv_scale": [2, 3]})");
    auto m = asset::load_material(f.ids[id], f.resolver());
    REQUIRE(m);
    CHECK(m->base_color.y == doctest::Approx(0.25f));
    CHECK(m->roughness == doctest::Approx(0.9f));
    CHECK(m->metallic == doctest::Approx(0.0f));      // default
    CHECK(m->normal_scale == doctest::Approx(1.0f));  // default
    CHECK(m->normal_map.file == f.ids[tex]);
    CHECK(m->base_color_map.empty());
    CHECK(m->alpha == asset::AlphaMode::Cutout);
    CHECK(m->double_sided);
    CHECK(m->uv_scale.y == doctest::Approx(3.0f));
}

TEST_CASE("material: instances take their parent's values and override some") {
    Folder f;
    const Guid tex = f.add("rock_albedo.png", "png");
    const Guid parent = f.add("rock.noormat", R"({"noor_material": 1, "base_color_map": ")" + tex.to_string() +
                                                  R"(", "roughness": 0.8, "metallic": 0.1})");
    const Guid child = f.add("rock_wet.noormat", R"({"noor_material": 1, "parent": ")" + parent.to_string() +
                                                     R"(", "overrides": {"roughness": 0.2}})");
    const Guid grandchild = f.add("rock_wet_plain.noormat", R"({"noor_material": 1, "parent": ")" + child.to_string() +
                                                                R"(", "overrides": {"base_color_map": null}})");
    auto wet = asset::load_material(f.ids[child], f.resolver());
    REQUIRE(wet);
    CHECK(wet->roughness == doctest::Approx(0.2f));  // overridden
    CHECK(wet->metallic == doctest::Approx(0.1f));   // inherited
    CHECK(wet->base_color_map.file == f.ids[tex]);   // inherited
    auto plain = asset::load_material(f.ids[grandchild], f.resolver());
    REQUIRE(plain);
    CHECK(plain->base_color_map.empty());  // null removes the parent's map
    CHECK(plain->roughness == doctest::Approx(0.2f));
}

TEST_CASE("material: parent loops and missing parents are errors, unknown textures warnings") {
    Folder f;
    const Guid a = Guid::generate(), b = Guid::generate();
    f.ids[a] = f.dir / "a.noormat";
    f.ids[b] = f.dir / "b.noormat";
    std::ofstream(f.ids[a]) << R"({"noor_material": 1, "parent": ")" + b.to_string() + R"(", "overrides": {}})";
    std::ofstream(f.ids[b]) << R"({"noor_material": 1, "parent": ")" + a.to_string() + R"(", "overrides": {}})";
    auto loop = asset::load_material(f.ids[a], f.resolver());
    REQUIRE_FALSE(loop);
    CHECK(loop.error().message.find("loop") != std::string::npos);

    const Guid orphan = f.add("orphan.noormat", R"({"noor_material": 1, "parent": ")" + Guid::generate().to_string() + R"(", "overrides": {}})");
    CHECK_FALSE(asset::load_material(f.ids[orphan], f.resolver()));

    const Guid unknown = f.add("unknown.noormat", R"({"noor_material": 1, "normal_map": ")" + Guid::generate().to_string() + R"("})");
    std::vector<std::string> warnings;
    auto m = asset::load_material(f.ids[unknown], f.resolver(), &warnings);
    REQUIRE(m);
    CHECK(m->normal_map.empty());
    CHECK(warnings.size() == 1);

    CHECK_FALSE(asset::load_material(f.ids[f.add("bad.noormat", R"({"base_color": [1, 1, 1, 1]})")], f.resolver()));
}

TEST_CASE("material: texture sets are found by name, ORM maps fill both slots") {
    Folder f;
    for (const char* name : {"Bricks_Albedo.png", "Bricks_Normal.png", "Bricks_ORM.png", "Bricks_Height.png", "preview_Bricks_lit.png",
                             "thumb_Bricks.png", "Other_Albedo.png"})
        f.touch(name);
    CHECK(asset::texture_role("x_albedo.png") == asset::TextureRole::BaseColor);
    CHECK(asset::texture_role("x_base_color.png") == asset::TextureRole::BaseColor);
    CHECK(asset::texture_role("x_nor_gl.png") == asset::TextureRole::Normal);
    CHECK(asset::texture_role("x_rough.png") == asset::TextureRole::Roughness);
    CHECK(asset::texture_role("x_arm.png") == asset::TextureRole::Orm);
    CHECK(asset::texture_role("normal.png") == asset::TextureRole::None);  // no set name before the role
    CHECK(asset::texture_role("preview_x_lit.png") == asset::TextureRole::None);
    // Set names with role words in them: the last role word is the role.
    CHECK(asset::texture_role("wooden_rough_planks_arm_1k.jpg") == asset::TextureRole::Orm);
    CHECK(asset::texture_role("blue_metal_plate_diff_1k.jpg") == asset::TextureRole::BaseColor);
    CHECK(asset::texture_role("x_base_color.png") == asset::TextureRole::BaseColor);

    const auto set = asset::find_texture_set(f.dir / "Bricks_Normal.png");
    CHECK(set.name == "Bricks");
    CHECK(set.maps.size() == 4);  // albedo, height, normal, ORM; not the preview, thumbnail or Other
    REQUIRE(set.find(asset::TextureRole::Orm));
    std::map<stdfs::path, Guid> ids;  // one id per file
    const auto json = asset::material_json_for(set, [&](const stdfs::path& p) -> std::optional<Guid> {
        return ids.try_emplace(p, Guid::generate()).first->second;
    });
    CHECK(json.contains("base_color_map"));
    CHECK(json.contains("normal_map"));
    CHECK(json["metallic_roughness_map"] == json["occlusion_map"]);  // the ORM map in both
    CHECK(json["roughness"].get<double>() == doctest::Approx(1.0));  // factors multiply the map

    for (const char* name : {"wooden_rough_planks_diff_1k.jpg", "wooden_rough_planks_nor_gl_1k.jpg", "wooden_rough_planks_arm_1k.jpg",
                             "Stone_Base_Color.png", "Stone_Normal.png"})
        f.touch(name);
    const auto wood = asset::find_texture_set(f.dir / "wooden_rough_planks_arm_1k.jpg");
    CHECK(wood.name == "wooden_rough_planks");
    CHECK(wood.maps.size() == 3);
    CHECK(wood.find(asset::TextureRole::BaseColor));
    CHECK(wood.find(asset::TextureRole::Orm));
    const auto stone = asset::find_texture_set(f.dir / "Stone_Normal.png");
    CHECK(stone.name == "Stone");
    CHECK(stone.find(asset::TextureRole::BaseColor));
}

TEST_CASE("material: GLB primitives become submeshes with their glTF materials and embedded images") {
    const auto glb = stdfs::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Models" / "material_panels.glb";
    auto model = asset::import_gltf(glb);
    REQUIRE(model);
    REQUIRE(model->meshes.size() == 1);
    const auto& mesh = model->meshes[0];
    REQUIRE(mesh.submeshes.size() == 3);
    CHECK(mesh.submeshes[1].first_index == 6);
    CHECK(mesh.submeshes[2].index_count == 6);
    CHECK(mesh.submeshes[2].material == 2);
    REQUIRE(model->materials.size() == 3);
    REQUIRE(model->images.size() == 2);
    CHECK_FALSE(model->images[0].bytes.empty());
    const auto& checker = model->materials[0].material;
    CHECK(checker.base_color_map.file == glb);
    CHECK(checker.base_color_map.embedded_image == 0);
    const auto& gold = model->materials[1].material;
    CHECK(gold.metallic == doctest::Approx(1.0f));
    CHECK(gold.roughness == doctest::Approx(0.3f));
    CHECK(gold.base_color.x == doctest::Approx(1.0f));  // the factor is in the vertex colours
    // The gold submesh's own vertices carry the factor (primitives sharing one vertex list each get a copy).
    CHECK(mesh.vertices[mesh.indices[mesh.submeshes[1].first_index]].color.y == doctest::Approx(0.78f).epsilon(0.01));
    const auto& leaf = model->materials[2].material;
    CHECK(leaf.alpha == asset::AlphaMode::Cutout);
    CHECK(leaf.double_sided);
}
