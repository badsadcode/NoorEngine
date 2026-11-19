#include "noor/asset/sprite_asset.h"

#include "noor/foundation/file_io.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace noor::asset {
namespace {

int integer(const Json& j, const char* key, int fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? static_cast<int>(std::lround(j[key].get<double>())) : fallback;
}
float number(const Json& j, const char* key, float fallback) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

} // namespace

math::Vec2 SpriteSheet::frame_size_m() const noexcept {
    const float ppm = std::max(1e-3f, pixels_per_metre);
    return {static_cast<float>(frame_width) / ppm, static_cast<float>(frame_height) / ppm};
}

int SpriteSheet::frame_at(std::string_view animation, double seconds) const {
    const auto it = animations.find(std::string(animation));
    if (it == animations.end() || it->second.frames.empty()) return 0;
    const auto& a = it->second;
    const auto count = static_cast<long long>(a.frames.size());
    long long k = a.fps > 0.0f ? static_cast<long long>(std::floor(std::max(0.0, seconds) * static_cast<double>(a.fps) + 1e-9)) : 0;
    k = a.loop ? k % count : std::min(k, count - 1);
    return a.frames[static_cast<std::size_t>(k)];
}

Result<SpriteSheet> parse_sprite_sheet(const Json& file) {
    if (!file.is_object() || !file.contains("noor_sprite") || !file["noor_sprite"].is_number_integer())
        return fail(ErrorCode::ParseError, "not a sprite sheet (no \"noor_sprite\")");
    if (file["noor_sprite"].get<int>() > kSpriteFormat)
        return fail(ErrorCode::ParseError, std::format("a sprite sheet made by a newer NoorEngine (format {})", file["noor_sprite"].get<int>()));
    SpriteSheet s;
    s.name = file.value("name", std::string());
    s.texture = guid_field(file, "texture");
    if (file.contains("grid") && file["grid"].is_object()) {
        const auto& g = file["grid"];
        s.columns = std::max(0, integer(g, "columns", 0));
        s.rows = std::max(0, integer(g, "rows", 0));
        s.frame_width = std::max(0, integer(g, "frame_width", 0));
        s.frame_height = std::max(0, integer(g, "frame_height", 0));
        s.margin = std::max(0, integer(g, "margin", 0));
        s.spacing = std::max(0, integer(g, "spacing", 0));
    }
    if (file.contains("pivot") && file["pivot"].is_array() && file["pivot"].size() == 2 && file["pivot"][0].is_number() && file["pivot"][1].is_number())
        s.pivot = {file["pivot"][0].get<float>(), file["pivot"][1].get<float>()};
    s.pixels_per_metre = std::max(1e-3f, number(file, "pixels_per_metre", s.pixels_per_metre));
    s.nearest = file.value("filter", std::string("nearest")) != "linear";
    if (file.contains("animations") && file["animations"].is_object())
        for (const auto& [name, a] : file["animations"].items()) {
            if (!a.is_object()) continue;
            SpriteAnimation anim;
            if (a.contains("frames") && a["frames"].is_array()) {
                for (const auto& f : a["frames"])
                    if (f.is_number()) anim.frames.push_back(std::max(0, static_cast<int>(f.get<double>())));
            } else if (a.contains("frames") && a["frames"].is_object()) {
                // Capped: a typo like "to": 2000000000 mustn't make two billion frames (no sheet has 64k of them).
                const int from = std::clamp(integer(a["frames"], "from", 0), 0, 65535), to = std::clamp(integer(a["frames"], "to", 0), 0, 65535);
                for (int f = from; from <= to ? f <= to : f >= to; f += from <= to ? 1 : -1) anim.frames.push_back(f);
            }
            anim.fps = std::max(0.0f, number(a, "fps", anim.fps));
            if (a.contains("loop") && a["loop"].is_boolean()) anim.loop = a["loop"].get<bool>();
            s.animations.emplace(name, std::move(anim));
        }
    return s;
}

Result<SpriteSheet> load_sprite_file(const std::filesystem::path& file) {
    auto text = fs::read_file(file, 4 << 20);
    if (!text) return std::unexpected(text.error());
    auto json = parse_json(*text, file.generic_string());
    if (!json) return std::unexpected(json.error());
    auto sheet = parse_sprite_sheet(*json);
    if (!sheet) return fail(ErrorCode::ParseError, std::format("{}: {}", file.filename().string(), sheet.error().message));
    if (sheet->name.empty()) sheet->name = file.stem().string();
    return sheet;
}

Status slice(SpriteSheet& s, int width, int height) {
    if (width <= 0 || height <= 0) return fail(ErrorCode::InvalidArgument, "a sprite sheet's texture has no size");
    const int inner_w = width - 2 * s.margin, inner_h = height - 2 * s.margin;
    if (inner_w <= 0 || inner_h <= 0) return fail(ErrorCode::InvalidArgument, "the margin leaves nothing of the sheet");
    // By columns and rows: the frame size follows (spacing between frames, none after the last).
    if (s.columns > 0 && s.rows > 0) {
        s.frame_width = (inner_w - (s.columns - 1) * s.spacing) / s.columns;
        s.frame_height = (inner_h - (s.rows - 1) * s.spacing) / s.rows;
    } else if (s.frame_width > 0 && s.frame_height > 0) {  // by frame size: as many as fit
        s.columns = (inner_w + s.spacing) / (s.frame_width + s.spacing);
        s.rows = (inner_h + s.spacing) / (s.frame_height + s.spacing);
    } else {
        s.columns = s.rows = 1;
        s.frame_width = inner_w;
        s.frame_height = inner_h;
    }
    if (s.columns <= 0 || s.rows <= 0 || s.frame_width <= 0 || s.frame_height <= 0)
        return fail(ErrorCode::InvalidArgument, std::format("the grid doesn't fit in a {}x{} sheet", width, height));
    s.frames.clear();
    s.frames.reserve(static_cast<std::size_t>(s.columns) * static_cast<std::size_t>(s.rows));
    const double w = width, h = height;
    for (int r = 0; r < s.rows; ++r)
        for (int c = 0; c < s.columns; ++c) {
            SpriteFrame f;
            f.x = s.margin + c * (s.frame_width + s.spacing);
            f.y = s.margin + r * (s.frame_height + s.spacing);
            f.width = s.frame_width;
            f.height = s.frame_height;
            f.uv_offset = {static_cast<float>(f.x / w), static_cast<float>(f.y / h)};
            f.uv_scale = {static_cast<float>(f.width / w), static_cast<float>(f.height / h)};
            s.frames.push_back(f);
        }
    return {};
}

Json sprite_file_json(const SpriteSheet& s) {
    Json animations = Json::object();
    for (const auto& [name, a] : s.animations) animations[name] = Json{{"frames", a.frames}, {"fps", a.fps}, {"loop", a.loop}};
    Json grid = Json::object();
    if (s.columns > 0 && s.rows > 0) grid = Json{{"columns", s.columns}, {"rows", s.rows}};
    else grid = Json{{"frame_width", s.frame_width}, {"frame_height", s.frame_height}};
    grid["margin"] = s.margin;
    grid["spacing"] = s.spacing;
    return Json{{"noor_sprite", kSpriteFormat}, {"name", s.name}, {"texture", s.texture ? Json(s.texture->to_string()) : Json(nullptr)},
                {"grid", grid}, {"pivot", {s.pivot.x, s.pivot.y}}, {"pixels_per_metre", s.pixels_per_metre},
                {"filter", s.nearest ? "nearest" : "linear"}, {"animations", animations}};
}

} // namespace noor::asset
