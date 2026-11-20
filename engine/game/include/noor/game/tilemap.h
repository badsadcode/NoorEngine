#pragma once
// Tilemaps (2D foundations, part 4, ADR-040): a grid of tiles from a tileset (asset/tileset_asset.h), drawn in chunks
// and colliding by merged rectangles.
//
//   "tilemap": {"tileset": "<.noortileset id>", "layout": "square" | "isometric" | "hexagonal",
//               "cell": [1, 1] (metres between cells), "rows": [". . 5 5", "0 1 1 2"] (top row first),
//               "layer": 0, "order": 0, "sort": "y", "unlit": false, "color": "#FFFFFF", "collision": true}
//
// Cells sit in the entity's XY plane, cell (0, 0) at its origin, x to the right and y up:
//   - square: cell (x, y) is centred at (x * cell.x, y * cell.y);
//   - isometric (diamonds, the usual 2:1 picture): at ((x - y) * cell.x / 2, (x + y) * cell.y / 2), so x runs up and to
//     the right, y up and to the left;
//   - hexagonal (pointy tops, odd rows shifted right by half a cell): at ((x + (y odd ? 0.5 : 0)) * cell.x, y * cell.y * 0.75).
// Each tile is drawn as its sheet frame (its size from the sheet's pixels_per_metre, placed by the sheet's pivot at the
// cell's centre), so tall isometric tiles (walls, trees) rise above their diamond. Tiles further back are drawn behind
// nearer ones (isometric and hexagonal maps keep that order within each chunk).
//
// Like sprites, a tilemap with "layer", "order" or "sort" is drawn in the see-through pass in that order (part 3).
// Square maps collide: solid tiles as boxes and one-way tiles as one-way platforms, neighbours merged into as few
// rectangles as cover them exactly. (Isometric and hexagonal maps don't collide yet: walkable grids are game rules.)

#include "noor/asset/sprite_asset.h"
#include "noor/asset/tileset_asset.h"
#include "noor/game/game.h"
#include "noor/game/texture_library.h"
#include "noor/render/renderer.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::game {

enum class TileLayout : std::uint8_t { Square, Isometric, Hexagonal };

// Chunks of this many cells a side are one draw each.
inline constexpr int kTileChunk = 32;

struct TilemapSettings {
    TileLayout layout = TileLayout::Square;
    math::Vec2 cell{1, 1};
    std::int32_t layer = 0;
    float order = 0.0f;
    bool sorted = false;  // "layer", "order" or "sort" given: drawn in the see-through pass, in that order
    bool sort_y = false;
    bool unlit = false;
    bool collision = true;
    math::Vec3 color{1, 1, 1};  // linear
};
[[nodiscard]] TilemapSettings tilemap_settings(const asset::Json& component);

// The component's tile grid, parsed once per distinct `rows` (shared by everything that asks: drawing, collision); an
// empty grid if the rows don't parse (the problem is logged once).
[[nodiscard]] std::shared_ptr<const asset::TileGrid> tile_grid(const asset::Json& component);

// Where a cell's centre is, in the tilemap's own plane (metres).
[[nodiscard]] math::Vec2 cell_centre(TileLayout layout, math::Vec2 cell, int x, int y) noexcept;

// The cell whose centre is nearest a point in the tilemap's plane (the inverse of cell_centre): the cell under the
// pointer when painting. Isometric: the diamond it's in; hexagonal: the hexagon it's in.
struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};
[[nodiscard]] Cell cell_at(TileLayout layout, math::Vec2 cell, math::Vec2 point) noexcept;

// The corners of a cell's outline in the tilemap's plane (4 for squares and diamonds, 6 for hexagons): the editor
// draws it under the pointer.
[[nodiscard]] std::vector<math::Vec2> cell_outline(TileLayout layout, math::Vec2 cell, int x, int y);

// Paints `tile` (-1: erases) into `cells` of a tilemap component, in place. Cells beyond the map's right or top edge
// grow it; cells left of or below (0, 0) grow it the other way, shifting every cell up by `shift` (so the map's cells
// keep their places, the entity must move by cell_centre(-shift.x, -shift.y): the caller does that). Hexagonal maps
// shift rows in pairs, so odd rows stay odd. Returns whether anything changed.
struct PaintResult {
    bool changed = false;
    Cell shift;
};
[[nodiscard]] PaintResult paint_tiles(asset::Json& component, std::span<const Cell> cells, std::int32_t tile);

// Cells as rectangles: [x, x + w) x [y, y + h).
struct CellRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const CellRect&) const = default;
};
// The cells holding one of `tiles`, covered exactly by as few rectangles as a greedy pass finds: the longest runs along
// each row first, then each run grown upwards while the rows above have the same run. Deterministic.
[[nodiscard]] std::vector<CellRect> merge_cells(const asset::TileGrid& grid, const std::set<int>& tiles);

// A collision box for a square tilemap, in world space.
struct TileBox {
    math::Vec3 centre;
    math::Vec3 half;
    math::Quat rotation;
    bool one_way = false;
};
// The boxes of a square tilemap whose entity is at `world` (solid and one-way tiles of `tileset`), `depth` thick.
[[nodiscard]] std::vector<TileBox> tilemap_boxes(const asset::TileGrid& grid, const asset::Tileset& tileset, const TilemapSettings& settings,
                                                 const math::Mat4& world, float depth = 1.0f);

// Draws tilemaps: one mesh per chunk, rebuilt only when the map's rows (or its tileset) change.
class TilemapLibrary {
public:
    TilemapLibrary(render::Renderer& renderer, TextureLibrary& textures, asset::AssetFileResolver resolve);
    ~TilemapLibrary();
    TilemapLibrary(const TilemapLibrary&) = delete;
    TilemapLibrary& operator=(const TilemapLibrary&) = delete;

    void set_resolver(asset::AssetFileResolver resolve) { resolve_ = std::move(resolve); }
    // Tilesets and sheets whose files changed load again (call a few times a second). Returns how many.
    std::size_t refresh();
    void add_draws(std::span<const SceneView::Tilemap> maps, std::vector<render::DrawItem>& out);
    // Chunk meshes built since this library was made (for tests and cost notes).
    [[nodiscard]] std::size_t chunks_built() const noexcept { return chunks_built_; }

private:
    struct Set {
        bool ok = false;
        std::filesystem::file_time_type time{}, sheet_time{};
        std::filesystem::path sheet_file;
        asset::Tileset tileset;
        asset::SpriteSheet sheet;
        int texture_width = 0, texture_height = 0;
        render::TextureHandle texture;
        std::map<std::pair<int, std::uint32_t>, render::MaterialHandle> materials;  // (look, tint) -> material
        std::uint64_t generation = 0;                                               // bumps when reloaded
    };
    struct Built {
        std::uint64_t key = 0;  // rows, layout, cell and tileset generation together
        std::vector<render::MeshHandle> chunks;
    };
    Set* tileset(const std::filesystem::path& file);
    render::MaterialHandle material(Set& s, int look, math::Vec3 tint);
    void build(Built& b, const SceneView::Tilemap& map, const TilemapSettings& settings, Set& s);

    render::Renderer& renderer_;
    TextureLibrary& textures_;
    asset::AssetFileResolver resolve_;
    std::unordered_map<std::string, Set> sets_;
    std::unordered_map<Guid, Built> built_;
    std::uint64_t generations_ = 0;
    std::size_t chunks_built_ = 0;
};

} // namespace noor::game
