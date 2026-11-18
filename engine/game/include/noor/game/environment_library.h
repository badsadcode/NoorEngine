#pragma once
// The scene's environment on the GPU (Milestone 5, part 5): processes an HDRI or a procedural sky
// (asset::make_environment, cached) and hands it to the renderer, only when it changes. Shared by
// noor_player (SceneRenderer) and the editor viewport. The atmosphere sky (Milestone 8A) is built on the
// GPU every frame the sun moves (render::Renderer::set_atmosphere); the CPU reference path is kept for
// comparisons (use_cpu_atmosphere).
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

    // Builds the atmosphere sky on the CPU (asset::Atmosphere + make_environment, part 1) instead of the
    // GPU: the reference the GPU path is tested against (noor_player --cpu-sky). About 120 ms per sun move.
    void use_cpu_atmosphere(bool cpu) noexcept { cpu_atmosphere_ = cpu; }

private:
    render::Renderer& renderer_;
    std::filesystem::path cache_dir_;
    std::string key_;  // what is set now
    render::TextureHandle background_;
    bool cpu_atmosphere_ = false;
};

} // namespace noor::game
