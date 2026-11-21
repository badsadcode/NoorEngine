#pragma once
// The UI spike (gameplay foundations, part 6; ADR-041): the same pause menu made two ways, drawn into pictures by the
// same CPU rasterizer, timed the same way. Not part of the engine: built only with -DNOOR_UI_SPIKE=ON.

#include "raster.h"

#include "noor/asset/json.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"

#include <functional>
#include <string>

namespace ui_spike {

struct SpikeResult {
    double ms_per_frame = 0;      // laying the menu out and making its triangles, per frame (CPU)
    std::size_t triangles = 0;
    std::function<void(Image&)> draw;
};

SpikeResult own_pause_menu(const std::string& layout_json, int focus, int width, int height);
SpikeResult rml_pause_menu(const std::string& rml_path, const std::string& font_path, int focus, int width, int height);

} // namespace ui_spike
