// The Tilemap panel's side of the document (2D foundations, part 5, ADR-040): the project's tilesets, what a tilemap's
// tileset looks like (its sheet's frames, for the palette), and painting: strokes of cells from the viewport's tile
// brush, each stroke one undo step. Painting past the map's edge grows it; growing left or down moves the entity by
// the same amount, so the tiles already there stay exactly where they were.
#include "document.h"

#include "noor/asset/sprite_asset.h"
#include "noor/asset/texture.h"
#include "noor/asset/tileset_asset.h"
#include "noor/game/tilemap.h"

#include <format>

namespace noor::editor {
namespace {
using asset::Json;
namespace stdfs = std::filesystem;
} // namespace

Result<Json> Document::tilemap(const Json& command) {
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
        return fail(ErrorCode::InvalidArgument, "tilemap: the command needs an \"op\"");
    const std::string op = command["op"];

    if (op == "list") {  // every tileset in the project
        Json out = Json::array();
        for (const auto& rec : registry_.assets()) {
            if (rec.importer != "tileset") continue;
            auto ts = asset::load_tileset_file(project_.root() / rec.path);
            out.push_back({{"id", rec.guid.to_string()}, {"path", rec.path}, {"name", ts ? ts->name : std::string()}});
        }
        return Json{{"tilesets", std::move(out)}};
    }
    if (!scene_) return fail(ErrorCode::InvalidArgument, "tilemap: no scene is open");
    if (!command.contains("tilemap") || !command["tilemap"].is_string()) return fail(ErrorCode::InvalidArgument, "tilemap: \"tilemap\" (an entity id) is needed");
    const auto id = Guid::parse(command["tilemap"].get<std::string>());
    const scene::Entity* e = id ? scene_->find(*id) : nullptr;
    if (!e || !e->has("tilemap") || !e->components.at("tilemap").is_object())
        return fail(ErrorCode::NotFound, "tilemap: that entity has no tilemap (or isn't editable here)");
    const Json& component = e->components.at("tilemap");

    if (op == "info") {  // the palette: the tileset's sheet cut into frames, and the map's shape
        Json out{{"layout", component.value("layout", std::string("square"))}, {"solid", Json::array()}, {"one_way", Json::array()}};
        const auto grid = asset::parse_tile_rows(component.contains("rows") ? component["rows"] : Json::array());
        out["width"] = grid ? grid->width : 0;
        out["height"] = grid ? grid->height : 0;
        const auto tsid = asset::guid_field(component, "tileset");
        const auto* tsrec = tsid ? registry_.find(*tsid) : nullptr;
        if (!tsrec) {
            out["error"] = "its tileset isn't in the project";
            return out;
        }
        out["tileset"] = tsrec->path;
        auto ts = asset::load_tileset_file(project_.root() / tsrec->path);
        if (!ts) {
            out["error"] = ts.error().message;
            return out;
        }
        for (int s : ts->solid) out["solid"].push_back(s);
        for (int s : ts->one_way) out["one_way"].push_back(s);
        const auto* sheetrec = ts->sheet ? registry_.find(*ts->sheet) : nullptr;
        if (!sheetrec) {
            out["error"] = "its sprite sheet isn't in the project";
            return out;
        }
        auto sheet = asset::load_sprite_file(project_.root() / sheetrec->path);
        const auto* texrec = sheet && sheet->texture ? registry_.find(*sheet->texture) : nullptr;
        if (!sheet || !texrec) {
            out["error"] = "its sprite sheet or texture can't be read";
            return out;
        }
        auto info = asset::texture_info(project_.root() / texrec->path, asset::TextureSettings{});
        if (!info || !asset::slice(*sheet, static_cast<int>(info->source_width), static_cast<int>(info->source_height))) {
            out["error"] = "its sprite sheet doesn't fit its texture";
            return out;
        }
        out["texture_path"] = texrec->path;
        Json frames = Json::array();
        for (const auto& f : sheet->frames) frames.push_back(Json::array({f.x, f.y, f.width, f.height}));
        out["frames"] = std::move(frames);
        out["pixels_per_metre"] = sheet->pixels_per_metre;
        return out;
    }

    if (op == "paint") {
        // {"phase": "begin" | "paint" | "end", "cells": [[x, y], ...], "tile": n (-1 erases)}: a stroke is one undo step.
        const std::string phase = command.value("phase", std::string("paint"));
        if (phase == "begin") {
            if (auto s = execute({{"op", "begin"}, {"label", command.value("tile", -1) < 0 ? "Erase tiles" : "Paint tiles"}}); !s)
                return std::unexpected(s.error());
        }
        Json out{{"changed", false}};
        std::vector<game::Cell> cells;
        if (command.contains("cells") && command["cells"].is_array())
            for (const auto& c : command["cells"])
                if (c.is_array() && c.size() == 2 && c[0].is_number_integer() && c[1].is_number_integer()) cells.push_back({c[0].get<int>(), c[1].get<int>()});
        if (!cells.empty()) {
            Json updated = component;
            const auto r = game::paint_tiles(updated, cells, command.value("tile", -1));
            if (r.changed) {
                if (auto s = execute({{"op", "set_component"}, {"entity", id->to_string()}, {"type", "tilemap"}, {"value", updated}}); !s)
                    return std::unexpected(s.error());
                out["changed"] = true;
            }
            if (r.shift.x != 0 || r.shift.y != 0) {
                // Grown left or down: move the entity back by that many cells, in its own (turned, scaled) axes.
                const scene::Entity* now = scene_->find(*id);
                const auto st = game::tilemap_settings(updated);
                const math::Vec2 off = game::cell_centre(st.layout, st.cell, -r.shift.x, -r.shift.y);
                scene::Transform t = now->transform;
                const math::Vec3 local = math::rotate(t.rotation, math::Vec3{off.x * t.scale.x, off.y * t.scale.y, 0.0f});
                t.position = t.position + local;
                if (auto s = execute({{"op", "set_transform"}, {"entity", id->to_string()},
                                      {"transform", {{"position", {t.position.x, t.position.y, t.position.z}}}}});
                    !s)
                    return std::unexpected(s.error());
                out["shifted"] = {r.shift.x, r.shift.y};
            }
            const auto grid = asset::parse_tile_rows(updated["rows"]);
            out["width"] = grid ? grid->width : 0;
            out["height"] = grid ? grid->height : 0;
        }
        if (phase == "end") {
            if (auto s = execute({{"op", "end"}}); !s) return std::unexpected(s.error());
        }
        return out;
    }
    return fail(ErrorCode::InvalidArgument, std::format("tilemap: unknown op \"{}\" (list, info, paint)", op));
}

} // namespace noor::editor
