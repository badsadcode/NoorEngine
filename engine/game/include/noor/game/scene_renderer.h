#pragma once
// Draws a SceneView: loads each model the first time it's seen and keeps the built-in shapes.

#include "noor/game/game.h"
#include "noor/game/geometry.h"
#include "noor/game/texture_library.h"
#include "noor/render/renderer.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::game {

class SceneRenderer {
public:
    // `texture_cache`: where decoded textures are kept (normally <project>/.noor/cache/textures).
    explicit SceneRenderer(render::Renderer& renderer, std::filesystem::path texture_cache = {});
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // The draw items for `view`. Models that fail to load are reported once and skipped.
    [[nodiscard]] std::vector<render::DrawItem> draws(const SceneView& view);

    // The game camera, or (with none) one that looks at the origin from above and behind.
    [[nodiscard]] static render::Camera camera(const SceneView& view);

    // A primitive's surface, with its textures loaded through `textures`.
    [[nodiscard]] static render::Surface surface(const SceneView::Primitive& p, TextureLibrary& textures);

private:
    render::MeshHandle upload(const Geometry& g, const char* name);

    render::Renderer& renderer_;
    render::MeshHandle box_, sphere_;
    std::map<std::pair<int, int>, render::MeshHandle> capsules_;  // by (radius, height) in mm
    std::unordered_map<std::string, std::optional<std::vector<render::MeshHandle>>> models_;  // nullopt: failed
    TextureLibrary textures_;
};

} // namespace noor::game
