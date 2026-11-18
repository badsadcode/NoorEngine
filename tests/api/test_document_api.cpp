// Drives noor_editor_api's document functions exactly as NoorEditor does (ADR-025), through the
// DLL's C ABI. No window and no GPU: runs in CI.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"

#include "noor/asset/gltf_import.h"
#include "noor/asset/json.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/guid.h"
#include "noor/scene/prefab.h"
#include "noor/procgraph/procgraph.h"
#include "noor/terrain/terrain.h"

#include <chrono>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <thread>
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
    float ground = 0;
    REQUIRE(noor_doc_terrain_height(t.doc, id.c_str(), 10, 0, &ground) == 1);  // world (10, 0) is the hill's top
    CHECK(ground == doctest::Approx(6.0f));
    REQUIRE(noor_doc_terrain_height(t.doc, id.c_str(), 40, 40, &ground) == 1);
    CHECK(ground == doctest::Approx(2.0f));
    CHECK(noor_doc_terrain_height(t.doc, first["sculpt"].get<std::string>().c_str(), 0, 0, &ground) == 0);  // not a terrain entity

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

    // A fast stroke (the pointer jumped 40 m between two frames) leaves a line, not two dots: the
    // middle, where the pointer never was, is raised too.
    {
        const Json begin{{"terrain", id}, {"phase", "begin"}, {"tool", "raise"}, {"radius", 4}, {"strength", 4},
                         {"dabs", Json::array({{{"point", {-10, 2, 30}}, {"seconds", 0.1}}})}};
        REQUIRE(noor_doc_sculpt(t.doc, begin.dump().c_str()) != nullptr);
        const Json end_cmd{{"terrain", id}, {"phase", "end"}, {"dabs", Json::array({{{"point", {30, 2, 30}}, {"seconds", 0.1}}})}};
        REQUIRE(noor_doc_sculpt(t.doc, end_cmd.dump().c_str()) != nullptr);
        REQUIRE(noor_doc_save_scene(t.doc) == 1);
        CHECK(saved_heights().height(0, 30) > 2.01f);   // the terrain is at x = 10: world x = 10 is local 0
        CHECK(saved_heights().height(0, 40) == doctest::Approx(2.0f));  // beside the line: untouched
    }

    // Bad input.
    CHECK(noor_doc_sculpt(t.doc, R"({"terrain": "not-an-id", "phase": "begin"})") == nullptr);
    CHECK(noor_doc_sculpt(t.doc, (Json{{"terrain", id}, {"phase", "begin"}, {"tool", "melt"}}).dump().c_str()) == nullptr);
}

// Milestone 6, part 7: painting materials onto a terrain through the editor API.
TEST_CASE("document api: painting adds material layers to a terrain, one undo step per stroke, saved as a paint layer") {
    TempDoc t;
    stdfs::create_directories(t.dir / "Assets" / "Materials");
    for (const char* name : {"grass", "rock", "dirt", "sand", "snow"})
        REQUIRE(noor::fs::write_file_atomic(t.dir / "Assets" / "Materials" / (std::string(name) + ".noormat"), R"({"noor_material": 1, "base_color": [0.5, 0.5, 0.5, 1]})", false));
    const Json assets = json(noor_doc_assets(t.doc, 1));
    auto material = [&](const std::string& name) {
        for (const auto& a : assets)
            if (a["path"] == "Assets/Materials/" + name + ".noormat") return a["guid"].get<std::string>();
        FAIL("no material " << name);
        return std::string();
    };
    REQUIRE(noor_doc_new_scene(t.doc, "Scenes/Paint.noorscene", "Paint") == 1);
    REQUIRE(t.exec({{"op", "create"}, {"name", "Field"}, {"components", {{"terrain", {{"tiles", {1, 1}}, {"base", "flat"}, {"material", material("grass")}}}}}}));
    const std::string id = (*find_entity(t.scene(), "Field"))["id"];
    auto paint = [&](const std::string& mat, const char* phase = "begin") {
        const Json cmd{{"terrain", id}, {"phase", phase}, {"tool", "paint"}, {"material", mat}, {"radius", 6}, {"strength", 4}, {"falloff", 0.3},
                       {"dabs", Json::array({{{"point", {0, 0, 0}}, {"seconds", 0.25}}})}};
        const char* r = noor_doc_sculpt(t.doc, cmd.dump().c_str());
        return r ? Json::parse(r) : Json();
    };
    auto end = [&] { REQUIRE(noor_doc_sculpt(t.doc, (Json{{"terrain", id}, {"phase", "end"}}).dump().c_str()) != nullptr); };

    // Rock onto a grass terrain: grass stays layer 0, rock becomes layer 1.
    const Json r = paint(material("rock"));
    REQUIRE(r.is_object());
    CHECK(r["layer"] == 1);
    end();
    Json comp = (*find_entity(t.scene(), "Field"))["components"]["terrain"];
    REQUIRE(comp["layers"].size() == 2);
    CHECK(comp["layers"][0]["material"] == material("grass"));
    CHECK(comp["layers"][1]["material"] == material("rock"));
    CHECK(comp["paint"] == r["paint"]);
    CHECK(t.scene()["undo_label"] == "Paint: rock");

    // Saved: the paint file has the rock under the brush.
    REQUIRE(noor_doc_save_scene(t.doc) == 1);
    auto layer = noor::terrain::PaintLayer::load(t.dir / "Assets" / "Terrain" / "Field.noorpaint");
    REQUIRE(layer);
    CHECK(noor::terrain::PaintLayer::weight(layer->get(128, 128), 1) == doctest::Approx(1.0f));

    // Grass painted back over it uses layer 0; two more materials fill the layers; a fifth is refused.
    CHECK(paint(material("grass"))["layer"] == 0);
    end();
    CHECK(paint(material("dirt"))["layer"] == 2);
    end();
    CHECK(paint(material("sand"))["layer"] == 3);
    end();
    CHECK(paint(material("snow")).is_null());
    CHECK(std::string(noor_last_error()).find("four materials") != std::string::npos);

    // Undo the three strokes after the rock (sand, dirt, grass): rock in the middle again, two layers.
    for (int i = 0; i < 3; ++i) REQUIRE(noor_doc_undo(t.doc) == 1);
    CHECK((*find_entity(t.scene(), "Field"))["components"]["terrain"]["layers"].size() == 2);
    REQUIRE(noor_doc_save_scene(t.doc) == 1);
    layer = noor::terrain::PaintLayer::load(t.dir / "Assets" / "Terrain" / "Field.noorpaint");
    REQUIRE(layer);
    CHECK(noor::terrain::PaintLayer::weight(layer->get(128, 128), 1) == doctest::Approx(1.0f));
    // And the rock stroke itself: no layers, no paint layer named, nothing painted when saved.
    REQUIRE(noor_doc_undo(t.doc) == 1);
    comp = (*find_entity(t.scene(), "Field"))["components"]["terrain"];
    CHECK_FALSE(comp.contains("layers"));
    CHECK_FALSE(comp.contains("paint"));
    REQUIRE(noor_doc_save_scene(t.doc) == 1);
    layer = noor::terrain::PaintLayer::load(t.dir / "Assets" / "Terrain" / "Field.noorpaint");
    REQUIRE(layer);
    CHECK(noor::terrain::PaintLayer::weight(layer->get(128, 128), 0) == doctest::Approx(1.0f));
}

// Milestone 6, part 8: the Hillside sample's house exports as a Noor Interchange GLB that reads back.
TEST_CASE("document api: the Hillside sample opens, and its house exports as a GLB that reads back") {
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_hillside_" + noor::Guid::generate().to_string());
    // A copy (the test must not touch the sample), without its derived data.
    stdfs::create_directories(dir);
    for (auto it = stdfs::recursive_directory_iterator(stdfs::path(NOOR_SAMPLES_DIR) / "Hillside"); it != stdfs::recursive_directory_iterator(); ++it) {
        const auto rel = it->path().lexically_relative(stdfs::path(NOOR_SAMPLES_DIR) / "Hillside");
        if (*rel.begin() == ".noor") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory()) stdfs::create_directories(dir / rel);
        else stdfs::copy_file(it->path(), dir / rel);
    }
    noor_doc* doc = noor_doc_open_project(dir.string().c_str());
    REQUIRE(doc != nullptr);
    REQUIRE(noor_doc_open_scene(doc, "Scenes/Hillside.noorscene") == 1);
    const Json scene = json(noor_doc_scene(doc));
    const Json* house = find_entity(scene, "House");
    const Json* terrain = find_entity(scene, "Hillside");
    REQUIRE(house != nullptr);
    REQUIRE(terrain != nullptr);
    CHECK((*terrain)["components"]["terrain"]["layers"].size() == 3);  // meadow, rock, dirt
    // The house stands on the terrain (its floor is the flattened pad).
    float ground = 0;
    REQUIRE(noor_doc_terrain_height(doc, (*terrain)["id"].get<std::string>().c_str(), 20, 0, &ground) == 1);
    CHECK((*house)["transform"]["position"][1].get<float>() == doctest::Approx(ground).epsilon(0.01));

    const Json csg = json(noor_doc_csg(doc));
    REQUIRE(csg.size() == 1);
    const auto glb = dir / "Exports" / "House.glb";
    REQUIRE(noor_doc_export_csg(doc, (*house)["id"].get<std::string>().c_str(), glb.string().c_str()) != nullptr);
    auto model = noor::asset::import_gltf(glb);
    REQUIRE_MESSAGE(model, (model ? "" : model.error().describe()));
    const auto collision = std::ranges::find_if(model->nodes, [](const auto& n) { return n.is_collision(); });
    REQUIRE(collision != model->nodes.end());
    CHECK(collision->name == "House-colonly");
    CHECK(model->images.size() >= 2);  // brick and planks maps, embedded
    std::size_t triangles = 0;
    for (const auto& sm : model->meshes[0].submeshes) triangles += sm.index_count / 3;
    CHECK(triangles == csg[0]["triangles"].get<std::size_t>());
    CHECK(model->meshes[0].submeshes.size() >= 2);  // runs of brick and planks (one or more per brush)
    noor_doc_close(doc);
    std::error_code ec;
    stdfs::remove_all(dir, ec);
}

// Milestone 7, part 2: a graph runs in the background with progress and Cancel, and the editor writes
// the same bytes as noor_cli generate (api.generate_matches_cli gives this case the CLI's folder).
TEST_CASE("document api: a graph runs in the background, can be cancelled, and matches the CLI") {
    const stdfs::path src = kData / "procgraph_project";
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_graphdoc_" + noor::Guid::generate().to_string());
    stdfs::create_directories(dir);
    stdfs::copy(src, dir, stdfs::copy_options::recursive);
    noor_doc* doc = noor_doc_open_project(dir.string().c_str());
    REQUIRE(doc != nullptr);
    auto wait = [&]() {
        Json s;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        do {
            s = json(noor_doc_generate_status(doc));
            if (!s["running"].get<bool>()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < until);
        return s;
    };

    // Cancel at once: nothing is written.
    REQUIRE(noor_doc_generate_start(doc, "Assets/Graphs/hills.noorgraph", nullptr) == 1);
    noor_doc_generate_cancel(doc);
    Json s = wait();
    CHECK(s["running"] == false);
    CHECK(s["cancelled"] == true);
    CHECK_FALSE(stdfs::exists(dir / "Generated" / "hills" / "hills.fingerprint.json"));

    // A full run: progress counts the nodes, the fingerprint is the pinned one.
    REQUIRE(noor_doc_generate_start(doc, "Assets/Graphs/hills.noorgraph", nullptr) == 1);
    s = wait();
    INFO(s.dump());
    REQUIRE(s["ok"] == true);
    CHECK(s["fingerprint"] == "193a309d3ece274d");
    CHECK(s["done"] == 6);
    CHECK(s["total"] == 6);
    CHECK(s["files"].size() == 2);
    // Again: everything from the cache.
    REQUIRE(noor_doc_generate_start(doc, "Assets/Graphs/hills.noorgraph", nullptr) == 1);
    s = wait();
    CHECK(s["cached"] == 6);
    CHECK(s["ran"] == 0);
    CHECK(noor_doc_generate_start(doc, "Assets/Graphs/missing.noorgraph", nullptr) == 0);

    // The CLI's files, when the ctest wrapper ran it first: byte for byte the same.
    if (const char* cli = std::getenv("NOOR_CLI_GENERATED")) {
        for (const char* f : {"terrain.noorfield", "hills.fingerprint.json"}) {
            INFO(f);
            const auto mine = noor::fs::read_file(dir / "Generated" / "hills" / f, 1u << 28);
            const auto theirs = noor::fs::read_file(stdfs::path(cli) / f, 1u << 28);
            REQUIRE(mine);
            REQUIRE(theirs);
            CHECK(*mine == *theirs);
        }
    } else {
        MESSAGE("NOOR_CLI_GENERATED isn't set: the CLI comparison runs as api.generate_matches_cli");
    }
    noor_doc_close(doc);
    std::error_code ec;
    stdfs::remove_all(dir, ec);
}

// Milestone 7, part 3: a terrain whose base is a procedural graph. The editor cooks the graph when the
// scene shows it; sculpting sits on top; a new seed in the graph changes the hills and keeps the sculpting.
TEST_CASE("document api: a terrain on a graph base cooks itself, and its sculpting survives a new seed") {
    const stdfs::path src = kData / "procgraph_project";
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_graphterrain_" + noor::Guid::generate().to_string());
    stdfs::create_directories(dir);
    stdfs::copy(src, dir, stdfs::copy_options::recursive);
    noor_doc* doc = noor_doc_open_project(dir.string().c_str());
    REQUIRE(doc != nullptr);
    REQUIRE(noor_doc_open_scene(doc, "Scenes/Main.noorscene") == 1);
    const Json scene = json(noor_doc_scene(doc));
    const std::string id = (*find_entity(scene, "Hills"))["id"];
    const stdfs::path cooked = dir / ".noor" / "cooked" / "graphs" / "7e770000-9a17-4000-8000-00000000600d" / "terrain.noorfield";
    CHECK(stdfs::exists(cooked));  // cooked when the scene was opened (published)
    auto field = noor::procgraph::load_field(cooked);
    REQUIRE(field);
    float ground = 0;
    REQUIRE(noor_doc_terrain_height(doc, id.c_str(), 0, 0, &ground) == 1);
    CHECK(ground == doctest::Approx(field->first.at(128, 128)));  // the graph's height at the centre
    CHECK(ground == doctest::Approx(9.3314f));                    // the pinned graph's value there

    // A 4 m bump by hand.
    const Json begin{{"terrain", id}, {"phase", "begin"}, {"tool", "raise"}, {"radius", 6}, {"strength", 4}, {"falloff", 0.5},
                     {"dabs", Json::array({{{"point", {0, 0, 0}}, {"seconds", 0.25}}})}};
    REQUIRE(noor_doc_sculpt(doc, begin.dump().c_str()) != nullptr);
    for (int i = 0; i < 3; ++i)
        REQUIRE(noor_doc_sculpt(doc, (Json{{"terrain", id}, {"phase", i == 2 ? "end" : "dab"}, {"dabs", Json::array({{{"point", {0, 0, 0}}, {"seconds", 0.25}}})}}).dump().c_str()) != nullptr);
    REQUIRE(noor_doc_terrain_height(doc, id.c_str(), 0, 0, &ground) == 1);
    CHECK(ground == doctest::Approx(field->first.at(128, 128) + 4.0f));
    REQUIRE(noor_doc_save_scene(doc) == 1);

    // A new seed in the graph file: different hills, the same 4 m on top of them.
    auto graph = noor::procgraph::Graph::load(dir / "Assets" / "Graphs" / "hills.noorgraph");
    REQUIRE(graph);
    graph->seed = 8;
    REQUIRE(graph->save(dir / "Assets" / "Graphs" / "hills.noorgraph"));
    REQUIRE(noor_doc_assets(doc, 1) != nullptr);  // the editor notices the changed asset (rescan republishes)
    REQUIRE(noor_doc_save_scene(doc) == 1);       // and Save always cooks
    auto reseeded = noor::procgraph::load_field(cooked);
    REQUIRE(reseeded);
    CHECK(reseeded->first.at(128, 128) != field->first.at(128, 128));
    REQUIRE(noor_doc_terrain_height(doc, id.c_str(), 0, 0, &ground) == 1);
    CHECK(ground == doctest::Approx(reseeded->first.at(128, 128) + 4.0f));
    noor_doc_close(doc);
    std::error_code ec;
    stdfs::remove_all(dir, ec);
}

// Milestone 7, part 4: what the graph editor asks the engine: node types, a check without running,
// per-node results of a run, and previews.
TEST_CASE("document api: the graph editor's calls: node types, check, node results and previews") {
    const Json types = json(noor_graph_node_types());
    auto type = [&](const std::string& name) -> const Json* {
        for (const auto& t : types)
            if (t["name"] == name) return &t;
        return nullptr;
    };
    REQUIRE(type("stamp") != nullptr);
    CHECK((*type("stamp"))["tiled"] == true);
    CHECK((*type("slope"))["tiled"] == false);
    CHECK((*type("noise"))["outputs"][0]["unit"] == "metres");
    CHECK((*type("remap"))["params"].size() == 5);

    // A check: issues on nodes, and the graph in the engine's own format.
    const Json broken{{"noor_graph", 1}, {"nodes", Json::array({{{"id", "c"}, {"type", "clamp"}}})}};
    const Json checked = json(noor_graph_check(broken.dump().c_str()));
    REQUIRE(checked["issues"].size() == 1);
    CHECK(checked["issues"][0]["node"] == "c");
    CHECK(checked["text"].get<std::string>().find("\"noor_graph\": 1") != std::string::npos);
    CHECK(noor_graph_check("not json") == nullptr);

    // A run's per-node results, and a preview of a field.
    const stdfs::path dir = stdfs::temp_directory_path() / ("noor_grapheditor_" + noor::Guid::generate().to_string());
    stdfs::create_directories(dir);
    stdfs::copy(kData / "procgraph_project", dir, stdfs::copy_options::recursive);
    noor_doc* doc = noor_doc_open_project(dir.string().c_str());
    REQUIRE(doc != nullptr);
    CHECK(noor_doc_generate_preview(doc, "hills", "height", 64) == nullptr);  // nothing has run yet
    REQUIRE(noor_doc_generate_start(doc, "Assets/Graphs/hills.noorgraph", nullptr) == 1);
    Json s;
    for (int i = 0; i < 12000; ++i) {
        s = json(noor_doc_generate_status(doc));
        if (!s["running"].get<bool>()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(s["ok"] == true);
    CHECK(s["written"] == true);
    CHECK(s["nodes"]["hills"]["state"] == "done");
    CHECK(s["nodes"]["mask"]["outputs"]["out"]["unit"] == "ratio (0..1)");
    CHECK(s["nodes"]["limit"]["outputs"]["out"]["max"].get<double>() <= 25.0);
    const Json preview = json(noor_doc_generate_preview(doc, "hills", "height", 64));
    CHECK(preview["width"] == 64);
    CHECK(preview["height"] == 64);
    CHECK(preview["pixels"].get<std::string>().size() == (64 * 64 + 2) / 3 * 4);  // base64 of 4096 bytes
    CHECK(preview["unit"] == "metres");
    CHECK(noor_doc_generate_preview(doc, "hills", "nope", 64) == nullptr);
    noor_doc_close(doc);
    std::error_code ec;
    stdfs::remove_all(dir, ec);
}
