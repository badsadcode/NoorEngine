#include "noor/game/display.h"

#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"

#include <algorithm>

namespace noor::game {

using asset::Json;

float DisplaySettings::aspect() const noexcept {
    const auto& d = display;
    if (d.stretch != render::DisplayDesc::Stretch::Off && d.base_width > 0 && d.base_height > 0 && d.keep_aspect)
        return static_cast<float>(d.base_width) / static_cast<float>(d.base_height);
    return static_cast<float>(std::max(1u, window_width)) / static_cast<float>(std::max(1u, window_height));
}

DisplaySettings display_settings(const Json& j) {
    DisplaySettings s;
    if (!j.is_object()) return s;
    if (const auto w = j.find("window"); w != j.end() && w->is_object()) {
        if (w->contains("width") && (*w)["width"].is_number_integer()) s.window_width = std::clamp((*w)["width"].get<std::uint32_t>(), 160u, 16384u);
        if (w->contains("height") && (*w)["height"].is_number_integer()) s.window_height = std::clamp((*w)["height"].get<std::uint32_t>(), 120u, 16384u);
        const std::string mode = w->value("mode", std::string("windowed"));
        s.window_mode = mode == "fullscreen" ? DisplaySettings::WindowMode::Fullscreen
                        : mode == "borderless" ? DisplaySettings::WindowMode::Borderless
                                               : DisplaySettings::WindowMode::Windowed;
    }
    auto& d = s.display;
    if (const auto b = j.find("base_resolution"); b != j.end() && b->is_array() && b->size() == 2 && (*b)[0].is_number_integer() && (*b)[1].is_number_integer()) {
        d.base_width = std::clamp((*b)[0].get<std::uint32_t>(), 1u, 16384u);
        d.base_height = std::clamp((*b)[1].get<std::uint32_t>(), 1u, 16384u);
    }
    const std::string stretch = j.value("stretch", std::string(d.base_width > 0 ? "viewport" : "off"));
    d.stretch = stretch == "viewport" ? render::DisplayDesc::Stretch::Viewport
                : stretch == "canvas"  ? render::DisplayDesc::Stretch::Canvas
                                       : render::DisplayDesc::Stretch::Off;
    d.keep_aspect = j.value("aspect", std::string("keep")) != "expand";
    d.integer_scale = j.value("integer_scale", true);
    if (j.contains("render_scale") && j["render_scale"].is_number()) d.render_scale = std::clamp(j["render_scale"].get<float>(), 0.25f, 2.0f);
    return s;
}

DisplaySettings load_display_settings(const std::filesystem::path& root) {
    const auto file = root / "Settings" / "display.json";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return {};
    auto text = fs::read_file(file, 1u << 20);
    if (!text) return {};
    const Json j = Json::parse(*text, nullptr, false);
    if (j.is_discarded()) {
        NOOR_LOG_WARN("Game", "Settings/display.json isn't valid JSON: the defaults are used");
        return {};
    }
    return display_settings(j);
}

} // namespace noor::game
