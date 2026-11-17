#pragma once
// Textures for drawing (ADR-029): loads a texture file once (through the derived-data cache),
// uploads it, and hands out its handle. Shared by noor_player (SceneRenderer) and the editor
// viewport. A file that can't be loaded is reported once and shows the renderer's checker.
#include "noor/render/renderer.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace noor::game {

class TextureLibrary {
public:
    // `cache_dir`: where decoded textures are kept (normally <project>/.noor/cache/textures);
    // empty or unwritable: no cache, every start decodes again.
    TextureLibrary(render::Renderer& renderer, std::filesystem::path cache_dir);
    ~TextureLibrary();
    TextureLibrary(const TextureLibrary&) = delete;
    TextureLibrary& operator=(const TextureLibrary&) = delete;

    // The texture for `file` (absolute); an empty path gives an invalid handle (no texture).
    [[nodiscard]] render::TextureHandle get(const std::filesystem::path& file);

    // Forgets `file`, so the next get() loads it again (it changed on disk).
    void reload(const std::filesystem::path& file);

    [[nodiscard]] std::size_t size() const noexcept { return textures_.size(); }

private:
    render::Renderer& renderer_;
    std::filesystem::path cache_dir_;
    std::unordered_map<std::string, render::TextureHandle> textures_;
};

} // namespace noor::game
