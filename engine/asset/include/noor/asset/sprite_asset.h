#pragma once
// Sprite sheets (Milestone 8B, part 5, ADR-035): a `.noorsprite` file says how a texture is cut into frames and which
// frames make which animations, so a `sprite` component can draw a character (or a torch, a coin, a sign) from it.
//
//   {"noor_sprite": 1, "name": "Bearded man", "texture": "<texture id>",
//    "grid": {"columns": 8, "rows": 4} | {"frame_width": 64, "frame_height": 64},
//          optional "margin": 0 (pixels round the sheet), "spacing": 0 (pixels between frames),
//    "pivot": [0.5, 0.0] (where the entity stands in a frame: 0..1 from the left and the bottom; feet = bottom centre),
//    "pixels_per_metre": 64, "filter": "nearest" | "linear",
//    "animations": {"walk": {"frames": [8, 9, 10, 11] | {"from": 8, "to": 11}, "fps": 8, "loop": true}, ...}}
//
// Frames are numbered left to right, top to bottom. Part 6 adds 8-direction layouts.

#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noor::asset {

inline constexpr std::string_view kSpriteExtension = ".noorsprite";
inline constexpr int kSpriteFormat = 1;

struct SpriteFrame {
    int x = 0, y = 0, width = 0, height = 0;  // pixels in the texture (top-left origin)
    math::Vec2 uv_offset{0, 0};                // the frame as a texture transform: uv = uv_offset + uv * uv_scale
    math::Vec2 uv_scale{1, 1};
};

struct SpriteAnimation {
    std::vector<int> frames;  // indices into SpriteSheet::frames
    float fps = 8.0f;
    bool loop = true;
};

struct SpriteSheet {
    std::string name;
    std::optional<Guid> texture;
    int columns = 1, rows = 1;           // after slicing
    int frame_width = 0, frame_height = 0;
    int margin = 0, spacing = 0;
    math::Vec2 pivot{0.5f, 0.0f};
    float pixels_per_metre = 64.0f;
    bool nearest = true;                 // pixel art by default
    std::map<std::string, SpriteAnimation> animations;
    std::vector<SpriteFrame> frames;     // from slice()

    // A frame's size in the world (metres), from its pixels and pixels_per_metre.
    [[nodiscard]] math::Vec2 frame_size_m() const noexcept;
    // Which frame an animation shows `seconds` after it started (looping or holding the last frame); a missing
    // animation shows frame 0. Exact: frame k from k / fps up to (k + 1) / fps.
    [[nodiscard]] int frame_at(std::string_view animation, double seconds) const;
};

// Reads the settings (no slicing yet: that needs the texture's size). Missing fields take the defaults above.
[[nodiscard]] Result<SpriteSheet> parse_sprite_sheet(const Json& file);
[[nodiscard]] Result<SpriteSheet> load_sprite_file(const std::filesystem::path& file);

// Cuts a sheet of `width` x `height` pixels into frames: by columns and rows, or by frame size (as many as fit), inside
// the margin, with the spacing between frames. Fills sheet.frames (and columns, rows, frame size). Frame rectangles are
// whole pixels; their texture transforms are exact divisions of the sheet's size.
[[nodiscard]] Status slice(SpriteSheet& sheet, int width, int height);

// The file for a sheet (defaults written out).
[[nodiscard]] Json sprite_file_json(const SpriteSheet& sheet);

} // namespace noor::asset
