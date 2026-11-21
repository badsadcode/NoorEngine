#pragma once
// A project's display settings (2D foundations, part 6): Settings/display.json, read by noor_player and built games.
//
//   {"window": {"width": 1280, "height": 720, "mode": "windowed" | "fullscreen" | "borderless"},
//    "base_resolution": [320, 180],          // the resolution the game is designed at (none: the window's own)
//    "stretch": "viewport" | "canvas" | "off",
//    "aspect": "keep" | "expand",            // black bars, or show more of the world
//    "integer_scale": true,                  // viewport: whole-number steps only (each game pixel k x k screen pixels)
//    "render_scale": 1.0}                    // off and canvas: draw the 3D scene smaller (0.25 .. 2) and scale it up
//
// Like Godot's stretch modes ("viewport" there, "canvas_items" here "canvas", "disabled" here "off"). Missing settings
// keep the defaults above (a 1280 x 720 window, the scene drawn at the window's size): every project made before this
// behaves as it did. render::layout_display does the arithmetic.

#include "noor/asset/json.h"
#include "noor/render/renderer.h"

#include <filesystem>

namespace noor::game {

struct DisplaySettings {
    std::uint32_t window_width = 1280, window_height = 720;
    enum class WindowMode : std::uint8_t { Windowed, Fullscreen, Borderless } window_mode = WindowMode::Windowed;
    render::DisplayDesc display;

    // The shape of the picture a camera fills (width / height) when it isn't on screen yet (a camera's follow bounds):
    // the base resolution's if it's kept, else the window's.
    [[nodiscard]] float aspect() const noexcept;
};

[[nodiscard]] DisplaySettings display_settings(const asset::Json& json);
// The project's (or built game's) Settings/display.json; the defaults if it isn't there.
[[nodiscard]] DisplaySettings load_display_settings(const std::filesystem::path& root);

} // namespace noor::game
