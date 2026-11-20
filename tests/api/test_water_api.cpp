// The Water panel through the editor API (Milestone 8D, part 7): presets, apply, edit, duplicate, reset and the debug
// view, checked on the files and on what a game reads from them (game::water_look over the project's asset paths).
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/asset/water_asset.h"
#include "noor/foundation/guid.h"
#include "noor/game/water.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

Json parsed(const char* text) {
    REQUIRE(text != nullptr);
    return Json::parse(text);
}

struct WaterDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_water_" + noor::Guid::generate().to_string());
    noor_doc* doc = noor_doc_create_project(dir.string().c_str(), "WaterTest");
    WaterDoc() {
        REQUIRE(doc != nullptr);
        REQUIRE(noor_doc_new_scene(doc, "Scenes/Main.noorscene", "Main") == 1);
    }
    ~WaterDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json water(const Json& command) const { return parsed(noor_doc_water(doc, command.dump().c_str())); }
    // The water component of an entity in the saved scene.
    Json water_of(const std::string& id) const {
        REQUIRE(noor_doc_save_scene(doc) == 1);
        auto scene = noor::scene::Scene::load(dir / "Scenes" / "Main.noorscene");
        REQUIRE(scene.has_value());
        for (const auto& e : scene->entities)
            if (e.id.to_string() == id && e.has("water")) return e.components.at("water");
        return Json();
    }
    // The look a game would draw that component with (its asset found by guid in the project's water folder).
    noor::asset::WaterLook look_of(const Json& component) const {
        const auto asset_path = [this](const noor::Guid& id) -> std::optional<stdfs::path> {
            // The meta files are the registry's: read them for the guid.
            for (const auto& f : stdfs::directory_iterator(dir / "Assets" / "Water")) {
                if (f.path().extension() != ".noormeta") continue;
                std::ifstream in(f.path());
                const Json meta = Json::parse(std::string(std::istreambuf_iterator<char>(in), {}));
                if (meta.value("guid", std::string()) == id.to_string()) return f.path().parent_path() / f.path().stem();
            }
            return std::nullopt;
        };
        return noor::game::water_look(component, asset_path);
    }
};

} // namespace

TEST_CASE("water api: seven built-in looks, in gallery order, each one a look") {
    const Json presets = parsed(noor_water_presets());
    REQUIRE(presets.size() == 7);
    const char* names[] = {"Lake", "Pond", "Sea", "Tropical", "Swamp", "River", "Deep sea"};
    for (std::size_t i = 0; i < 7; ++i) {
        CHECK(presets[i]["name"] == names[i]);
        CHECK(!presets[i]["description"].get<std::string>().empty());
        noor::asset::WaterLook look;
        look.apply(presets[i]["water"]);
        CHECK(look.absorption.x > 0.0f);  // every one says how its water absorbs
    }
}

TEST_CASE("water api: applying a look copies it into the project and onto a surface, its shape kept, in one undo step") {
    WaterDoc d;
    // A new surface with it.
    const Json pond = d.water(Json{{"op", "apply"}, {"preset", "Pond"}, {"position", {1, 0, 2}}});
    CHECK(pond["path"] == "Assets/Water/Pond.noorwater");
    CHECK(pond["preset"] == "Pond");
    REQUIRE(pond.contains("entity"));
    CHECK(stdfs::exists(d.dir / "Assets" / "Water" / "Pond.noorwater"));
    CHECK(d.water_of(pond["entity"])["asset"] == pond["guid"]);
    CHECK(d.water_of(pond["entity"])["size"] == Json::array({20, 20}));
    CHECK(noor_doc_undo(d.doc) == 1);  // gone again in one step
    CHECK(d.water_of(pond["entity"]).is_null());
    CHECK(noor_doc_redo(d.doc) == 1);
    // Onto an existing river: its course stays, its own look fields and preset go.
    const std::string id = pond["entity"];
    REQUIRE(noor_doc_execute(d.doc, Json{{"op", "set_component"}, {"entity", id}, {"type", "water"},
                                        {"value", {{"shape", "river"}, {"points", {{0, 0, 0}, {10, 0, 0}}}, {"width", 5}, {"speed", 2},
                                                   {"preset", "Lake"}, {"tint", "#FF0000"}}}}.dump().c_str()) == 1);
    const Json river = d.water(Json{{"op", "apply"}, {"preset", "River"}, {"entity", id}});
    const Json c = d.water_of(id);
    CHECK(c["asset"] == river["guid"]);
    CHECK(c["shape"] == "river");
    CHECK(c["points"].size() == 2);
    CHECK(c["speed"] == 2);
    CHECK(!c.contains("tint"));
    CHECK(!c.contains("preset"));
    CHECK(d.water(Json{{"op", "list"}}).size() == 2);
}

TEST_CASE("water api: editing a look changes what a game draws, and never the preset, with duplicate and reset") {
    WaterDoc d;
    const Json lake = d.water(Json{{"op", "apply"}, {"preset", "Lake"}});
    const Json component = d.water_of(lake["entity"]);
    const float clear = d.look_of(component).absorption.y;
    Json water = lake["water"];
    water["tint"] = "#406020";
    water["clarity"] = 0.5;  // murky
    const Json saved = d.water(Json{{"op", "save"}, {"asset", lake["path"]}, {"water", water}});
    CHECK(saved["water"]["clarity"] == 0.5);
    CHECK(saved["preset"] == "Lake");
    CHECK(d.look_of(component).absorption.y > clear * 5.0f);  // the game now reads much murkier water
    // The built-in preset is untouched; Reset puts the copy back; a duplicate is its own file.
    CHECK((*noor::asset::water_preset("Lake"))["water"]["clarity"] == 4.0);
    const Json reset = d.water(Json{{"op", "reset"}, {"asset", lake["path"]}});
    CHECK(reset["water"]["clarity"] == 4.0);
    CHECK(d.look_of(component).absorption.y == doctest::Approx(clear));
    const Json copy = d.water(Json{{"op", "duplicate"}, {"asset", lake["path"]}});
    CHECK(copy["path"] == "Assets/Water/Lake copy.noorwater");
    CHECK(copy["water"] == reset["water"]);
    CHECK(copy["guid"] != reset["guid"]);
}

TEST_CASE("water api: the debug view, and bad commands") {
    WaterDoc d;
    CHECK(d.water(Json{{"op", "debug"}})["view"] == "none");
    for (const char* view : {"depth", "flow", "waves", "normals", "cost", "none"}) CHECK(d.water(Json{{"op", "debug"}, {"view", view}})["view"] == view);
    CHECK(noor_doc_water(d.doc, R"({"op": "debug", "view": "sideways"})") == nullptr);
    CHECK(noor_doc_water(d.doc, R"({"op": "apply", "preset": "Lava"})") == nullptr);
    CHECK(noor_doc_water(d.doc, R"({"op": "fly"})") == nullptr);
    CHECK(noor_doc_water(d.doc, R"({"op": "read", "asset": "Assets/Water/Nope.noorwater"})") == nullptr);
}
