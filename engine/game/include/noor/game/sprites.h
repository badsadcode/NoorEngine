#pragma once
// Sprites (Milestone 8B, part 5, ADR-035): `sprite` components drawn as quads cut from a sprite sheet (.noorsprite,
// asset/sprite_asset.h). A sprite is an ordinary mesh draw: one shared quad, and a cutout material per frame whose
// texture transform picks that frame from the sheet. So sprites are lit, cast and receive sun shadows, write depth and
// are fogged like any other surface, and pixel-art sheets use nearest filtering. Games (SceneRenderer) and the editor's
// viewport both use this.
//
//   "sprite": {"sheet": "<.noorsprite id>", "animation": "walk", "frame": 0 (without an animation), "speed": 1,
//              "time_offset": 0, "billboard": "upright" | "camera" | "none", "flip_x": false, "color": "#FFFFFF",
//              "cast_shadows": true}
//
// Billboards: "upright" turns about the vertical to face the camera (characters, trees: they stay standing); "camera"
// faces it fully (smoke, coins); "none" keeps the entity's own rotation (a sign on a wall). The entity's position is the
// sheet's pivot (the feet, by default); its scale multiplies the frame's size in metres.

#include "noor/asset/material.h"
#include "noor/asset/sprite_asset.h"
#include "noor/game/game.h"
#include "noor/game/sprite_animation.h"
#include "noor/game/texture_library.h"
#include "noor/render/renderer.h"

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace noor::game {

// One sprite to draw: its entity, its sheet's file, its component and where its entity is.
using SpriteInstance = SceneView::Sprite;

class SpriteLibrary {
public:
    SpriteLibrary(render::Renderer& renderer, TextureLibrary& textures, asset::AssetFileResolver resolve);
    ~SpriteLibrary();
    SpriteLibrary(const SpriteLibrary&) = delete;
    SpriteLibrary& operator=(const SpriteLibrary&) = delete;

    void set_resolver(asset::AssetFileResolver resolve) { resolve_ = std::move(resolve); }
    // Sheets whose files changed load again (call a few times a second, not every frame). Returns how many.
    std::size_t refresh();
    // Draw items for `sprites`, facing `eye`, animated at `seconds`. Sheets that fail to load are reported once.
    void add_draws(std::span<const SpriteInstance> sprites, math::Vec3 eye, double seconds, std::vector<render::DrawItem>& out);
    // The frame a sprite shows to a viewer at `eye` at `seconds` (sprite_frame); -1 if its sheet won't load.
    [[nodiscard]] int frame_of(const SpriteInstance& sprite, math::Vec3 eye, double seconds);

private:
    struct Sheet {
        bool ok = false;
        std::filesystem::file_time_type time{};
        asset::SpriteSheet sheet;
        render::TextureHandle texture;
        std::map<std::tuple<int, std::uint32_t, bool>, render::MaterialHandle> materials;  // (frame, tint, flipped) -> material
    };
    Sheet* sheet(const std::filesystem::path& file);
    render::MaterialHandle material(Sheet& s, int frame, math::Vec3 tint, bool flip);

    render::Renderer& renderer_;
    TextureLibrary& textures_;
    asset::AssetFileResolver resolve_;
    render::MeshHandle quad_;
    std::unordered_map<std::string, Sheet> sheets_;
};

} // namespace noor::game
