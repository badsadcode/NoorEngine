// Milestone 2 tests: projects, the asset registry, GLB import (real BadBuilding output) and scenes.
// The registry tests include the Milestone 2 gate: a scene keeps working after its asset is
// renamed or moved, both through the engine and behind its back (Explorer/Git style).

#include "noor/asset/gltf_import.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/file_io.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <ostream>
#include <doctest/doctest.h>

namespace stdfs = std::filesystem;
using namespace noor;

namespace {

const stdfs::path kData = NOOR_TEST_DATA_DIR;
const stdfs::path kBuildingGlb = kData / "badbuilding" / "osm_w117821721.glb";
constexpr const char* kBuildingId = "4529993a-ec0c-5a66-bc58-462a7823fbc6";  // from its .noor.json

// A fresh project in a temp folder, deleted at the end of the test.
struct TempProject {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_test_" + Guid::generate().to_string());
    asset::Project project = *asset::Project::create(dir, "TestGame");
    ~TempProject() {
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    // Copies the sample building (GLB + manifest + recipe) into Assets/<folder>.
    void add_building(const std::string& folder) const {
        const stdfs::path to = project.assets_dir() / folder;
        stdfs::create_directories(to);
        for (const char* f : {"osm_w117821721.glb", "osm_w117821721.noor.json", "osm_w117821721.recipe.json"}) {
            stdfs::copy_file(kData / "badbuilding" / f, to / f);
        }
    }
};

} // namespace

// ---------------------------------------------------------------- projects

TEST_CASE("project: create, reopen, and refuse a second project in the same folder") {
    TempProject t;
    CHECK(stdfs::exists(t.project.file()));
    CHECK(stdfs::is_directory(t.project.assets_dir()));
    auto reopened = asset::Project::open(t.dir);
    REQUIRE(reopened);
    CHECK(reopened->info().id == t.project.info().id);
    CHECK(reopened->info().name == "TestGame");
    CHECK_FALSE(asset::Project::create(t.dir, "Again"));
}

TEST_CASE("project: paths can't escape the project folder") {
    TempProject t;
    CHECK(t.project.resolve("Assets/a.glb"));
    for (const char* bad : {"../outside.glb", "Assets/../../x", "C:/Windows/system.ini", "/etc/passwd", ""}) {
        CAPTURE(bad);
        CHECK_FALSE(t.project.resolve(bad));
    }
}

TEST_CASE("project: a newer project format is refused with a clear message") {
    TempProject t;
    REQUIRE(fs::write_file_atomic(t.project.file(),
                                  R"({"noor_project": 99, "id": "4529993a-ec0c-5a66-bc58-462a7823fbc6"})", false));
    auto p = asset::Project::open(t.dir);
    REQUIRE_FALSE(p);
    CHECK(p.error().code == ErrorCode::VersionMismatch);
}

// ---------------------------------------------------------------- GLB import

TEST_CASE("gltf: imports the real BadBuilding block with its Noor Interchange data") {
    auto model = asset::import_gltf(kBuildingGlb);
    REQUIRE(model);
    CHECK(model->nodes.size() == 28);
    CHECK(model->meshes.size() == 22);
    CHECK(model->warnings.empty());
    // The manifest says LOD0 has 7,432 triangles; the collision mass adds 28.
    CHECK(model->render_triangles() == 7432);
    CHECK(model->collision_triangles() == 28);

    int entrances = 0, collisions = 0;
    for (std::size_t i = 0; i < model->nodes.size(); ++i) {
        const auto& n = model->nodes[i];
        CHECK(n.parent < static_cast<int>(i));  // parents come first
        if (n.is_marker() && n.noor["marker"] == "entrance") ++entrances;
        if (n.is_collision()) ++collisions;
    }
    CHECK(entrances == 4);
    CHECK(collisions == 1);
    CHECK(model->nodes[0].noor.value("id", std::string{}) == kBuildingId);
}

TEST_CASE("gltf: garbage and truncated files fail cleanly") {
    TempProject t;
    const stdfs::path bad = t.dir / "bad.glb";
    REQUIRE(fs::write_file_atomic(bad, "glTF this is not", false));
    CHECK_FALSE(asset::import_gltf(bad));

    auto real = fs::read_file(kBuildingGlb);
    REQUIRE(real);
    REQUIRE(fs::write_file_atomic(bad, real->substr(0, real->size() / 2), false));
    CHECK_FALSE(asset::import_gltf(bad));
}

// ---------------------------------------------------------------- registry

TEST_CASE("registry: a Noor Interchange asset keeps the id from its manifest") {
    TempProject t;
    t.add_building("Buildings");
    asset::AssetRegistry reg;
    auto r = reg.scan(t.project);
    REQUIRE(r);
    CHECK(r->assets == 1);
    CHECK(r->created == 1);
    REQUIRE(reg.assets().size() == 1);
    CHECK(reg.assets()[0].guid.to_string() == kBuildingId);
    CHECK(reg.assets()[0].from_interchange);
    CHECK(stdfs::exists(t.project.assets_dir() / "Buildings" / "osm_w117821721.glb.noormeta"));

    auto again = reg.scan(t.project);  // idempotent
    REQUIRE(again);
    CHECK(again->created == 0);
    CHECK(again->moved == 0);
}

TEST_CASE("M2 gate: a scene survives renaming and moving its asset, in and outside the engine") {
    TempProject t;
    t.add_building("Buildings");
    asset::AssetRegistry reg;
    REQUIRE(reg.scan(t.project));
    const Guid id = reg.assets()[0].guid;

    // Build and save a scene that uses the building.
    auto model = asset::import_gltf(t.project.assets_dir() / "Buildings" / "osm_w117821721.glb");
    REQUIRE(model);
    scene::Scene s;
    s.name = "Main";
    scene::instantiate_model(s, id, *model, "Block");
    const stdfs::path scene_file = t.project.scenes_dir() / "Main.noorscene";
    REQUIRE(s.save(scene_file));
    const auto exists = [&](const Guid& g) { return reg.find(g) != nullptr; };

    // 1. Rename inside the engine.
    REQUIRE(reg.move_asset(t.project, "Assets/Buildings/osm_w117821721.glb", "Assets/Buildings/block_a.glb"));
    CHECK(reg.find(id)->path == "Assets/Buildings/block_a.glb");

    // 2. Move behind the engine's back: only the GLB moves, its meta stays behind.
    stdfs::create_directories(t.project.assets_dir() / "Warsaw" / "Mokotow");
    stdfs::rename(t.project.assets_dir() / "Buildings" / "block_a.glb",
                  t.project.assets_dir() / "Warsaw" / "Mokotow" / "panel_block.glb");
    auto r = reg.scan(t.project);
    REQUIRE(r);
    CHECK(r->moved == 1);
    CHECK(r->orphaned_metas.empty());
    REQUIRE(reg.find(id));
    CHECK(reg.find(id)->path == "Assets/Warsaw/Mokotow/panel_block.glb");

    // 3. The saved scene still resolves every reference.
    auto reloaded = scene::Scene::load(scene_file);
    REQUIRE(reloaded);
    CHECK(scene::validate(*reloaded, exists).empty());
}

TEST_CASE("registry: a copied asset (file + meta) gets a new id instead of a duplicate") {
    TempProject t;
    t.add_building("A");
    asset::AssetRegistry reg;
    REQUIRE(reg.scan(t.project));
    stdfs::create_directories(t.project.assets_dir() / "B");
    for (const char* f : {"osm_w117821721.glb", "osm_w117821721.glb.noormeta"}) {
        stdfs::copy_file(t.project.assets_dir() / "A" / f, t.project.assets_dir() / "B" / f);
    }
    auto r = reg.scan(t.project);
    REQUIRE(r);
    REQUIRE(reg.assets().size() == 2);
    CHECK(reg.assets()[0].guid != reg.assets()[1].guid);
    CHECK(r->warnings.size() == 1);
}

TEST_CASE("registry: a deleted asset leaves an orphaned meta that is reported, never deleted") {
    TempProject t;
    t.add_building("Buildings");
    asset::AssetRegistry reg;
    REQUIRE(reg.scan(t.project));
    stdfs::remove(t.project.assets_dir() / "Buildings" / "osm_w117821721.glb");
    auto r = reg.scan(t.project);
    REQUIRE(r);
    CHECK(r->orphaned_metas.size() == 1);
    CHECK(stdfs::exists(t.project.assets_dir() / "Buildings" / "osm_w117821721.glb.noormeta"));
}

// ---------------------------------------------------------------- scenes

TEST_CASE("scene: save -> load -> save is byte-identical (deterministic files)") {
    auto model = asset::import_gltf(kBuildingGlb);
    REQUIRE(model);
    scene::Scene s;
    s.name = "Roundtrip";
    scene::instantiate_model(s, *Guid::parse(kBuildingId), *model, "Block",
                             {{1.5f, 0, -3}, math::Quat::from_axis_angle({0, 1, 0}, 0.3f), {1, 1, 1}});
    const std::string first = s.to_text();
    auto loaded = scene::Scene::from_text(first, "memory");
    REQUIRE(loaded);
    CHECK(loaded->to_text() == first);
    CHECK(first.find('\r') == std::string::npos);
    CHECK(first.back() == '\n');
}

TEST_CASE("scene: instantiating a model maps markers, colliders and meshes") {
    auto model = asset::import_gltf(kBuildingGlb);
    REQUIRE(model);
    scene::Scene s;
    scene::instantiate_model(s, *Guid::parse(kBuildingId), *model, "Block");
    CHECK(s.entities.size() == model->nodes.size() + 1);  // + root
    int markers = 0, colliders = 0, meshes = 0;
    for (const auto& e : s.entities) {
        markers += e.has("marker");
        colliders += e.has("collider");
        meshes += e.has("mesh");
        CHECK_FALSE((e.has("collider") && e.has("mesh")));  // collision is never rendered
    }
    CHECK(markers == 4);
    CHECK(colliders == 1);
    CHECK(meshes == 21);  // 22 meshes, one of which is the collision mass
    CHECK(scene::validate_structure(s).empty());
}

TEST_CASE("scene: unknown component types survive load and save untouched") {
    const char* text = R"({
  "noor_scene": 1,
  "id": "11111111-2222-4333-8444-555555555555",
  "name": "Plugins",
  "entities": [
    {
      "id": "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
      "name": "Tree",
      "parent": null,
      "transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
      "components": {"noorvegetation.wind": {"strength": 0.7, "gusts": [1, 2, 3]}}
    }
  ]
}
)";
    auto s = scene::Scene::from_text(text, "memory");
    REQUIRE(s);
    auto again = scene::Scene::from_text(s->to_text(), "memory");
    REQUIRE(again);
    CHECK(again->entities[0].components["noorvegetation.wind"]["gusts"][2] == 3);
}

TEST_CASE("scene: broken documents are rejected with the reason") {
    CHECK_FALSE(scene::Scene::from_text("{}", "memory"));
    CHECK_FALSE(scene::Scene::from_text(R"({"noor_scene": 1})", "memory"));  // no id
    auto future = scene::Scene::from_text(R"({"noor_scene": 7, "id": "11111111-2222-4333-8444-555555555555"})", "m");
    REQUIRE_FALSE(future);
    CHECK(future.error().code == ErrorCode::VersionMismatch);
}

TEST_CASE("scene: validation finds missing assets and broken parents") {
    scene::Scene s;
    scene::Entity child;
    child.id = Guid::generate();
    child.name = "Orphan";
    child.parent = Guid::generate();  // does not exist
    child.components["mesh"] = {{"asset", Guid::generate().to_string()}, {"mesh_index", 0}};
    s.entities.push_back(child);
    const auto issues = scene::validate(s, [](const Guid&) { return false; });
    CHECK(issues.size() == 2);
}
