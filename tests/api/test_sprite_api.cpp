// Sprite sheets through the editor API (Milestone 8B, part 5): create a sheet from a texture, slice it by saving a grid,
// read the frames back, apply it to entities, and refuse what doesn't parse. The sheet is the test sprite sheet
// (tests/data/render_scenes/Assets/Sprites/walker.png: 128 x 64, 4 x 2 frames of 32 x 32).
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/asset/sprite_asset.h"
#include "noor/foundation/guid.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

const stdfs::path kSheetPng = stdfs::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Sprites" / "walker.png";

Json parsed(const char* text) {
    REQUIRE(text != nullptr);
    return Json::parse(text);
}

struct SpriteDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_sprite_" + noor::Guid::generate().to_string());
    noor_doc* doc = nullptr;
    SpriteDoc() {
        doc = noor_doc_create_project(dir.string().c_str(), "SpriteTest");
        REQUIRE(doc != nullptr);
        REQUIRE(noor_doc_new_scene(doc, "Scenes/Main.noorscene", "Main") == 1);
        stdfs::create_directories(dir / "Assets" / "Sprites");
        stdfs::copy_file(kSheetPng, dir / "Assets" / "Sprites" / "walker.png");
    }
    ~SpriteDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json sprite(const Json& command) const { return parsed(noor_doc_sprite(doc, command.dump().c_str())); }
    Json component_of(const std::string& id) const {
        REQUIRE(noor_doc_save_scene(doc) == 1);
        auto scene = noor::scene::Scene::load(dir / "Scenes" / "Main.noorscene");
        REQUIRE(scene.has_value());
        for (const auto& e : scene->entities)
            if (e.id.to_string() == id && e.has("sprite")) return e.components.at("sprite");
        return Json();
    }
};

} // namespace

TEST_CASE("sprite api: a sheet made from a texture, sliced by a grid, frames exactly on it") {
    SpriteDoc d;
    const Json made = d.sprite(Json{{"op", "create"}, {"texture", "Assets/Sprites/walker.png"}});
    CHECK(made["path"] == "Assets/Sprites/walker.noorsprite");
    CHECK(!made["guid"].get<std::string>().empty());
    CHECK(made["image"]["width"] == 128);
    CHECK(made["image"]["height"] == 64);
    REQUIRE(made["frames"].size() == 1);  // the whole image until a grid is set
    CHECK(made["frames"][0] == Json::array({0, 0, 128, 64}));

    Json sheet = made["sheet"];
    sheet["grid"] = {{"columns", 4}, {"rows", 2}};
    sheet["pixels_per_metre"] = 16;
    sheet["animations"] = {{"walk", {{"frames", {{"from", 0}, {"to", 3}}}, {"fps", 6}}}};
    const Json saved = d.sprite(Json{{"op", "save"}, {"asset", made["path"]}, {"sheet", sheet}});
    REQUIRE(saved["frames"].size() == 8);
    for (int i = 0; i < 8; ++i) CHECK(saved["frames"][static_cast<std::size_t>(i)] == Json::array({(i % 4) * 32, (i / 4) * 32, 32, 32}));
    CHECK(saved["frame_size_m"][0].get<float>() == doctest::Approx(2.0f));
    // What a game loads from the file is the same sheet.
    auto file = noor::asset::load_sprite_file(d.dir / "Assets" / "Sprites" / "walker.noorsprite");
    REQUIRE(file.has_value());
    CHECK(file->animations.at("walk").frames.size() == 4);
    CHECK(d.sprite(Json{{"op", "list"}}).size() == 1);
    CHECK(d.sprite(Json{{"op", "read"}, {"asset", made["path"]}})["rows"] == 2);
}

TEST_CASE("sprite api: a grid that doesn't fit is saved with its error, and a sheet that isn't one is refused") {
    SpriteDoc d;
    const Json made = d.sprite(Json{{"op", "create"}, {"texture", "Assets/Sprites/walker.png"}, {"name", "Walker"}});
    Json sheet = made["sheet"];
    sheet["grid"] = {{"frame_width", 300}, {"frame_height", 32}};
    const Json saved = d.sprite(Json{{"op", "save"}, {"asset", made["path"]}, {"sheet", sheet}});
    CHECK(saved.contains("error"));
    CHECK(!saved.contains("frames"));
    CHECK(noor_doc_sprite(d.doc, Json{{"op", "save"}, {"asset", made["path"]}, {"sheet", "not a sheet"}}.dump().c_str()) == nullptr);
    CHECK(noor_doc_sprite(d.doc, R"({"op": "spin"})") == nullptr);
    // A second sheet for the same texture gets its own name.
    CHECK(d.sprite(Json{{"op", "create"}, {"texture", "Assets/Sprites/walker.png"}, {"name", "Walker"}})["path"] == "Assets/Sprites/Walker 2.noorsprite");
    CHECK(noor_doc_sprite(d.doc, R"({"op": "create", "texture": "Scenes/Main.noorscene"})") == nullptr);  // not a texture
}

TEST_CASE("sprite api: applying a sheet makes a sprite entity in one undo step, or keeps an entity's other fields") {
    SpriteDoc d;
    const Json made = d.sprite(Json{{"op", "create"}, {"texture", "Assets/Sprites/walker.png"}});
    const Json applied = d.sprite(Json{{"op", "apply"}, {"asset", made["path"]}, {"animation", "walk"}, {"position", {1, 0, 2}}});
    REQUIRE(applied.contains("entity"));
    const std::string id = applied["entity"];
    CHECK(d.component_of(id)["sheet"] == made["guid"]);
    CHECK(d.component_of(id)["animation"] == "walk");
    CHECK(noor_doc_undo(d.doc) == 1);
    CHECK(d.component_of(id).is_null());
    CHECK(noor_doc_redo(d.doc) == 1);
    // Onto the same entity again (another sheet): its billboard and colour stay.
    const Json set = Json{{"op", "set_component"}, {"entity", id}, {"type", "sprite"},
                          {"value", {{"sheet", made["guid"]}, {"billboard", "camera"}, {"color", "#FF0000"}}}};
    REQUIRE(noor_doc_execute(d.doc, set.dump().c_str()) == 1);
    const Json other = d.sprite(Json{{"op", "create"}, {"texture", "Assets/Sprites/walker.png"}, {"name", "Other"}});
    d.sprite(Json{{"op", "apply"}, {"asset", other["path"]}, {"entity", id}});
    const Json c = d.component_of(id);
    CHECK(c["sheet"] == other["guid"]);
    CHECK(c["billboard"] == "camera");
    CHECK(c["color"] == "#FF0000");
}
