// The Tilemap panel through the editor API (2D foundations, part 5): listing tilesets, a tilemap's palette, and
// painting strokes: one undo step each, growing the map past its edges, and moving the entity when it grows left or
// down so the tiles already there stay where they were.
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/asset/tileset_asset.h"
#include "noor/foundation/guid.h"

#include <filesystem>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

Json parsed(const char* text) {
    REQUIRE(text != nullptr);
    return Json::parse(text);
}

struct TileDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_tiles_" + noor::Guid::generate().to_string());
    noor_doc* doc = noor_doc_create_project(dir.string().c_str(), "TileTest");
    std::string tileset_id;
    TileDoc() {
        REQUIRE(doc != nullptr);
        // The render scenes' square tiles (picture, sheet, tileset and their metas: the ids come with them).
        const stdfs::path from = stdfs::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Tiles";
        stdfs::create_directories(dir / "Assets" / "Tiles");
        for (const char* f : {"square.png", "square.png.noormeta", "square.noorsprite", "square.noorsprite.noormeta", "square.noortileset",
                              "square.noortileset.noormeta"})
            stdfs::copy_file(from / f, dir / "Assets" / "Tiles" / f);
        REQUIRE(noor_doc_assets(doc, 1) != nullptr);
        REQUIRE(noor_doc_new_scene(doc, "Scenes/Main.noorscene", "Main") == 1);
        const Json list = tilemap({{"op", "list"}});
        REQUIRE(list["tilesets"].size() == 1);
        tileset_id = list["tilesets"][0]["id"];
    }
    ~TileDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json tilemap(const Json& command) const { return parsed(noor_doc_tilemap(doc, command.dump().c_str())); }
    Json entity(const std::string& id) const {
        const Json scene = parsed(noor_doc_scene(doc));  // kept alive for the loop (a temporary's member wouldn't be)
        for (const auto& e : scene["entities"])
            if (e["id"] == id) return e;
        return Json();
    }
};

} // namespace

TEST_CASE("tilemap api: list, info (the palette), and a stroke of painting is one undo step") {
    TileDoc d;
    REQUIRE(noor_doc_execute(d.doc, Json{{"op", "create"}, {"name", "Level"}, {"transform", {{"position", {5, 2, 0}}}},
                                        {"components", {{"tilemap", {{"tileset", d.tileset_id}, {"rows", Json::array({"0 0 0"})}}}}}}
                                       .dump()
                                       .c_str()) == 1);
    std::string id;
    const Json scene = parsed(noor_doc_scene(d.doc));
    for (const auto& e : scene["entities"])
        if (e["name"] == "Level") id = e["id"];
    REQUIRE(!id.empty());

    const Json info = d.tilemap({{"op", "info"}, {"tilemap", id}});
    CHECK_FALSE(info.contains("error"));
    CHECK(info["frames"].size() == 8);  // the square sheet's eight tiles
    CHECK(info["frames"][1] == Json::array({16, 0, 16, 16}));
    CHECK(info["width"] == 3);
    CHECK(info["height"] == 1);
    CHECK(info["solid"].size() == 5);
    CHECK(info["one_way"] == Json::array({4}));

    // One stroke: begin on (1, 1), paint (2, 1) and (3, 1) (past the right edge: it grows), end.
    d.tilemap({{"op", "paint"}, {"tilemap", id}, {"phase", "begin"}, {"cells", {{1, 1}}}, {"tile", 4}});
    d.tilemap({{"op", "paint"}, {"tilemap", id}, {"phase", "paint"}, {"cells", {{2, 1}, {3, 1}}}, {"tile", 4}});
    const Json end = d.tilemap({{"op", "paint"}, {"tilemap", id}, {"phase", "end"}, {"cells", Json::array()}, {"tile", 4}});
    Json rows = d.entity(id)["components"]["tilemap"]["rows"];
    CHECK(rows == Json::array({". 4 4 4", "0 0 0 ."}));
    // One undo takes the whole stroke back.
    CHECK(noor_doc_undo(d.doc) == 1);
    CHECK(d.entity(id)["components"]["tilemap"]["rows"] == Json::array({"0 0 0"}));
    CHECK(noor_doc_redo(d.doc) == 1);
    CHECK(d.entity(id)["components"]["tilemap"]["rows"] == rows);
    (void)end;
}

TEST_CASE("tilemap api: painting left of and below the map grows it and moves the entity, tiles stay put") {
    TileDoc d;
    REQUIRE(noor_doc_execute(d.doc, Json{{"op", "create"}, {"name", "Level"}, {"transform", {{"position", {5, 2, 0}}}},
                                        {"components", {{"tilemap", {{"tileset", d.tileset_id}, {"rows", Json::array({"1"})}}}}}}
                                       .dump()
                                       .c_str()) == 1);
    std::string id;
    const Json scene = parsed(noor_doc_scene(d.doc));
    for (const auto& e : scene["entities"])
        if (e["name"] == "Level") id = e["id"];
    // A cell two to the left and one below: the map becomes 3 x 2 and its origin moves (-2, -1) cells.
    d.tilemap({{"op", "paint"}, {"tilemap", id}, {"phase", "begin"}, {"cells", Json::array()}, {"tile", 6}});
    const Json r = d.tilemap({{"op", "paint"}, {"tilemap", id}, {"phase", "paint"}, {"cells", {{-2, -1}}}, {"tile", 6}});
    d.tilemap({{"op", "paint"}, {"tilemap", id}, {"phase", "end"}, {"cells", Json::array()}, {"tile", 6}});
    CHECK(r["shifted"] == Json::array({2, 1}));
    CHECK(r["width"] == 3);
    CHECK(r["height"] == 2);
    const Json e = d.entity(id);
    CHECK(e["components"]["tilemap"]["rows"] == Json::array({". . 1", "6 . ."}));
    // The old tile (now at (2, 1)) is where it was: origin (3, 1) + 2 cells right, 1 up = (5, 2).
    CHECK(e["transform"]["position"][0].get<float>() == doctest::Approx(3.0f));
    CHECK(e["transform"]["position"][1].get<float>() == doctest::Approx(1.0f));
    // Undo puts both back in one step.
    CHECK(noor_doc_undo(d.doc) == 1);
    const Json back = d.entity(id);
    CHECK(back["components"]["tilemap"]["rows"] == Json::array({"1"}));
    CHECK(back["transform"]["position"][0].get<float>() == doctest::Approx(5.0f));
    // Erasing outside the map changes nothing (and never grows it).
    const Json erased = d.tilemap({{"op", "paint"}, {"tilemap", id}, {"phase", "paint"}, {"cells", {{9, 9}, {-3, 0}}}, {"tile", -1}});
    CHECK(erased["changed"] == false);
    CHECK(d.entity(id)["components"]["tilemap"]["rows"] == Json::array({"1"}));
    // Not a tilemap: a clear error.
    CHECK(noor_doc_tilemap(d.doc, Json{{"op", "info"}, {"tilemap", noor::Guid::generate().to_string()}}.dump().c_str()) == nullptr);
}
