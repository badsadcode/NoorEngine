#pragma once
// Textures for drawing (ADR-029): loads a texture file once (through the derived-data cache),
// uploads it, and hands out its handle. Shared by noor_player (SceneRenderer) and the editor
// viewport. A file that can't be loaded is reported once and shows the renderer's checker.
#include "noor/asset/texture.h"
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

    // An image inside a model file (`image` = its index there): `bytes` encoded PNG/JPEG, decoded as `kind`.
    [[nodiscard]] render::TextureHandle get_embedded(const std::filesystem::path& model, int image,
                                                     std::span<const std::uint8_t> bytes, asset::TextureKind kind);

    // Reloads texture files that changed on disk (content or .noormeta), keeping their handles.
    // Returns how many were reloaded. Cheap enough for a few times a second.
    std::size_t refresh();

    [[nodiscard]] std::size_t size() const noexcept { return textures_.size(); }

private:
    render::Renderer& renderer_;
    std::filesystem::path cache_dir_;
    struct Entry {
        render::TextureHandle handle;
        std::filesystem::file_time_type file_time{}, meta_time{};  // for refresh(); files only
    };
    std::unordered_map<std::string, Entry> textures_;
    render::TextureHandle upload(const Result<asset::TextureData>& data, const std::string& name,
                                 render::TextureHandle replace = {});
};

} // namespace noor::game
