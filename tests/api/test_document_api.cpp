// Drives noor_editor_api's document functions exactly as NoorEditor does (ADR-025), through the
// DLL's C ABI. No window and no GPU: runs in CI.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"

#include "noor/asset/json.h"
#include "noor/foundation/guid.h"
#include "noor/scene/prefab.h"
#include "noor/terrain/terrain.h"

#include <chrono>
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

TEST_CASE("document api: play runs behaviours on a copy, stop restores, edits wait") {
    TempDoc t;
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/Play.noorscene", "Play") == 1);
    REQUIRE(t.exec({{"op", "add_asset"}, {"asset", kBuilding}, {"name", "Spinner"}}));
    const std::string id = (*find_entity(t.scene(), "Spinner"))["id"];
    REQUIRE(t.exec({{"op", "set_component"}, {"entity", id}, {"type", "rotator"}, {"value", {{"degrees_per_second", 90}}}}));
    REQUIRE(noor_doc_save_scene(t.doc) == 1);

    REQUIRE(noor_doc_play(t.doc) == 1);
    CHECK(t.scene()["playing"] == true);
    for (int i = 0; i < 10; ++i) noor_doc_tick(t.doc, 0.1, nullptr);  // 1 s at 90 deg/s
    const Json running = t.scene();
    const auto& rot = (*find_entity(running, "Spinner"))["transform"]["rotation"];
    // A quarter turn about Y: q = (0, sin 45, 0, cos 45).
    CHECK(rot[1].get<double>() == doctest::Approx(0.7071).epsilon(1e-3));
    CHECK(running["play_seconds"].get<double>() == doctest::Approx(1.0));

    // Editing, undo and opening scenes wait until Stop.
    CHECK_FALSE(t.exec({{"op", "rename"}, {"entity", id}, {"name", "Nope"}}));
    CHECK(std::string(noor_last_error()).find("Stop play mode") != std::string::npos);
    CHECK(noor_doc_undo(t.doc) == 0);
    CHECK(noor_doc_open_scene(t.doc, "Scenes/Play.noorscene") == 0);

    noor_doc_stop(t.doc);
    const Json stopped = t.scene();
    CHECK(stopped["playing"] == false);
    CHECK((*find_entity(stopped, "Spinner"))["transform"]["rotation"][1].get<double>() == doctest::Approx(0.0));
    CHECK(stopped["modified"] == false);  // playing never touches the saved scene
    CHECK(t.exec({{"op", "rename"}, {"entity", id}, {"name", "Spinner 2"}}));
}

TEST_CASE("document api: a prefab edited on disk shows up after poll_files") {
    TempDoc t;
    noor::scene::Prefab lamp;
    lamp.name = "Lamp";
    noor::scene::Entity bulb;
    bulb.id = noor::Guid::generate();
    bulb.name = "Bulb";
    lamp.entities.push_back(bulb);
    const auto file = t.dir / "Assets" / "Props" / "Lamp.noorprefab";
    REQUIRE(lamp.save(file));
    REQUIRE(noor_doc_assets(t.doc, 1) != nullptr);
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/S.noorscene", "S") == 1);
    REQUIRE(t.exec({{"op", "add_asset"}, {"asset", lamp.id.to_string()}, {"name", "Lamp 1"}}));
    CHECK(find_entity(t.scene(), "Bulb"));
    CHECK(json(noor_doc_poll_files(t.doc)).empty());

    // A generator (or a person) rewrites the prefab: the bulb becomes "Big bulb".
    lamp.entities[0].name = "Big bulb";
    REQUIRE(lamp.save(file));
    stdfs::last_write_time(file, stdfs::last_write_time(file) + std::chrono::seconds(2));  // coarse file clocks
    Json changed = json(noor_doc_poll_files(t.doc));  // first sighting...
    if (changed.empty()) changed = json(noor_doc_poll_files(t.doc));  // ...settled
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].get<std::string>().ends_with("Lamp.noorprefab"));
    CHECK(find_entity(t.scene(), "Big bulb"));
    CHECK_FALSE(find_entity(t.scene(), "Bulb"));
}

TEST_CASE("document api: undo edge cases") {
    TempDoc t;
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/A.noorscene", "A") == 1);
    REQUIRE(t.exec({{"op", "add_asset"}, {"asset", kBuilding}, {"name", "B"}}));
    const std::string id = (*find_entity(t.scene(), "B"))["id"];
    REQUIRE(noor_doc_save_scene(t.doc) == 1);

    SUBCASE("undo after save marks the scene modified again; redo back to the saved state clears it") {
        REQUIRE(t.exec({{"op", "rename"}, {"entity", id}, {"name", "C"}}));
        REQUIRE(noor_doc_undo(t.doc) == 1);
        CHECK(t.scene()["modified"] == false);  // back at the saved state
        REQUIRE(noor_doc_undo(t.doc) == 1);    // undo the add: differs from the file
        CHECK(t.scene()["modified"] == true);
        REQUIRE(noor_doc_redo(t.doc) == 1);
        CHECK(t.scene()["modified"] == false);
    }
    SUBCASE("a new edit after undo drops the redo branch") {
        REQUIRE(t.exec({{"op", "rename"}, {"entity", id}, {"name", "C"}}));
        REQUIRE(noor_doc_undo(t.doc) == 1);
        CHECK(t.scene()["can_redo"] == true);
        REQUIRE(t.exec({{"op", "rename"}, {"entity", id}, {"name", "D"}}));
        CHECK(t.scene()["can_redo"] == false);
        CHECK(noor_doc_redo(t.doc) == 0);
    }
    SUBCASE("a transaction is one undo step") {
        REQUIRE(t.exec({{"op", "begin"}, {"label", "Tidy"}}));
        REQUIRE(t.exec({{"op", "rename"}, {"entity", id}, {"name", "E"}}));
        REQUIRE(t.exec({{"op", "set_transform"}, {"entity", id}, {"transform", {{"position", {5, 0, 0}}}}}));
        REQUIRE(t.exec({{"op", "end"}}));
        CHECK(t.scene()["undo_label"] == "Tidy");
        REQUIRE(noor_doc_undo(t.doc) == 1);
        const Json after = t.scene();
        const Json* b = find_entity(after, "B");
        REQUIRE(b);
        CHECK((*b)["transform"]["position"][0] == 0.0);
    }
    SUBCASE("opening another scene starts a fresh history") {
        REQUIRE(t.exec({{"op", "rename"}, {"entity", id}, {"name", "F"}}));
        REQUIRE(noor_doc_save_scene(t.doc) == 1);
        REQUIRE(noor_doc_new_scene(t.doc, "Scenes/Other.noorscene", "Other") == 1);
        CHECK(t.scene()["can_undo"] == false);
        CHECK(noor_doc_undo(t.doc) == 0);
        REQUIRE(noor_doc_open_scene(t.doc, "Scenes/A.noorscene") == 1);
        CHECK(t.scene()["can_undo"] == false);
        CHECK(find_entity(t.scene(), "F"));
    }
}

// Milestone 6, part 6: sculpting through the editor API, exactly as the viewport's strokes arrive.
TEST_CASE("document api: sculpting raises a hill, undoes as one step, saves a layer that outlives a new seed") {
    TempDoc t;
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/Sculpt.noorscene", "Sculpt") == 1);
    const Json flat{{"tiles", {2, 2}}, {"base", "flat"}, {"flat_height", 2}, {"collision", true}};
    REQUIRE(t.exec({{"op", "create"}, {"name", "Hill"}, {"transform", {{"position", {10, 0, 0}}}}, {"components", {{"terrain", flat}}}}));
    const std::string id = (*find_entity(t.scene(), "Hill"))["id"];
    auto sculpt = [&](const char* phase, double seconds, const char* tool = "raise") {
        const Json cmd{{"terrain", id}, {"phase", phase}, {"tool", tool}, {"radius", 5}, {"strength", 4}, {"falloff", 0.5},
                       {"dabs", Json::array({{{"point", {10, 2, 0}}, {"seconds", seconds}}})}};
        const char* r = noor_doc_sculpt(t.doc, cmd.dump().c_str());
        INFO(noor_last_error());
        REQUIRE(r != nullptr);
        return Json::parse(r);
    };
    // Where heights come from after a save: the component's settings with its files (like a game).
    auto saved_heights = [&]() {
        const Json comp = (*find_entity(t.scene(), "Hill"))["components"]["terrain"];
        const Json assets = json(noor_doc_assets(t.doc, 1));
        const auto settings = noor::terrain::Settings::from_json(comp, [&](const noor::Guid& g) -> std::optional<stdfs::path> {
            for (const auto& a : assets)
                if (a["guid"] == g.to_string()) return t.dir / a["path"].get<std::string>();
            return std::nullopt;
        });
        auto h = noor::terrain::Heights::build(settings);
        REQUIRE(h);
        return *h;
    };

    // One stroke: begin, two dabs, end (1 s at 4 m/s at the centre).
    const Json first = sculpt("begin", 0.25);
    sculpt("dab", 0.25);
    sculpt("dab", 0.25);
    sculpt("end", 0.25);
    Json s = t.scene();
    const Json comp = (*find_entity(s, "Hill"))["components"]["terrain"];
    REQUIRE(comp.contains("sculpt"));
    CHECK(comp["sculpt"] == first["sculpt"]);
    CHECK(stdfs::exists(t.dir / "Assets" / "Terrain" / "Hill.noorsculpt"));
    CHECK(s["undo_label"] == "Sculpt: Raise");
    CHECK(s["modified"] == true);
    REQUIRE(noor_doc_save_scene(t.doc) == 1);
    CHECK(saved_heights().height(0, 0) == doctest::Approx(6.0f));  // 2 m flat + 4 m

    // Undo takes the whole stroke back (and the layer it made); redo brings it back.
    REQUIRE(noor_doc_undo(t.doc) == 1);
    CHECK_FALSE((*find_entity(t.scene(), "Hill"))["components"]["terrain"].contains("sculpt"));
    REQUIRE(noor_doc_redo(t.doc) == 1);
    REQUIRE(noor_doc_save_scene(t.doc) == 1);
    CHECK(saved_heights().height(0, 0) == doctest::Approx(6.0f));

    // A new base (noise instead of flat): the hand-made 4 m sits on top of the new hills.
    Json noisy = (*find_entity(t.scene(), "Hill"))["components"]["terrain"];
    noisy["base"] = "noise";
    noisy["seed"] = 12;
    noisy["height"] = 20;
    REQUIRE(t.exec({{"op", "set_component"}, {"entity", id}, {"type", "terrain"}, {"value", noisy}}));
    REQUIRE(noor_doc_save_scene(t.doc) == 1);
    {
        Json base_only = noisy;
        base_only.erase("sculpt");
        auto base = noor::terrain::Heights::build(noor::terrain::Settings::from_json(base_only, {}));
        REQUIRE(base);
        CHECK(saved_heights().height(0, 0) - base->height(0, 0) == doctest::Approx(4.0f));
        CHECK(saved_heights().height(30, 30) == doctest::Approx(base->height(30, 30)));  // untouched away from the hill
    }

    // Play collides with sculpting that isn't saved yet: back to the flat base, 4 m more on the hill
    // (unsaved), and a ball dropped on top.
    Json flat_again = noisy;
    flat_again["base"] = "flat";
    REQUIRE(t.exec({{"op", "set_component"}, {"entity", id}, {"type", "terrain"}, {"value", flat_again}}));
    sculpt("begin", 0.5);
    sculpt("end", 0.5);
    REQUIRE(t.exec({{"op", "create"}, {"name", "Ball"}, {"transform", {{"position", {10, 20, 0}}}},
                    {"components", {{"rigidbody", {{"type", "dynamic"}, {"mass", 1}}}, {"collider", {{"shape", "sphere"}, {"radius", 0.5}}}}}}));
    REQUIRE(noor_doc_play(t.doc) == 1);
    for (int i = 0; i < 40; ++i) noor_doc_tick(t.doc, 0.1, nullptr);
    const double ball_y = (*find_entity(t.scene(), "Ball"))["transform"]["position"][1].get<double>();
    noor_doc_stop(t.doc);
    // 2 m + 6 m of hill (dabs count at most 0.25 s each, so this stroke added 2 m) + the ball's radius.
    // The saved layer alone would give 6.5 m: Play used the unsaved sculpting.
    CHECK(ball_y == doctest::Approx(8.5).epsilon(0.02));

    // Bad input.
    CHECK(noor_doc_sculpt(t.doc, R"({"terrain": "not-an-id", "phase": "begin"})") == nullptr);
    CHECK(noor_doc_sculpt(t.doc, (Json{{"terrain", id}, {"phase", "begin"}, {"tool", "melt"}}).dump().c_str()) == nullptr);
}
