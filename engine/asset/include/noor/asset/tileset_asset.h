#pragma once
// Tilesets and tile grids (2D foundations, part 4, ADR-040).
//
// A tileset (`.noortileset`) says which sprite sheet the tiles come from and how each tile behaves:
//
//   {"noor_tileset": 1, "name": "Cave", "sheet": "<.noorsprite id>",
//    "solid": [0, 1, 2, 8], "one_way": [5]}
//
// Tiles are the sheet's frames (numbered left to right, top to bottom, as sprites count them). A `tilemap` component
// (game/tilemap.h) places them in a grid, one text line per row, top row first, so maps stay readable and diff well:
//
//   "rows": [". . . 5 5 5 . .",
//            "0 1 1 1 1 1 1 2"]
//
// `.` is an empty cell; any other token is a tile number. Cell (x, y) counts from the bottom-left: x to the right, y
// up (the 2D world's axes), so the last line is y = 0.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace noor::asset {

inline constexpr std::string_view kTilesetExtension = ".noortileset";
inline constexpr int kTilesetFormat = 1;

struct Tileset {
    std::string name;
    std::optional<Guid> sheet;  // the .noorsprite the tiles are cut from
    std::set<int> solid;        // collide on every side
    std::set<int> one_way;      // stood on from above, jumped up through
};

[[nodiscard]] Result<Tileset> parse_tileset(const Json& file);
[[nodiscard]] Result<Tileset> load_tileset_file(const std::filesystem::path& file);
[[nodiscard]] Json tileset_file_json(const Tileset& tileset);

// A grid of tiles: width x height cells, -1 empty. tiles[y * width + x], y = 0 the bottom row.
struct TileGrid {
    int width = 0, height = 0;
    std::vector<std::int32_t> tiles;
    std::uint64_t hash = 0;  // of the rows it came from (tile_rows_hash)

    [[nodiscard]] std::int32_t at(int x, int y) const noexcept {
        return x < 0 || y < 0 || x >= width || y >= height ? -1 : tiles[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

// The grid from a `rows` array (top row first). Rows may differ in length: the widest one sets the width, the rest are
// empty past their end. A token that is neither `.` nor a whole number is an error naming its row and column.
[[nodiscard]] Result<TileGrid> parse_tile_rows(const Json& rows);
// A cheap fingerprint of a `rows` array (FNV-1a over its text): what tells caches the map changed.
[[nodiscard]] std::uint64_t tile_rows_hash(const Json& rows);
// The rows for a grid (top row first, tokens separated by one space, `.` for empty).
[[nodiscard]] Json tile_rows_json(const TileGrid& grid);

// The tile at cell (x, y) of a `rows` array (y = 0 the bottom row): -1 if empty or outside.
[[nodiscard]] std::int32_t tile_at(const Json& rows, int x, int y);
// Sets cell (x, y) of a `rows` array in place (-1 empties it), rewriting only that row. Cells outside the grid are
// refused (false): grow a map by writing its rows.
bool set_tile(Json& rows, int x, int y, std::int32_t tile);

} // namespace noor::asset
