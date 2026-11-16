#pragma once
// The HUD: turns what scripts drew this frame (runtime::UiCommand) into overlay triangles for the
// renderer, with the built-in font (Atkinson Hyperlegible, SIL OFL 1.1, embedded in the engine).
//
// Text is drawn from signed-distance-field glyphs (stb_truetype), so it stays sharp at any size.
// UI units: the screen is runtime::kUiHeight (720) tall; one unit is height/720 pixels.

#include "noor/render/renderer.h"
#include "noor/runtime/world.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace noor::game {

class Hud {
public:
    // Bakes the font atlas (a few milliseconds). Latin, Latin-1 and Latin Extended-A (Polish, Czech,
    // German, French...), plus € ° – — … ‘ ’ “ ” •. Other characters draw as '?'.
    Hud();

    // The atlas for render::Renderer::set_overlay_atlas (R8, width x height).
    [[nodiscard]] std::uint32_t atlas_width() const noexcept { return atlas_width_; }
    [[nodiscard]] std::uint32_t atlas_height() const noexcept { return atlas_height_; }
    [[nodiscard]] std::span<const std::uint8_t> atlas() const noexcept { return atlas_; }

    // Appends the triangles for `commands` on a framebuffer of width x height pixels.
    void build(std::span<const runtime::UiCommand> commands, float width, float height,
               std::vector<render::OverlayVertex>& out) const;

    // Size of a text block in UI units at `size` (line height): widest line x lines * size.
    [[nodiscard]] math::Vec2 measure(std::string_view utf8, float size) const;

    // Where an element of `extent` (UI units) goes on a screen of `screen` (UI units): its top-left.
    [[nodiscard]] static math::Vec2 place(runtime::UiAnchor anchor, math::Vec2 position, math::Vec2 extent, math::Vec2 screen);

    [[nodiscard]] std::size_t glyph_count() const noexcept { return glyphs_.size(); }
    [[nodiscard]] bool has_glyph(char32_t codepoint) const { return glyphs_.contains(codepoint); }

private:
    struct Glyph {
        float advance = 0;          // in baked pixels
        float x0 = 0, y0 = 0;       // bitmap offset from the pen position on the baseline (baked pixels)
        float w = 0, h = 0;         // bitmap size (baked pixels), 0 for blanks
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        int index = 0;              // glyph index, for kerning
    };
    const Glyph& glyph(char32_t c) const;
    float kern(int a, int b) const;

    std::vector<std::uint8_t> atlas_;
    std::uint32_t atlas_width_ = 0, atlas_height_ = 0;
    std::unordered_map<char32_t, Glyph> glyphs_;
    std::unordered_map<std::uint64_t, float> kerning_;  // (a << 32 | b) -> baked pixels; only non-zero pairs
    float ascent_ = 0, line_height_ = 0;  // baked pixels
};

} // namespace noor::game
