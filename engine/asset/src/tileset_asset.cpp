#include "noor/asset/tileset_asset.h"

#include "noor/foundation/file_io.h"

#include <algorithm>
#include <charconv>
#include <format>

namespace noor::asset {

namespace {

std::set<int> int_set(const Json& j, const char* key) {
    std::set<int> out;
    if (const auto it = j.find(key); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_number_integer() && v.get<int>() >= 0) out.insert(v.get<int>());
    return out;
}

// The tokens of one row (separated by spaces or tabs).
std::vector<std::string_view> tokens(std::string_view row) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < row.size()) {
        while (i < row.size() && (row[i] == ' ' || row[i] == '\t')) ++i;
        const std::size_t start = i;
        while (i < row.size() && row[i] != ' ' && row[i] != '\t') ++i;
        if (i > start) out.push_back(row.substr(start, i - start));
    }
    return out;
}

// -1 for ".", the number otherwise; nullopt if it's neither.
std::optional<std::int32_t> token_value(std::string_view t) {
    if (t == ".") return -1;
    std::int32_t v = 0;
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (ec != std::errc{} || end != t.data() + t.size() || v < -1) return std::nullopt;
    return v;
}

} // namespace

Result<Tileset> parse_tileset(const Json& file) {
    if (!file.is_object() || !file.contains("noor_tileset"))
        return fail(ErrorCode::InvalidArgument, "not a tileset (no \"noor_tileset\")");
    if (file["noor_tileset"].is_number_integer() && file["noor_tileset"].get<int>() > kTilesetFormat)
        return fail(ErrorCode::InvalidArgument, std::format("tileset format {} is newer than this engine reads ({})", file["noor_tileset"].get<int>(), kTilesetFormat));
    Tileset t;
    t.name = file.value("name", std::string());
    t.sheet = guid_field(file, "sheet");
    t.solid = int_set(file, "solid");
    t.one_way = int_set(file, "one_way");
    t.floor = int_set(file, "floor");
    return t;
}

Result<Tileset> load_tileset_file(const std::filesystem::path& file) {
    auto text = fs::read_file(file, 16u << 20);
    if (!text) return std::unexpected(text.error());
    const Json j = Json::parse(*text, nullptr, false);
    if (j.is_discarded()) return fail(ErrorCode::InvalidArgument, std::format("{}: not valid JSON", file.filename().string()));
    auto t = parse_tileset(j);
    if (!t) return fail(ErrorCode::InvalidArgument, std::format("{}: {}", file.filename().string(), t.error().message));
    return t;
}

Json tileset_file_json(const Tileset& t) {
    Json j{{"noor_tileset", kTilesetFormat}, {"name", t.name}, {"solid", Json::array()}, {"one_way", Json::array()}};
    if (t.sheet) j["sheet"] = t.sheet->to_string();
    for (int s : t.solid) j["solid"].push_back(s);
    for (int s : t.one_way) j["one_way"].push_back(s);
    if (!t.floor.empty()) {  // only when there are any: older files stay as they were
        j["floor"] = Json::array();
        for (int s : t.floor) j["floor"].push_back(s);
    }
    return j;
}

Result<TileGrid> parse_tile_rows(const Json& rows) {
    TileGrid g;
    g.hash = tile_rows_hash(rows);
    if (!rows.is_array()) return g;  // no rows: an empty map
    std::vector<std::vector<std::int32_t>> parsed;
    parsed.reserve(rows.size());
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (!rows[r].is_string()) return fail(ErrorCode::InvalidArgument, std::format("tilemap row {} isn't text", r + 1));
        auto& line = parsed.emplace_back();
        const auto ts = tokens(rows[r].get_ref<const std::string&>());
        line.reserve(ts.size());
        for (std::size_t c = 0; c < ts.size(); ++c) {
            const auto v = token_value(ts[c]);
            if (!v) return fail(ErrorCode::InvalidArgument, std::format("tilemap row {}, column {}: \"{}\" is neither . nor a tile number", r + 1, c + 1, ts[c]));
            line.push_back(*v);
        }
        g.width = std::max(g.width, static_cast<int>(line.size()));
    }
    g.height = static_cast<int>(parsed.size());
    g.tiles.assign(static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height), -1);
    for (int r = 0; r < g.height; ++r) {
        const int y = g.height - 1 - r;  // the first row is the top
        const auto& line = parsed[static_cast<std::size_t>(r)];
        std::copy(line.begin(), line.end(), g.tiles.begin() + static_cast<std::ptrdiff_t>(y) * g.width);
    }
    return g;
}

std::uint64_t tile_rows_hash(const Json& rows) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](unsigned char c) {
        h ^= c;
        h *= 1099511628211ull;
    };
    if (!rows.is_array()) return h;
    for (const auto& r : rows) {
        if (r.is_string())
            for (const char c : r.get_ref<const std::string&>()) mix(static_cast<unsigned char>(c));
        mix('\n');
    }
    return h;
}

Json tile_rows_json(const TileGrid& g) {
    Json rows = Json::array();
    for (int y = g.height - 1; y >= 0; --y) {
        std::string line;
        for (int x = 0; x < g.width; ++x) {
            if (x > 0) line += ' ';
            const auto t = g.at(x, y);
            line += t < 0 ? std::string(".") : std::to_string(t);
        }
        rows.push_back(std::move(line));
    }
    return rows;
}

std::int32_t tile_at(const Json& rows, int x, int y) {
    if (!rows.is_array() || x < 0 || y < 0 || y >= static_cast<int>(rows.size())) return -1;
    const auto& row = rows[rows.size() - 1 - static_cast<std::size_t>(y)];
    if (!row.is_string()) return -1;
    const auto ts = tokens(row.get_ref<const std::string&>());
    if (x >= static_cast<int>(ts.size())) return -1;
    return token_value(ts[static_cast<std::size_t>(x)]).value_or(-1);
}

bool set_tile(Json& rows, int x, int y, std::int32_t tile) {
    if (!rows.is_array() || x < 0 || y < 0 || y >= static_cast<int>(rows.size())) return false;
    auto& row = rows[rows.size() - 1 - static_cast<std::size_t>(y)];
    if (!row.is_string()) return false;
    const std::string old = row.get<std::string>();
    auto ts = tokens(old);
    std::vector<std::string> parts(ts.begin(), ts.end());
    int width = 0;
    for (const auto& r : rows)
        if (r.is_string()) width = std::max(width, static_cast<int>(tokens(r.get_ref<const std::string&>()).size()));
    if (x >= width) return false;  // outside the grid
    while (static_cast<int>(parts.size()) <= x) parts.emplace_back(".");
    parts[static_cast<std::size_t>(x)] = tile < 0 ? std::string(".") : std::to_string(tile);
    std::string line;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) line += ' ';
        line += parts[i];
    }
    row = std::move(line);
    return true;
}

} // namespace noor::asset
