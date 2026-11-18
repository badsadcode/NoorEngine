#include "noor/game/model_lod.h"

#include "noor/asset/interchange.h"

#include <cmath>

namespace noor::game {

ModelLods read_model_lods(const std::filesystem::path& model_file) {
    ModelLods out;
    const auto manifest = asset::manifest_path_for_model(model_file);
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) return out;
    auto read = asset::read_manifest(manifest);
    if (!read || read->has_errors()) return out;
    const auto& lods = read->manifest.lods;
    if (lods.size() < 2 || !lods[0].max_distance) return out;  // nothing to switch, or no distances to switch by
    for (const auto& l : lods) {
        if (l.file.empty()) break;
        const auto file = l.level == 0 ? model_file : manifest.parent_path() / std::filesystem::path(l.file);
        if (!std::filesystem::is_regular_file(file, ec)) break;  // a missing level ends the list: never skip one
        out.levels.push_back({file, l.max_distance, l.billboard});
    }
    if (out.levels.size() < 2) out.levels.clear();
    return out;
}

int choose_lod(const ModelLods& lods, float distance, float fov_y, float scale) {
    if (!lods.switches()) return 0;
    // The distances are for a 60-degree view: a narrower one magnifies, so it counts as nearer.
    const float tan30 = 0.57735027f;
    const float view = std::tan(std::clamp(fov_y, 0.01f, 3.0f) * 0.5f) / tan30;
    const double effective = static_cast<double>(distance) * view / std::max(scale, 0.01f);
    for (std::size_t i = 0; i + 1 < lods.levels.size(); ++i) {
        if (!lods.levels[i].max_distance || effective <= *lods.levels[i].max_distance) return static_cast<int>(i);
    }
    return static_cast<int>(lods.levels.size() - 1);
}

} // namespace noor::game
