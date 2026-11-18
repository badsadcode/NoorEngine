// Milestone 7, part 5: generated assets in, through Noor Interchange v0.4. The fixture is one oak
// from NoorMD's NoorForm in the NoorEngine export layout (tests/data/noorform_oak: converted from
// a real NoorForm pack by tools/interchange/noormd_engine_export.py until NoorMD writes the layout
// itself), plus the BadBuilding v0.2 asset to show older manifests still read.

#include "noor/asset/gltf_import.h"
#include "noor/asset/interchange.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/file_io.h"
#include "noor/scene/scene.h"

#include <algorithm>
#include <filesystem>
#include <ostream>
#include <doctest/doctest.h>

namespace stdfs = std::filesystem;
using namespace noor;

namespace {

const stdfs::path kData = NOOR_TEST_DATA_DIR;
const stdfs::path kOak = kData / "noorform_oak";
// UUID v5 of "noorform:tree:Oak 01:1" under NoorForm's namespace: the value the export guide predicts.
constexpr const char* kOakId = "719beb3e-327d-56ed-8d54-be8882f405fd";

struct TempDir {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_test_" + Guid::generate().to_string());
    TempDir() { stdfs::create_directories(dir); }
    ~TempDir() {
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    // The oak's files, copied into `to`.
    static void copy_oak(const stdfs::path& to) {
        stdfs::create_directories(to);
        for (const auto& e : stdfs::directory_iterator(kOak)) stdfs::copy_file(e.path(), to / e.path().filename());
    }
};

asset::Json oak_manifest() { return *asset::parse_json(*fs::read_file(kOak / "Oak_01.noor.json"), "oak"); }

// Writes `manifest` next to a copy of the oak and reads it back.
asset::ManifestRead read_changed(const TempDir& t, const asset::Json& manifest) {
    TempDir::copy_oak(t.dir);
    REQUIRE(fs::write_file_atomic(t.dir / "Oak_01.noor.json", asset::to_text(manifest), false));
    auto r = asset::read_manifest(t.dir / "Oak_01.noor.json");
    REQUIRE(r);
    return *r;
}

bool has_issue(const asset::ManifestRead& r, const std::string& field, bool error) {
    return std::ranges::any_of(r.issues, [&](const asset::ManifestIssue& i) { return i.field == field && i.error == error; });
}

} // namespace

TEST_CASE("interchange: a NoorForm oak imports as one asset with four levels") {
    TempDir t;
    auto project = asset::Project::create(t.dir / "game", "Forest");
    REQUIRE(project);
    TempDir::copy_oak(project->assets_dir() / "Trees");
    asset::AssetRegistry reg;
    auto scan = reg.scan(*project);
    REQUIRE(scan);
    // One asset: the .lod<n>.glb files belong to Oak_01.glb, and its id comes from the manifest.
    REQUIRE(reg.assets().size() == 1);
    CHECK(reg.assets()[0].path == "Assets/Trees/Oak_01.glb");
    CHECK(reg.assets()[0].guid.to_string() == kOakId);
    CHECK(reg.assets()[0].from_interchange);

    auto read = asset::read_manifest(kOak / "Oak_01.noor.json");
    REQUIRE(read);
    for (const auto& i : read->issues) INFO(i.field, ": ", i.message);
    CHECK(read->issues.empty());
    const auto& m = read->manifest;
    CHECK(m.id.to_string() == kOakId);
    CHECK(m.kind == "vegetation.tree");
    CHECK(m.generator.tool == "NoorForm");
    REQUIRE(m.recipe.seed);
    CHECK(*m.recipe.seed == 1u);
    CHECK(m.recipe.file == "Oak_01.recipe.json");
    REQUIRE(m.lods.size() == 4);
    CHECK(m.lods[0].file == "Oak_01.glb");
    CHECK(m.lods[3].file == "Oak_01.lod3.glb");
    CHECK(m.lods[0].max_distance.value_or(0) == doctest::Approx(29.01));
    CHECK(m.lods[1].max_distance.value_or(0) == doctest::Approx(72.52));
    CHECK(m.lods[2].max_distance.value_or(0) == doctest::Approx(181.29));
    CHECK_FALSE(m.lods[3].max_distance);  // the last level: no limit
    CHECK(m.lods[3].billboard);
    CHECK(m.collision == "capsule");
    REQUIRE(m.family);
    CHECK(m.family->name == "Oak");
    CHECK(m.family->count == 9);
    CHECK(m.wind["channels"]["bend"] == "TEXCOORD_1.x");  // kept, not used yet
    CHECK(std::ranges::find(m.tool_blocks, std::string("noorform")) != m.tool_blocks.end());

    // Every level opens and agrees with the manifest.
    const auto levels = asset::check_levels(*read, kOak);
    REQUIRE(levels.size() == 4);
    for (const auto& l : levels) {
        INFO("level ", l.level);
        for (const auto& i : l.issues) INFO(i);
        CHECK(l.exists);
        CHECK(l.issues.empty());
        CHECK(l.triangles == l.manifest_triangles);
        CHECK(l.collision_nodes == (l.level == 0 ? 1 : 0));
    }
    CHECK(levels[0].triangles == 15268u);
    CHECK(levels[0].parts == std::vector<std::string>{"trunk", "leaves"});
    CHECK(levels[3].triangles == 8u);
}

TEST_CASE("interchange: the oak's trunk capsule is collision, never drawn, and keeps its size in a scene") {
    auto model = asset::import_gltf(kOak / "Oak_01.glb");
    REQUIRE(model);
    CHECK(model->render_triangles() == 15268u);  // the capsule adds nothing to draw
    const auto capsule = std::ranges::find_if(model->nodes, [](const asset::ImportedNode& n) { return n.is_collision(); });
    REQUIRE(capsule != model->nodes.end());
    CHECK(capsule->name == "Trunk-colonly");
    CHECK(capsule->mesh == -1);

    scene::Scene s;
    scene::instantiate_model(s, *Guid::parse(kOakId), *model, "Oak_01", {});
    const auto e = std::ranges::find_if(s.entities, [](const scene::Entity& x) { return x.name == "Trunk-colonly"; });
    REQUIRE(e != s.entities.end());
    CHECK_FALSE(e->components.contains("mesh"));
    REQUIRE(e->components.contains("collider"));
    const auto& c = e->components["collider"];
    CHECK(c["shape"] == "capsule");
    CHECK(c["radius"].get<double>() == doctest::Approx(0.682));
    CHECK(c["height"].get<double>() == doctest::Approx(2.5));
}

TEST_CASE("interchange: mistakes in a manifest are issues on their field") {
    SUBCASE("a seed written as a float, a string or a negative number") {
        for (const asset::Json bad : {asset::Json(1.0), asset::Json("1"), asset::Json(-1)}) {
            TempDir t;
            auto j = oak_manifest();
            j["recipe"]["seed"] = bad;
            const auto r = read_changed(t, j);
            CHECK(has_issue(r, "recipe.seed", true));
            CHECK_FALSE(r.manifest.recipe.seed);
        }
    }
    SUBCASE("the largest seed survives exactly") {
        TempDir t;
        auto j = oak_manifest();
        j["recipe"]["seed"] = std::uint64_t{18446744073709551615u};
        const auto r = read_changed(t, j);
        REQUIRE(r.manifest.recipe.seed);
        CHECK(*r.manifest.recipe.seed == 18446744073709551615u);
        CHECK_FALSE(has_issue(r, "recipe.seed", true));
    }
    SUBCASE("the wrong space") {
        TempDir t;
        auto j = oak_manifest();
        j["space"]["up"] = "+Z";
        j["space"]["units"] = "centimeters";
        const auto r = read_changed(t, j);
        CHECK(has_issue(r, "space.up", true));
        CHECK(has_issue(r, "space.units", true));
    }
    SUBCASE("a path out of the folder") {
        TempDir t;
        auto j = oak_manifest();
        j["files"][1]["path"] = "../../Windows/evil.glb";
        const auto r = read_changed(t, j);
        CHECK(has_issue(r, "files[1]", true));
        CHECK(r.has_errors());
    }
    SUBCASE("distances that don't grow, a gap in the levels") {
        TempDir t;
        auto j = oak_manifest();
        j["lods"][1]["max_distance"] = 10.0;
        auto r = read_changed(t, j);
        CHECK(has_issue(r, "lods", true));
        TempDir t2;
        j = oak_manifest();
        j["lods"].erase(1);
        r = read_changed(t2, j);
        CHECK(has_issue(r, "lods", true));
    }
    SUBCASE("a missing level file and a changed one are warnings; the level check says so") {
        TempDir t;
        TempDir::copy_oak(t.dir);
        stdfs::remove(t.dir / "Oak_01.lod2.glb");
        auto lod1 = *fs::read_file(t.dir / "Oak_01.lod1.glb");
        REQUIRE(fs::write_file_atomic(t.dir / "Oak_01.lod1.glb", lod1 + std::string(4, '\0'), false));  // padding: still a GLB
        auto r = asset::read_manifest(t.dir / "Oak_01.noor.json");
        REQUIRE(r);
        CHECK(has_issue(*r, "files[1]", false));  // lod1: changed since export
        CHECK(has_issue(*r, "files[2]", false));  // lod2: not there
        CHECK_FALSE(r->has_errors());
        const auto levels = asset::check_levels(*r, t.dir);
        REQUIRE(levels.size() == 4);
        CHECK_FALSE(levels[2].exists);
        CHECK_FALSE(levels[2].issues.empty());
    }
    SUBCASE("versions: a newer minor loads with a warning, a newer major doesn't") {
        TempDir t;
        auto j = oak_manifest();
        j["noor_interchange"] = "0.9";
        auto r = read_changed(t, j);
        CHECK(has_issue(r, "noor_interchange", false));
        CHECK_FALSE(r.has_errors());
        TempDir t2;
        j["noor_interchange"] = "1.0";
        r = read_changed(t2, j);
        CHECK(has_issue(r, "noor_interchange", true));
    }
    SUBCASE("not a manifest at all") {
        TempDir t;
        REQUIRE(fs::write_file_atomic(t.dir / "x.noor.json", R"({"name": "just JSON"})", false));
        CHECK_FALSE(asset::read_manifest(t.dir / "x.noor.json"));
    }
}

TEST_CASE("interchange: an older manifest (BadBuilding, v0.2) still reads") {
    auto r = asset::read_manifest(kData / "badbuilding" / "osm_w117821721.noor.json");
    REQUIRE(r);
    for (const auto& i : r->issues) INFO(i.field, ": ", i.message);
    CHECK_FALSE(r->has_errors());  // its LOD files aren't in the repository: warnings only
    CHECK(r->manifest.kind == "building");
    REQUIRE(r->manifest.recipe.seed);
    CHECK(*r->manifest.recipe.seed > (std::uint64_t{1} << 63));  // the seed above 2^63 (v0.3's reason)
    REQUIRE_FALSE(r->manifest.lods.empty());
    CHECK(r->manifest.lods[0].file == "osm_w117821721.glb");
}
