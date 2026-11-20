// Tilemaps (2D foundations, part 4, ADR-040): the rows format, merging solid cells into rectangles, where cells sit in
// each layout, collision boxes, and a character standing on (and falling through) a map whose tiles change.
#include "noor/asset/tileset_asset.h"
#include "noor/game/tilemap.h"
#include "noor/physics/physics.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>

using namespace noor;
using asset::Json;

TEST_CASE("tilemap rows: top row first, . empty, ragged rows padded, errors name the row and column") {
    const Json rows = Json::array({". . 5 5", "0 1 1 1 2", "3"});
    const auto g = asset::parse_tile_rows(rows);
    REQUIRE(g);
    CHECK(g->width == 5);
    CHECK(g->height == 3);
    CHECK(g->at(0, 0) == 3);   // the last line is the bottom row
    CHECK(g->at(1, 0) == -1);  // past a short row's end: empty
    CHECK(g->at(4, 1) == 2);
    CHECK(g->at(0, 2) == -1);
    CHECK(g->at(2, 2) == 5);
    CHECK(g->at(9, 9) == -1);  // outside
    // Written back, then read again: the same grid.
    const auto again = asset::parse_tile_rows(asset::tile_rows_json(*g));
    REQUIRE(again);
    CHECK(again->tiles == g->tiles);
    // A bad token is refused, saying where.
    const auto bad = asset::parse_tile_rows(Json::array({"0 0", "0 x"}));
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("row 2, column 2") != std::string::npos);
    // The fingerprint changes with the text, and only with it.
    CHECK(asset::tile_rows_hash(rows) == asset::tile_rows_hash(Json::array({". . 5 5", "0 1 1 1 2", "3"})));
    CHECK(asset::tile_rows_hash(rows) != asset::tile_rows_hash(Json::array({". . 5 5", "0 1 1 1 2", "4"})));
}

TEST_CASE("tilemap rows: reading and setting one cell rewrites just that row") {
    Json rows = Json::array({". . .", "0 1 2"});
    CHECK(asset::tile_at(rows, 1, 0) == 1);
    CHECK(asset::tile_at(rows, 1, 1) == -1);
    CHECK(asset::set_tile(rows, 1, 1, 7));
    CHECK(rows[0] == ". 7 .");
    CHECK(rows[1] == "0 1 2");
    CHECK(asset::set_tile(rows, 0, 0, -1));
    CHECK(rows[1] == ". 1 2");
    CHECK_FALSE(asset::set_tile(rows, 3, 0, 1));  // outside: refused
    CHECK_FALSE(asset::set_tile(rows, 0, 2, 1));
}

TEST_CASE("tilemap collision: solid cells merged into as few rectangles as cover them exactly") {
    const auto grid = [](std::initializer_list<const char*> lines) {
        Json rows = Json::array();
        for (const char* l : lines) rows.push_back(l);
        return *asset::parse_tile_rows(rows);
    };
    const std::set<int> solid{1};
    // A full block: one rectangle.
    CHECK(game::merge_cells(grid({"1 1 1", "1 1 1"}), solid) == std::vector<game::CellRect>{{0, 0, 3, 2}});
    // An L: the bottom run, then the column above its left end.
    CHECK(game::merge_cells(grid({"1 . .", "1 . .", "1 1 1"}), solid) == std::vector<game::CellRect>{{0, 0, 3, 1}, {0, 1, 1, 2}});
    // A checkerboard can't merge at all: one each.
    CHECK(game::merge_cells(grid({"1 . 1", ". 1 .", "1 . 1"}), solid).size() == 5);
    // Every solid cell covered exactly once, whatever the shape.
    const auto g = grid({"1 1 . 1 1 1", "1 1 1 1 . 1", ". 1 1 1 1 1", "1 1 . . 1 1"});
    std::vector<int> covered(g.tiles.size(), 0);
    for (const auto& r : game::merge_cells(g, solid))
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) ++covered[static_cast<std::size_t>(y * g.width + x)];
    for (int y = 0; y < g.height; ++y)
        for (int x = 0; x < g.width; ++x) CHECK(covered[static_cast<std::size_t>(y * g.width + x)] == (g.at(x, y) == 1 ? 1 : 0));
    // Only the kinds asked for.
    CHECK(game::merge_cells(grid({"2 2 2"}), solid).empty());
}

TEST_CASE("tilemap layouts: where cells sit, and the boxes a square map collides with") {
    using game::TileLayout;
    const math::Vec2 cell{1, 1};
    CHECK(game::cell_centre(TileLayout::Square, cell, 3, 2).x == 3.0f);
    CHECK(game::cell_centre(TileLayout::Square, cell, 3, 2).y == 2.0f);
    // Isometric diamonds 2 x 1: x runs up and right, y up and left.
    const math::Vec2 iso{2, 1};
    CHECK(game::cell_centre(TileLayout::Isometric, iso, 1, 0).x == 1.0f);
    CHECK(game::cell_centre(TileLayout::Isometric, iso, 1, 0).y == 0.5f);
    CHECK(game::cell_centre(TileLayout::Isometric, iso, 0, 1).x == -1.0f);
    CHECK(game::cell_centre(TileLayout::Isometric, iso, 0, 1).y == 0.5f);
    // Hexagons: odd rows shifted right by half a cell, rows three quarters of a cell apart.
    CHECK(game::cell_centre(TileLayout::Hexagonal, cell, 0, 1).x == 0.5f);
    CHECK(game::cell_centre(TileLayout::Hexagonal, cell, 0, 1).y == 0.75f);
    CHECK(game::cell_centre(TileLayout::Hexagonal, cell, 2, 2).x == 2.0f);

    // A floor of three solid tiles and a one-way plank above, at (10, 5): two boxes, in the right places.
    asset::Tileset ts;
    ts.solid = {0};
    ts.one_way = {4};
    Json rows = Json::array({". 4 .", "0 0 0"});
    const auto g = *asset::parse_tile_rows(rows);
    const auto boxes = game::tilemap_boxes(g, ts, game::tilemap_settings(Json{{"cell", {1, 1}}}), math::translation({10, 5, 0}));
    REQUIRE(boxes.size() == 2);
    CHECK(boxes[0].centre.x == doctest::Approx(11.0f));  // cells 0..2 centred on x 0..2: the middle at 1
    CHECK(boxes[0].centre.y == doctest::Approx(5.0f));
    CHECK(boxes[0].half.x == doctest::Approx(1.5f));
    CHECK_FALSE(boxes[0].one_way);
    CHECK(boxes[1].one_way);
    CHECK(boxes[1].centre.y == doctest::Approx(6.0f));
    // Isometric and hexagonal maps don't collide (yet), and collision can be switched off.
    CHECK(game::tilemap_boxes(g, ts, game::tilemap_settings(Json{{"layout", "isometric"}}), math::Mat4::identity()).empty());
    CHECK(game::tilemap_boxes(g, ts, game::tilemap_settings(Json{{"collision", false}}), math::Mat4::identity()).empty());
}

TEST_CASE("tilemap + physics: a character stands on solid tiles, on a one-way tile, and falls when a script breaks the floor") {
    asset::Tileset ts;
    ts.solid = {0};
    ts.one_way = {4};
    physics::PhysicsSystem::Options o;
    o.tilemap_key = [](const Json& c) { return asset::tile_rows_hash(c["rows"]); };
    o.tilemap_shapes = [&ts](const scene::Entity& e, const runtime::World&) {
        std::vector<physics::StaticShape> out;
        const Json& c = e.components["tilemap"];
        for (const auto& b : game::tilemap_boxes(*game::tile_grid(c), ts, game::tilemap_settings(c), math::Mat4::identity())) {
            physics::StaticShape s;
            s.kind = physics::StaticShape::Kind::Box;
            s.centre = b.centre;
            s.half = b.half;
            s.rotation = b.rotation;
            s.one_way = b.one_way;
            out.push_back(s);
        }
        return out;
    };
    // Cells are 1 m, centred on whole numbers: the floor (row y = 0) has its top at y 0.5, the plank (y = 3) at 3.5.
    scene::Scene s;
    scene::Entity map;
    map.id = Guid::generate();
    map.name = "Map";
    map.components["tilemap"] = {{"tileset", Guid::generate().to_string()},
                                 {"rows", Json::array({". . . . . . . .", ". . . . . 4 4 .", ". . . . . . . .", ". . . . . . . .", "0 0 0 0 0 0 0 0"})}};
    scene::Entity hero;
    hero.id = Guid::generate();
    hero.name = "Hero";
    hero.transform.position = {2, 1.0f, 0};
    hero.components["character"] = {{"radius", 0.3}, {"height", 0.9}, {"plane", "xy"}, {"gravity_scale", 3}};
    scene::Entity faller = hero;
    faller.id = Guid::generate();
    faller.name = "On the plank";
    faller.transform.position = {5.5f, 4.5f, 0};
    s.entities = {map, hero, faller};
    runtime::World world(s);
    auto p = std::make_unique<physics::PhysicsSystem>(std::move(o));
    auto* phys = p.get();
    world.add_system(std::move(p));
    for (int i = 0; i < 60; ++i) world.frame(1.0 / 60.0, {});
    const auto y_of = [&](const scene::Entity& e) { return world.find(e.id)->transform.position.y; };
    MESSAGE("standing: hero " << y_of(hero) << ", on the plank " << y_of(faller));
    CHECK(y_of(hero) == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(y_of(faller) == doctest::Approx(3.5f).epsilon(0.02));  // fell onto the one-way plank (row y = 3: its top at 3.5)
    CHECK(phys->is_grounded(hero.id));
    // Break the floor under the hero (as a script would): the boxes are rebuilt and he falls.
    CHECK(runtime::api::set_tile(world, map.id, 2, 0, -1));
    for (int i = 0; i < 60; ++i) world.frame(1.0 / 60.0, {});
    MESSAGE("after the floor broke: hero " << y_of(hero));
    CHECK(y_of(hero) < -1.0f);
    CHECK(runtime::api::tile(world, map.id, 2, 0) == -1);
    CHECK(runtime::api::tile(world, map.id, 1, 0) == 0);
}
