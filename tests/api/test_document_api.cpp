// Drives noor_editor_api's document functions exactly as NoorEditor does (ADR-025), through the
// DLL's C ABI. No window and no GPU: runs in CI.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"

#include "noor/asset/json.h"
#include "noor/foundation/guid.h"
#include "noor/scene/prefab.h"

#include <filesystem>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

const stdfs::path kData = NOOR_TEST_DATA_DIR;
constexpr const char* kBuilding = "4529993a-ec0c-5a66-bc58-462a7823fbc6";

Json json(const char* text) {
    REQUIRE(text != nullptr);
    return Json::parse(text);
}

struct TempDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_doc_" + noor::Guid::generate().to_string());
    noor_doc* doc = noor_doc_create_project(dir.string().c_str(), "EditorTest");
    TempDoc() {
        REQUIRE(doc != nullptr);
        stdfs::create_directories(dir / "Assets" / "Buildings");
        for (const char* f : {"osm_w117821721.glb", "osm_w117821721.noor.json", "osm_w117821721.recipe.json"}) {
            stdfs::copy_file(kData / "badbuilding" / f, dir / "Assets" / "Buildings" / f);
        }
        REQUIRE(noor_doc_assets(doc, 1) != nullptr);
    }
    ~TempDoc() {
        noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    bool exec(const Json& cmd) const { return noor_doc_execute(doc, cmd.dump().c_str()) == 1; }
    Json scene() const { return json(noor_doc_scene(doc)); }
};

const Json* find_entity(const Json& scene, std::string_view name) {
    for (const auto& e : scene["entities"]) {
        if (e["name"].get<std::string>() == name) return &e;
    }
    return nullptr;
}

} // namespace

TEST_CASE("document api: version, project info and assets") {
    CHECK(noor_editor_api_version() == NOOR_EDITOR_API_VERSION);
    TempDoc t;
    const Json info = json(noor_doc_project_info(t.doc));
    CHECK(info["name"] == "EditorTest");
    const Json assets = json(noor_doc_assets(t.doc, 0));
    REQUIRE(assets.size() == 1);
    CHECK(assets[0]["guid"] == kBuilding);
    CHECK(assets[0]["importer"] == "gltf");
}

TEST_CASE("document api: add, move, undo, redo, delete, save and reopen") {
    TempDoc t;
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/Main.noorscene", "Main") == 1);
    Json s = t.scene();
    CHECK(s["open"] == true);
    CHECK(s["entities"].empty());
    CHECK(s["modified"] == false);

    const std::uint64_t r0 = noor_doc_revision(t.doc);
    REQUIRE(t.exec({{"op", "add_asset"}, {"asset", kBuilding}, {"position", {10, 0, -5}}, {"name", "Block A"}}));
    CHECK(noor_doc_revision(t.doc) > r0);
    s = t.scene();
    CHECK(s["entities"].size() == 29);  // root + 28 nodes
    CHECK(s["modified"] == true);
    CHECK(s["undo_label"] == "Add Block A");
    const Json* root = find_entity(s, "Block A");
    REQUIRE(root);
    CHECK((*root)["transform"]["position"][0] == 10.0);
    const std::string id = (*root)["id"];

    // Move it (partial transform: position only), then undo and redo.
    REQUIRE(t.exec({{"op", "set_transform"}, {"entity", id}, {"transform", {{"position", {1, 2, 3}}}}}));
    CHECK(t.scene()["undo_label"] == "Move");
    CHECK((*find_entity(t.scene(), "Block A"))["transform"]["position"][1] == 2.0);
    REQUIRE(noor_doc_undo(t.doc) == 1);
    CHECK((*find_entity(t.scene(), "Block A"))["transform"]["position"][0] == 10.0);
    REQUIRE(noor_doc_redo(t.doc) == 1);
    CHECK((*find_entity(t.scene(), "Block A"))["transform"]["position"][0] == 1.0);

    // Rename, delete (with the whole subtree), undo the delete.
    REQUIRE(t.exec({{"op", "rename"}, {"entity", id}, {"name", "Block B"}}));
    REQUIRE(t.exec({{"op", "remove"}, {"entity", id}}));
    CHECK(t.scene()["entities"].empty());
    REQUIRE(noor_doc_undo(t.doc) == 1);
    CHECK(t.scene()["entities"].size() == 29);

    REQUIRE(noor_doc_save_scene(t.doc) == 1);
    CHECK(t.scene()["modified"] == false);
    CHECK(stdfs::exists(t.dir / "Scenes" / "Main.noorscene"));

    // Reopen from disk in a fresh document.
    noor_doc* again = noor_doc_open_project(t.dir.string().c_str());
    REQUIRE(again);
    REQUIRE(noor_doc_open_scene(again, "Scenes/Main.noorscene") == 1);
    const Json reopened = json(noor_doc_scene(again));
    CHECK(reopened["entities"].size() == 29);
    CHECK(reopened["modified"] == false);
    REQUIRE(find_entity(reopened, "Block B"));
    CHECK(json(noor_doc_scenes(again)).size() == 1);
    noor_doc_close(again);
}

TEST_CASE("document api: entities from a prefab are shown read-only") {
    TempDoc t;
    // A one-entity prefab file, created with the engine library.
    noor::scene::Prefab lamp;
    lamp.name = "Lamp";
    noor::scene::Entity bulb;
    bulb.id = noor::Guid::generate();
    bulb.name = "Bulb";
    lamp.entities.push_back(bulb);
    REQUIRE(lamp.save(t.dir / "Assets" / "Props" / "Lamp.noorprefab"));
    REQUIRE(noor_doc_assets(t.doc, 1) != nullptr);
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/Street.noorscene", "Street") == 1);
    REQUIRE(t.exec({{"op", "add_asset"}, {"asset", lamp.id.to_string()}, {"name", "Lamp 1"}}));

    const Json s = t.scene();
    REQUIRE(s["entities"].size() == 2);  // the instance + the expanded bulb
    const Json* expanded = find_entity(s, "Bulb");
    REQUIRE(expanded);
    CHECK((*expanded)["from_prefab"] == true);
    CHECK((*expanded)["instance"] == (*find_entity(s, "Lamp 1"))["id"]);
    CHECK_FALSE(t.exec({{"op", "rename"}, {"entity", (*expanded)["id"]}, {"name", "nope"}}));
    CHECK(std::string(noor_last_error()).find("prefab") != std::string::npos);
}

TEST_CASE("document api: bad input fails cleanly with a reason") {
    TempDoc t;
    CHECK(noor_doc_execute(t.doc, "{\"op\": \"rename\"}") == 0);  // no scene open yet
    CHECK(std::string(noor_last_error()).find("No scene") != std::string::npos);
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/A.noorscene", "A") == 1);
    CHECK(noor_doc_execute(t.doc, "not json") == 0);
    CHECK(noor_doc_execute(t.doc, "{\"op\": \"teleport\", \"entity\": \"4529993a-ec0c-5a66-bc58-462a7823fbc6\"}") == 0);
    CHECK(noor_doc_execute(t.doc, "{\"op\": \"add_asset\", \"asset\": \"00000000-0000-4000-8000-000000000000\"}") == 0);
    CHECK(noor_doc_new_scene(t.doc, "../escape.noorscene", "X") == 0);
    CHECK(noor_doc_open_project("definitely/not/a/project") == nullptr);
    CHECK(noor_doc_undo(t.doc) == 0);  // nothing to undo
}
