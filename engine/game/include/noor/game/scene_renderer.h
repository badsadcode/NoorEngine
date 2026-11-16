#pragma once
// Draws a SceneView: loads each model the first time it's seen and keeps the built-in shapes.

#include "noor/game/game.h"
#include "noor/game/geometry.h"
#include "noor/render/renderer.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::game {

class SceneRenderer {
public:
    explicit SceneRenderer(render::Renderer& renderer);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // The draw items for `view`. Models that fail to load are reported once and skipped.
    [[nodiscard]] std::vector<render::DrawItem> draws(const SceneView& view);

    // The game camera, or (with none) one that looks at the origin from above and behind.
    [[nodiscard]] static render::Camera camera(const SceneView& view);

private:
    render::MeshHandle upload(const Geometry& g, const char* name);

    render::Renderer& renderer_;
    render::MeshHandle box_, sphere_;
    std::map<std::pair<int, int>, render::MeshHandle> capsules_;  // by (radius, height) in mm
    std::unordered_map<std::string, std::optional<std::vector<render::MeshHandle>>> models_;  // nullopt: failed
};

} // namespace noor::game
