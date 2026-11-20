#include "noor/game/tilemap.h"

#include "noor/asset/texture.h"
#include "noor/foundation/log.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <mutex>
#include <unordered_set>

namespace noor::game {

using asset::Json;
using math::Vec2;
using math::Vec3;

namespace {

void warn_once(const std::string& message) {
    static std::mutex mutex;
    static std::unordered_set<std::string> said;
    std::scoped_lock lock(mutex);
    if (said.insert(message).second) NOOR_LOG_WARN("Tilemap", "{}", message);
}

Vec3 linear_colour(const Json& c, Vec3 fallback) {
    if (c.is_array() && c.size() == 3 && c[0].is_number() && c[1].is_number() && c[2].is_number())
        return {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
    if (!c.is_string()) return fallback;
    const std::string s = c.get<std::string>();
    if (s.size() != 7 || s[0] != '#') return fallback;
    const auto channel = [&](int i) {
        const float v = static_cast<float>(std::stoi(s.substr(static_cast<std::size_t>(1 + 2 * i), 2), nullptr, 16)) / 255.0f;
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return {channel(0), channel(1), channel(2)};
}

std::uint32_t pack(Vec3 c) {
    const auto b = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16);
}

// The rotation part of a matrix (its scale taken out first), as a quaternion.
math::Quat rotation_of(const math::Mat4& m) {
    Vec3 c[3];
    for (int i = 0; i < 3; ++i) {
        c[i] = Vec3{m.m[i].x, m.m[i].y, m.m[i].z};
        const float l = math::length(c[i]);
        c[i] = l > 1e-8f ? c[i] * (1.0f / l) : Vec3{i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f};
    }
    // Columns c[0..2] are the rotated axes: r(row, col) = c[col][row].
    const float r00 = c[0].x, r11 = c[1].y, r22 = c[2].z;
    const float trace = r00 + r11 + r22;
    math::Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(c[1].z - c[2].y) / s, (c[2].x - c[0].z) / s, (c[0].y - c[1].x) / s, 0.25f * s};
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q = {0.25f * s, (c[1].x + c[0].y) / s, (c[2].x + c[0].z) / s, (c[1].z - c[2].y) / s};
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q = {(c[1].x + c[0].y) / s, 0.25f * s, (c[2].y + c[1].z) / s, (c[2].x - c[0].z) / s};
    } else {
        const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q = {(c[2].x + c[0].z) / s, (c[2].y + c[1].z) / s, 0.25f * s, (c[0].y - c[1].x) / s};
    }
    return q;
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

} // namespace

TilemapSettings tilemap_settings(const Json& c) {
    TilemapSettings s;
    if (!c.is_object()) return s;
    const std::string layout = c.value("layout", std::string("square"));
    s.layout = layout == "isometric" ? TileLayout::Isometric : layout == "hexagonal" ? TileLayout::Hexagonal : TileLayout::Square;
    if (const auto it = c.find("cell"); it != c.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
        s.cell = {std::max(1e-4f, (*it)[0].get<float>()), std::max(1e-4f, (*it)[1].get<float>())};
    s.sort_y = c.value("sort", std::string()) == "y";
    s.sorted = c.contains("layer") || c.contains("order") || s.sort_y;
    if (c.contains("layer") && c["layer"].is_number()) s.layer = static_cast<std::int32_t>(std::lround(c["layer"].get<double>()));
    if (c.contains("order") && c["order"].is_number()) s.order = c["order"].get<float>();
    s.unlit = c.value("unlit", false);
    s.collision = c.value("collision", true);
    if (c.contains("color")) s.color = linear_colour(c["color"], s.color);
    return s;
}

std::shared_ptr<const asset::TileGrid> tile_grid(const Json& component) {
    static const auto empty = std::make_shared<const asset::TileGrid>();
    if (!component.is_object() || !component.contains("rows")) return empty;
    const Json& rows = component["rows"];
    const std::uint64_t hash = asset::tile_rows_hash(rows);
    // Parsed grids by their rows' fingerprint: maps that haven't changed are never parsed again, and the same map
    // drawn and collided is parsed once. Kept while anything holds them (and the last few, for the next frame).
    static std::mutex mutex;
    static std::unordered_map<std::uint64_t, std::weak_ptr<const asset::TileGrid>> cache;
    static std::vector<std::shared_ptr<const asset::TileGrid>> recent;
    std::scoped_lock lock(mutex);
    if (const auto it = cache.find(hash); it != cache.end())
        if (auto g = it->second.lock()) return g;
    auto parsed = asset::parse_tile_rows(rows);
    if (!parsed) {
        warn_once(parsed.error().message);
        return empty;
    }
    auto g = std::make_shared<const asset::TileGrid>(std::move(*parsed));
    cache[hash] = g;
    recent.push_back(g);
    if (recent.size() > 64) recent.erase(recent.begin());
    if (cache.size() > 1024)
        for (auto it = cache.begin(); it != cache.end();) it = it->second.expired() ? cache.erase(it) : std::next(it);
    return g;
}

Vec2 cell_centre(TileLayout layout, Vec2 cell, int x, int y) noexcept {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    switch (layout) {
    case TileLayout::Isometric: return {(fx - fy) * cell.x * 0.5f, (fx + fy) * cell.y * 0.5f};
    case TileLayout::Hexagonal: return {(fx + ((y & 1) != 0 ? 0.5f : 0.0f)) * cell.x, fy * cell.y * 0.75f};
    case TileLayout::Square: break;
    }
    return {fx * cell.x, fy * cell.y};
}

std::vector<CellRect> merge_cells(const asset::TileGrid& g, const std::set<int>& tiles) {
    std::vector<CellRect> out;
    if (tiles.empty() || g.width <= 0 || g.height <= 0) return out;
    std::vector<char> used(g.tiles.size(), 0);
    const auto wanted = [&](int x, int y) {
        return !used[static_cast<std::size_t>(y) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(x)] && tiles.contains(g.at(x, y));
    };
    for (int y = 0; y < g.height; ++y) {
        for (int x = 0; x < g.width; ++x) {
            if (!wanted(x, y)) continue;
            int w = 1;
            while (x + w < g.width && wanted(x + w, y)) ++w;  // the run along this row
            int h = 1;
            for (;; ++h) {  // grown upwards while the row above has the whole run free
                if (y + h >= g.height) break;
                bool full = true;
                for (int i = 0; i < w && full; ++i) full = wanted(x + i, y + h);
                if (!full) break;
            }
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i) used[static_cast<std::size_t>(y + j) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(x + i)] = 1;
            out.push_back({x, y, w, h});
            x += w - 1;
        }
    }
    return out;
}

std::vector<TileBox> tilemap_boxes(const asset::TileGrid& grid, const asset::Tileset& tileset, const TilemapSettings& s, const math::Mat4& world,
                                   float depth) {
    std::vector<TileBox> out;
    if (s.layout != TileLayout::Square || !s.collision) return out;
    const math::Quat rotation = rotation_of(world);
    const Vec3 scale{math::length(math::transform_vector(world, {1, 0, 0})), math::length(math::transform_vector(world, {0, 1, 0})),
                     math::length(math::transform_vector(world, {0, 0, 1}))};
    const auto add = [&](const std::set<int>& tiles, bool one_way) {
        for (const CellRect& r : merge_cells(grid, tiles)) {
            // The rectangle's middle: cells are centred on their coordinates, so it spans [x - 0.5, x + w - 0.5].
            const Vec2 mid{(static_cast<float>(r.x) + static_cast<float>(r.w) * 0.5f - 0.5f) * s.cell.x,
                           (static_cast<float>(r.y) + static_cast<float>(r.h) * 0.5f - 0.5f) * s.cell.y};
            const Vec3 half{static_cast<float>(r.w) * s.cell.x * 0.5f * scale.x, static_cast<float>(r.h) * s.cell.y * 0.5f * scale.y,
                            depth * 0.5f * scale.z};
            out.push_back({world.transform_point({mid.x, mid.y, 0.0f}), half, rotation, one_way});
        }
    };
    add(tileset.solid, false);
    add(tileset.one_way, true);
    return out;
}

// ---- drawing ----

TilemapLibrary::TilemapLibrary(render::Renderer& renderer, TextureLibrary& textures, asset::AssetFileResolver resolve)
    : renderer_(renderer), textures_(textures), resolve_(std::move(resolve)) {}

TilemapLibrary::~TilemapLibrary() {
    for (auto& [entity, b] : built_)
        for (auto m : b.chunks) renderer_.destroy_mesh(m);
    for (auto& [file, s] : sets_)
        for (auto& [key, m] : s.materials) renderer_.destroy_material(m);
}

std::size_t TilemapLibrary::refresh() {
    std::size_t changed = 0;
    for (auto it = sets_.begin(); it != sets_.end();) {
        std::error_code ec, ec2;
        const auto time = std::filesystem::last_write_time(it->first, ec);
        const auto sheet_time = it->second.sheet_file.empty() ? it->second.sheet_time : std::filesystem::last_write_time(it->second.sheet_file, ec2);
        if (!ec && !ec2 && time == it->second.time && sheet_time == it->second.sheet_time) {
            ++it;
            continue;
        }
        for (auto& [key, m] : it->second.materials) renderer_.destroy_material(m);
        it = sets_.erase(it);  // loads again when next drawn (with a new generation, so its maps rebuild)
        ++changed;
    }
    return changed;
}

TilemapLibrary::Set* TilemapLibrary::tileset(const std::filesystem::path& file) {
    const auto [it, fresh] = sets_.try_emplace(file.generic_string());
    Set& s = it->second;
    if (!fresh) return s.ok ? &s : nullptr;
    s.generation = ++generations_;
    std::error_code ec;
    s.time = std::filesystem::last_write_time(file, ec);
    auto ts = asset::load_tileset_file(file);
    if (!ts) {
        warn_once(ts.error().message);
        return nullptr;
    }
    const auto sheet_file = ts->sheet && resolve_ ? resolve_(*ts->sheet) : std::nullopt;
    if (!sheet_file) {
        warn_once(std::format("{}: its sprite sheet isn't in the project", file.filename().string()));
        return nullptr;
    }
    s.sheet_file = *sheet_file;
    s.sheet_time = std::filesystem::last_write_time(s.sheet_file, ec);
    auto sheet = asset::load_sprite_file(s.sheet_file);
    if (!sheet) {
        warn_once(sheet.error().message);
        return nullptr;
    }
    const auto texture = sheet->texture && resolve_ ? resolve_(*sheet->texture) : std::nullopt;
    if (!texture) {
        warn_once(std::format("{}: its texture isn't in the project", s.sheet_file.filename().string()));
        return nullptr;
    }
    auto info = asset::texture_info(*texture, asset::TextureSettings{});
    if (!info) {
        warn_once(info.error().describe());
        return nullptr;
    }
    if (auto sliced = asset::slice(*sheet, static_cast<int>(info->source_width), static_cast<int>(info->source_height)); !sliced) {
        warn_once(std::format("{}: {}", s.sheet_file.filename().string(), sliced.error().message));
        return nullptr;
    }
    s.tileset = std::move(*ts);
    s.sheet = std::move(*sheet);
    s.texture_width = static_cast<int>(info->source_width);
    s.texture_height = static_cast<int>(info->source_height);
    s.texture = textures_.get(*texture);
    s.ok = true;
    return &s;
}

render::MaterialHandle TilemapLibrary::material(Set& s, int look, Vec3 tint) {
    const auto key = std::make_pair(look, pack(tint));
    if (const auto it = s.materials.find(key); it != s.materials.end()) return it->second;
    render::MaterialDesc d;
    d.base_color = {tint.x, tint.y, tint.z, 1.0f};
    d.roughness = 0.9f;
    d.alpha = (look & 1) != 0 ? render::AlphaMode::Blend : render::AlphaMode::Cutout;
    d.coverage = (look & 1) != 0;
    d.alpha_cutoff = 0.5f;
    d.double_sided = true;
    d.nearest_filter = s.sheet.nearest;
    d.unlit = (look & 2) != 0;
    d.base_color_texture = s.texture;
    auto m = renderer_.create_material(d);
    const render::MaterialHandle h = m ? *m : render::MaterialHandle{};
    s.materials.emplace(key, h);
    return h;
}

void TilemapLibrary::build(Built& b, const SceneView::Tilemap& map, const TilemapSettings& st, Set& s) {
    for (auto m : b.chunks) renderer_.destroy_mesh(m);
    b.chunks.clear();
    const asset::TileGrid& g = *map.grid;
    const Vec2 size = s.sheet.frame_size_m();
    const Vec2 pivot = s.sheet.pivot;
    // Same tilt as sprites: the normal leans up a little so tiles take the sky's light too.
    const Vec3 n = math::normalize(Vec3{0.0f, 0.5f, 0.866f});
    // Nearest-texel sampling picks the next tile at a quad's exact edge now and then: step a hundredth of a texel in.
    const float inset_u = 0.01f / static_cast<float>(std::max(1, s.texture_width));
    const float inset_v = 0.01f / static_cast<float>(std::max(1, s.texture_height));
    const int frames = static_cast<int>(s.sheet.frames.size());
    for (int cy = 0; cy < g.height; cy += kTileChunk) {
        for (int cx = 0; cx < g.width; cx += kTileChunk) {
            std::vector<render::Vertex> verts;
            std::vector<std::uint32_t> idx;
            // Back to front within the chunk: isometric cells further up (x + y larger) first, hexagonal rows from the
            // top; each drawn a hair further back too, so depth testing agrees with that order wherever tiles overlap.
            std::vector<std::pair<int, int>> cells;
            for (int y = cy; y < std::min(cy + kTileChunk, g.height); ++y)
                for (int x = cx; x < std::min(cx + kTileChunk, g.width); ++x)
                    if (const auto t = g.at(x, y); t >= 0 && t < frames) cells.emplace_back(x, y);
            const auto depth_rank = [&](int x, int y) {
                return st.layout == TileLayout::Isometric ? x + y : st.layout == TileLayout::Hexagonal ? y : 0;
            };
            std::ranges::stable_sort(cells, [&](auto a, auto b) { return depth_rank(a.first, a.second) > depth_rank(b.first, b.second); });
            for (const auto& [x, y] : cells) {
                const auto& f = s.sheet.frames[static_cast<std::size_t>(g.at(x, y))];
                const Vec2 c = cell_centre(st.layout, st.cell, x, y);
                const float z = -1e-3f * static_cast<float>(depth_rank(x, y));
                const float x0 = c.x - pivot.x * size.x, y0 = c.y - pivot.y * size.y;
                const float u0 = f.uv_offset.x + inset_u, u1 = f.uv_offset.x + f.uv_scale.x - inset_u;
                const float v0 = f.uv_offset.y + inset_v, v1 = f.uv_offset.y + f.uv_scale.y - inset_v;  // v0: the frame's top
                const auto base = static_cast<std::uint32_t>(verts.size());
                verts.push_back({{x0, y0, z}, n, {1, 1, 1}, {u0, v1}, {1, 0, 0, 1}});
                verts.push_back({{x0 + size.x, y0, z}, n, {1, 1, 1}, {u1, v1}, {1, 0, 0, 1}});
                verts.push_back({{x0 + size.x, y0 + size.y, z}, n, {1, 1, 1}, {u1, v0}, {1, 0, 0, 1}});
                verts.push_back({{x0, y0 + size.y, z}, n, {1, 1, 1}, {u0, v0}, {1, 0, 0, 1}});
                for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
            }
            if (idx.empty()) continue;
            if (auto m = renderer_.create_mesh({verts, idx})) {
                b.chunks.push_back(*m);
                ++chunks_built_;
            }
        }
    }
}

void TilemapLibrary::add_draws(std::span<const SceneView::Tilemap> maps, std::vector<render::DrawItem>& out) {
    for (const auto& map : maps) {
        if (!map.grid || map.grid->tiles.empty()) continue;
        Set* s = tileset(map.tileset);
        if (!s) continue;
        const TilemapSettings st = tilemap_settings(map.settings);
        std::uint64_t key = mix(map.grid->hash, s->generation);
        key = mix(key, static_cast<std::uint64_t>(st.layout));
        key = mix(key, std::bit_cast<std::uint32_t>(st.cell.x));
        key = mix(key, std::bit_cast<std::uint32_t>(st.cell.y));
        Built& b = built_[map.entity];
        if (b.key != key) {
            build(b, map, st, *s);
            b.key = key;
        }
        const int look = (st.sorted ? 1 : 0) | (st.unlit ? 2 : 0);
        const render::MaterialHandle mat = material(*s, look, st.color);
        for (auto mesh : b.chunks) {
            render::DrawItem item;
            item.mesh = mesh;
            item.world_from_object = map.world;
            item.material = mat;
            item.cast_shadow = map.settings.value("cast_shadows", false);
            item.sort_layer = st.layer;
            item.sort_order = st.order;
            out.push_back(item);
        }
    }
}

} // namespace noor::game
