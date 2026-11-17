#pragma once
// The scene's environment on the GPU (Milestone 5, part 5): processes an HDRI or a procedural sky
// (asset::make_environment, cached) and hands it to the renderer, only when it changes. Shared by
// noor_player (SceneRenderer) and the editor viewport.
#include "noor/game/game.h"
#include "noor/render/renderer.h"

#include <filesystem>
#include <string>

namespace noor::game {

class EnvironmentLibrary {
public:
    EnvironmentLibrary(render::Renderer& renderer, std::filesystem::path cache_dir);
    ~EnvironmentLibrary();
    EnvironmentLibrary(const EnvironmentLibrary&) = delete;
    EnvironmentLibrary& operator=(const EnvironmentLibrary&) = delete;

    // Makes `environment` the renderer's (cheap when it is the one already set).
    void apply(const SceneView::Environment& environment);

private:
    render::Renderer& renderer_;
    std::filesystem::path cache_dir_;
    std::string key_;  // what is set now
    render::TextureHandle background_;
};

} // namespace noor::game
