// CSG kernel spike (Milestone 6, part 1, ADR-016): brush shapes are closed solids of the right
// volume, and the cases that break CSG kernels work: a doorway (with a coplanar floor edge),
// rotated and non-uniformly scaled brushes, touching boxes, a brush inside another, 100 cuts in one
// wall. Materials and UVs must follow source faces through the cuts.
#include "noor/csg/csg.h"
#include "noor/asset/gltf_export.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/csg/scene_csg.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/sha256.h"
#include "noor/game/cooked_csg.h"
#include "noor/game/game.h"


#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

using namespace noor;
using csg::Brush;
using csg::Operation;
using csg::Shape;
using math::Vec3;

namespace {

Brush make(Shape shape, Vec3 size, Vec3 at = {0, 0, 0}, Operation op = Operation::Add, std::uint32_t material = 0) {
    Brush b;
    b.shape = shape;
    b.size = size;
    b.operation = op;
    b.transform = math::translation(at);
    b.surface.material = material;
    return b;
}

csg::Model build(std::vector<Brush> brushes) {
    auto m = csg::build_model(brushes);
    REQUIRE_MESSAGE(m, (m ? "" : m.error().describe()));
    CHECK_MESSAGE(m->warnings.empty(), (m->warnings.empty() ? "" : m->warnings[0]));
    return std::move(*m);
}

double polygon_area(std::uint32_t n, double r) { return 0.5 * n * r * r * std::sin(2.0 * math::kPi / n); }

Vec3 centroid(const csg::Model& m, std::size_t tri) {
    const Vec3 a = m.vertices[m.indices[tri * 3]].position, b = m.vertices[m.indices[tri * 3 + 1]].position,
               c = m.vertices[m.indices[tri * 3 + 2]].position;
    return (a + b + c) * (1.0f / 3.0f);
}

} // namespace

TEST_CASE("csg: every brush shape is a closed solid with the volume it should have") {
    CHECK(build({make(Shape::Box, {2, 3, 4})}).volume == doctest::Approx(24.0).epsilon(1e-4));
    CHECK(build({make(Shape::Wedge, {2, 2, 2})}).volume == doctest::Approx(4.0).epsilon(1e-4));
    Brush cyl = make(Shape::Cylinder, {2, 2, 2});
    cyl.sides = 64;
    CHECK(build({cyl}).volume == doctest::Approx(polygon_area(64, 1.0) * 2.0).epsilon(1e-4));
    Brush cone = make(Shape::Cone, {2, 3, 2});
    cone.sides = 32;
    CHECK(build({cone}).volume == doctest::Approx(polygon_area(32, 1.0) * 3.0 / 3.0).epsilon(1e-4));
    Brush ball = make(Shape::Sphere, {2, 2, 2});
    ball.sides = 48;
    CHECK(build({ball}).volume == doctest::Approx(4.0 / 3.0 * math::kPi).epsilon(0.02));
    Brush stairs = make(Shape::Stairs, {1, 1, 1});
    stairs.steps = 4;
    CHECK(build({stairs}).volume == doctest::Approx(0.625).epsilon(1e-4));  // (1+2+3+4)/4 steps of 1/4 x 1/4
    Brush arch = make(Shape::Arch, {1, 2, 0.2f});
    arch.sides = 64;
    // A 1 x 1.5 rectangle plus a half circle of radius 0.5 (as a 32-segment half polygon), 0.2 deep.
    CHECK(build({arch}).volume == doctest::Approx((1.5 + polygon_area(64, 0.5) / 2.0) * 0.2).epsilon(1e-4));
    for (const Shape s : {Shape::Box, Shape::Wedge, Shape::Cylinder, Shape::Cone, Shape::Sphere, Shape::Stairs, Shape::Arch})
        for (std::uint32_t f = 0; f < csg::face_count(s); ++f) CHECK(csg::face_name(s, f) != "?");
}

TEST_CASE("csg: a doorway cut into a wall keeps the wall's surface, the new faces get the cutter's") {
    // A brick wall (material 1, 2 repeats per metre) and a door (material 2) whose bottom is
    // coplanar with the wall's: the classic case that leaves slivers in weak kernels.
    Brush wall = make(Shape::Box, {4, 3, 0.3f}, {0, 0, 0}, Operation::Add, 1);
    wall.surface.scale = {2, 2};
    const Brush door = make(Shape::Box, {1, 2, 1}, {0, -0.5f, 0}, Operation::Subtract, 2);
    const auto model = build({wall, door});
    CHECK(model.volume == doctest::Approx(4 * 3 * 0.3 - 1 * 2 * 0.3).epsilon(1e-4));

    std::size_t wall_front = 0, jambs = 0;
    for (std::size_t t = 0; t < model.triangle_count(); ++t) {
        const auto src = model.triangle_source[t];
        if (src.brush == 0 && src.face == 4) ++wall_front;
        if (src.brush == 1) {
            ++jambs;
            // The door's faces inside the opening: material 2.
            const auto sub = std::find_if(model.submeshes.begin(), model.submeshes.end(), [&](const auto& s) {
                return t * 3 >= s.first_index && t * 3 < s.first_index + s.index_count;
            });
            REQUIRE(sub != model.submeshes.end());
            CHECK(sub->material == 2);
            // Every exposed door face lies inside the wall's thickness.
            CHECK(std::abs(centroid(model, t).z) <= 0.15f + 1e-4f);
        }
    }
    CHECK(wall_front > 2);
    CHECK(jambs >= 6);  // two sides and the top, each a quad (the bottom is open)
    CHECK(model.submeshes.size() == 2);

    // UVs are anchored in model space: on the wall's front (+Z) face u = x, v = -y, times the scale,
    // with or without the door.
    for (std::size_t i = 0; i < model.indices.size(); ++i) {
        const auto src = model.triangle_source[i / 3];
        if (src.brush != 0 || src.face != 4) continue;
        const auto& v = model.vertices[model.indices[i]];
        CHECK(v.uv.x == doctest::Approx(v.position.x * 2).epsilon(1e-5));
        CHECK(v.uv.y == doctest::Approx(-v.position.y * 2).epsilon(1e-5));
        CHECK(v.normal.z == doctest::Approx(1.0f));
        CHECK(std::abs(v.tangent.w) == doctest::Approx(1.0f));
    }
}

TEST_CASE("csg: rotated and non-uniformly scaled brushes cut exactly") {
    const math::Mat4 turn = math::rotation({0, 1, 0}, math::radians(37.0f));
    Brush wall = make(Shape::Box, {6, 3, 0.3f});
    wall.transform = turn;
    // A unit box stretched to 0.5 x 2 x 3, turned with the wall: a 0.5 x 2 hole through 0.3 m.
    Brush window = make(Shape::Box, {1, 1, 1}, {}, Operation::Subtract);
    window.transform = turn * math::scale({0.5f, 2.0f, 3.0f});
    CHECK(build({wall, window}).volume == doctest::Approx(6 * 3 * 0.3 - 0.5 * 2 * 0.3).epsilon(1e-4));

    // Tilted 20 degrees the other way: the hole is a slanted prism; its volume is the window's
    // cross-section times the thickness divided by cos(20 degrees) along the slant.
    Brush slanted = make(Shape::Box, {0.5f, 0.5f, 3}, {}, Operation::Subtract);
    slanted.transform = math::rotation({1, 0, 0}, math::radians(20.0f));
    const auto m = build({make(Shape::Box, {6, 3, 0.3f}), slanted});
    CHECK(m.volume == doctest::Approx(6 * 3 * 0.3 - 0.5 * 0.5 * 0.3 / std::cos(math::radians(20.0f))).epsilon(1e-3));

    // A mirrored brush (negative scale) is still a solid, not an inside-out one.
    Brush mirrored = make(Shape::Wedge, {2, 2, 2});
    mirrored.transform = math::scale({-1, 1, 1});
    CHECK(build({mirrored}).volume == doctest::Approx(4.0).epsilon(1e-4));
}

TEST_CASE("csg: touching boxes merge without an inside wall, a brush inside another makes a hollow") {
    const auto merged = build({make(Shape::Box, {1, 1, 1}), make(Shape::Box, {1, 1, 1}, {1, 0, 0})});
    CHECK(merged.volume == doctest::Approx(2.0).epsilon(1e-4));
    for (std::size_t t = 0; t < merged.triangle_count(); ++t)
        CHECK_MESSAGE(std::abs(centroid(merged, t).x - 0.5f) > 1e-3f, "a face was left between the two boxes");

    const auto hollow = build({make(Shape::Box, {2, 2, 2}), make(Shape::Box, {1, 1, 1}, {}, Operation::Subtract, 3)});
    CHECK(hollow.volume == doctest::Approx(7.0).epsilon(1e-4));
    CHECK(std::any_of(hollow.triangle_source.begin(), hollow.triangle_source.end(), [](const auto& s) { return s.brush == 1; }));

    const auto lens = build({make(Shape::Box, {2, 2, 2}), make(Shape::Sphere, {2.4f, 2.4f, 2.4f}, {}, Operation::Intersect)});
    CHECK(lens.volume > 4.0);  // more than the ball's inscribed...
    CHECK(lens.volume < 8.0);  // ...less than the box

    // Nothing to cut from yet: an empty model, not an error.
    auto empty = csg::build_model(std::vector<Brush>{make(Shape::Box, {1, 1, 1}, {}, Operation::Subtract)});
    REQUIRE(empty);
    CHECK(empty->triangle_count() == 0);
}

TEST_CASE("csg: a wall with 100 windows, built twice, is the same to the byte") {
    std::vector<Brush> brushes{make(Shape::Box, {10, 3, 0.3f}, {}, Operation::Add, 1)};
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 20; ++col)
            brushes.push_back(make(Shape::Box, {0.2f, 0.2f, 1}, {-4.75f + col * 0.5f, -1.0f + row * 0.5f, 0}, Operation::Subtract, 2));
    const auto start = std::chrono::steady_clock::now();
    const auto first = build(brushes);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("101 brushes: " << ms << " ms, " << first.triangle_count() << " triangles, " << first.vertices.size() << " vertices");
    CHECK(first.volume == doctest::Approx(10 * 3 * 0.3 - 100 * 0.2 * 0.2 * 0.3).epsilon(1e-4));
    const auto second = build(brushes);
    REQUIRE(second.vertices.size() == first.vertices.size());
    CHECK(second.indices == first.indices);
    CHECK(std::memcmp(second.vertices.data(), first.vertices.data(), first.vertices.size() * sizeof(csg::Vertex)) == 0);
}

TEST_CASE("csg: one face can have its own material, and its texture can turn with the brush") {
    // A wall turned 37 degrees: its front face has material 2, aligned with the brush; the rest
    // material 1, aligned with the world.
    const float angle = math::radians(37.0f);
    Brush wall = make(Shape::Box, {4, 3, 0.3f}, {}, Operation::Add, 1);
    wall.transform = math::rotation({0, 1, 0}, angle);
    wall.faces.resize(6, wall.surface);
    wall.faces[4].material = 2;
    wall.faces[4].align = csg::Align::Brush;
    const auto model = build({wall});
    const Vec3 along{std::cos(angle), 0, -std::sin(angle)};  // the wall's own +X, turned
    std::size_t front = 0;
    for (std::size_t t = 0; t < model.triangle_count(); ++t) {
        const auto src = model.triangle_source[t];
        const auto sub = std::find_if(model.submeshes.begin(), model.submeshes.end(), [&](const auto& s) {
            return t * 3 >= s.first_index && t * 3 < s.first_index + s.index_count;
        });
        REQUIRE(sub != model.submeshes.end());
        CHECK(sub->material == (src.face == 4 ? 2u : 1u));
        if (src.face != 4) continue;
        ++front;
        for (int k = 0; k < 3; ++k) {
            const auto& v = model.vertices[model.indices[t * 3 + k]];
            // u runs along the wall (not along a world axis), v straight down: rows of bricks stay level.
            CHECK(v.uv.x == doctest::Approx(math::dot(v.position, along)).epsilon(1e-4));
            CHECK(v.uv.y == doctest::Approx(-v.position.y).epsilon(1e-4));
        }
    }
    CHECK(front == 2);
    CHECK(model.submeshes.size() == 2);
}

// ---- Milestone 6, part 4: cooking and export ----


namespace {

// A scene with one CSG model: a wall (material `wall_material`) with a doorway cut through it.
scene::Scene wall_scene(const std::optional<Guid>& wall_material) {
    scene::Scene s;
    scene::Entity model;
    model.id = *Guid::parse("c5600000-0002-4000-8000-000000000001");
    model.name = "House";
    model.components["csg_model"] = asset::Json::object();
    model.components["collider"] = {{"shape", "mesh"}};
    s.entities.push_back(model);
    scene::Entity wall;
    wall.id = *Guid::parse("c5600000-0002-4000-8000-000000000002");
    wall.parent = model.id;
    wall.transform.position = {0, 1.5f, 0};
    wall.components["brush"] = {{"shape", "box"}, {"size", {4.0, 3.0, 0.3}},
                                {"material", wall_material ? asset::Json(wall_material->to_string()) : asset::Json(nullptr)}};
    s.entities.push_back(wall);
    scene::Entity door;
    door.id = *Guid::parse("c5600000-0002-4000-8000-000000000003");
    door.parent = model.id;
    door.transform.position = {0, 1.1f, 0};
    door.components["brush"] = {{"shape", "arch"}, {"operation", "subtract"}, {"size", {1.0, 2.2, 1.0}}};
    s.entities.push_back(door);
    return s;
}

} // namespace

TEST_CASE("csg cooking: a model cooks, reads back the same, and its key follows its brushes") {
    const auto s = wall_scene(std::nullopt);
    auto cooked = csg::cook_model(s, 0);
    REQUIRE(cooked);
    CHECK(cooked->indices.size() > 36);
    CHECK(cooked->submeshes.size() == 1);
    CHECK_FALSE(cooked->submeshes[0].material);
    CHECK(cooked->key == game::csg_model_key(s, 0));

    const auto dir = std::filesystem::temp_directory_path() / ("noor_cook_" + Guid::generate().to_string());
    const auto file = game::cooked_csg_path(dir, s.entities[0].id);
    REQUIRE(game::write_cooked_csg(file, *cooked));
    auto back = game::read_cooked_csg(file);
    REQUIRE(back);
    CHECK(back->key == cooked->key);
    CHECK(back->indices == cooked->indices);
    CHECK(std::memcmp(back->vertices.data(), cooked->vertices.data(), cooked->vertices.size() * sizeof(render::Vertex)) == 0);
    REQUIRE(fs::write_file_atomic(file, "NCSG but not really", false));
    CHECK_FALSE(game::read_cooked_csg(file));  // damaged files are errors, not crashes
    std::filesystem::remove_all(dir);

    // The key: the same scene, the same key; a moved doorway, another key.
    auto moved = s;
    CHECK(game::csg_model_key(moved, 0) == cooked->key);
    moved.entities[2].transform.position.x = 1.0f;
    CHECK(game::csg_model_key(moved, 0) != cooked->key);
}

TEST_CASE("csg export: a Noor Interchange GLB with embedded textures, a collision node and a manifest") {
    // The brick material of the render test project (a normal map, PNG).
    const auto project = asset::Project::open(std::filesystem::path(NOOR_TEST_DATA_DIR) / "render_scenes");
    REQUIRE(project);
    asset::AssetRegistry registry;
    REQUIRE(registry.scan(*project));
    const Guid brick = *Guid::parse("7e5d0000-3a70-4000-8000-000000000001");
    const auto s = wall_scene(brick);
    auto cooked = csg::cook_model(s, 0);
    REQUIRE(cooked);
    REQUIRE(cooked->submeshes.size() == 2);  // the brick wall, the doorway's own faces (no material)

    const auto resolve = game::file_resolver(game::registry_asset_path(*project, registry));
    asset::ExportModel em;
    em.name = "House";
    em.id = s.entities[0].id;
    em.generator_version = "test";
    for (const auto& v : cooked->vertices) em.vertices.push_back({v.position, v.normal, v.color, v.uv, v.tangent});
    em.indices = cooked->indices;
    for (const auto& sm : cooked->submeshes)
        em.parts.push_back({sm.first_index, sm.index_count, sm.material ? resolve(*sm.material).value_or(std::filesystem::path{}) : std::filesystem::path{}});
    const auto dir = std::filesystem::temp_directory_path() / ("noor_export_" + Guid::generate().to_string());
    std::vector<std::string> warnings;
    auto manifest = asset::export_noor_glb(em, resolve, dir / "House.glb", &warnings);
    REQUIRE_MESSAGE(manifest, (manifest ? "" : manifest.error().describe()));

    // Read back with our own importer: the visible mesh, the collision node (flagged, "-colonly"),
    // the brick's normal map embedded.
    auto model = asset::import_gltf(dir / "House.glb");
    REQUIRE_MESSAGE(model, (model ? "" : model.error().describe()));
    CHECK(model->meshes.size() == 2);
    const auto collision = std::ranges::find_if(model->nodes, [](const auto& n) { return n.is_collision(); });
    REQUIRE(collision != model->nodes.end());
    CHECK(collision->name == "House-colonly");
    CHECK(collision->noor["collision"] == "mesh");
    CHECK(model->images.size() == 1);
    std::size_t triangles = 0;
    for (const auto& sm : model->meshes[0].submeshes) triangles += sm.index_count / 3;
    CHECK(triangles == cooked->indices.size() / 3);

    // The manifest (spec section 4): version, the model's id, the GLB's hash, honest validation.
    auto text = fs::read_file(dir / "House.noor.json");
    REQUIRE(text);
    const auto json = asset::parse_json(*text, "manifest");
    REQUIRE(json);
    CHECK((*json)["noor_interchange"] == "0.3");
    CHECK((*json)["id"] == s.entities[0].id.to_string());
    CHECK((*json)["collision"] == "mesh");
    CHECK((*json)["validation"]["gltf_validator"] == "not_run");
    CHECK((*json)["files"][0]["sha256"] == Sha256::hex(*fs::read_file(dir / "House.glb", 1u << 26)));
    CHECK((*json)["lods"][0]["triangles"] == triangles);
    std::filesystem::remove_all(dir);
}
