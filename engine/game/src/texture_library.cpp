#include "noor/game/texture_library.h"

#include "noor/asset/texture.h"
#include "noor/foundation/log.h"

#include <vector>

namespace noor::game {

TextureLibrary::TextureLibrary(render::Renderer& renderer, std::filesystem::path cache_dir)
    : renderer_(renderer), cache_dir_(std::move(cache_dir)) {}

TextureLibrary::~TextureLibrary() {
    for (const auto& [path, h] : textures_)
        if (h.valid() && h.id != renderer_.missing_texture().id) renderer_.destroy_texture(h);
}

render::TextureHandle TextureLibrary::get(const std::filesystem::path& file) {
    if (file.empty()) return {};
    const std::string key = file.generic_string();
    if (const auto it = textures_.find(key); it != textures_.end()) return it->second;

    render::TextureHandle handle = renderer_.missing_texture();
    auto data = asset::load_texture(file, cache_dir_);
    if (!data) {
        NOOR_LOG_WARN("Game", "Texture {} can't be loaded ({}); showing a checker instead", file.filename().string(),
                      data.error().describe());
    } else {
        std::vector<render::TextureMipData> mips;
        for (const auto& m : data->mips) mips.push_back({m.width, m.height, m.bytes});
        const auto format = data->format == asset::TextureFormat::Rgba16Float  ? render::TextureFormat::Rgba16Float
                            : data->format == asset::TextureFormat::Rgba8Unorm ? render::TextureFormat::Rgba8Unorm
                                                                              : render::TextureFormat::Rgba8Srgb;
        const std::string name = file.filename().string();
        if (auto h = renderer_.create_texture({format, mips, name})) {
            handle = *h;
            NOOR_LOG_DEBUG("Game", "Texture {}: {}x{}, {} mips, {:.1f} MB", name, data->width(), data->height(), data->mips.size(),
                           static_cast<double>(data->gpu_bytes()) / (1024.0 * 1024.0));
        } else {
            NOOR_LOG_WARN("Game", "Texture {} can't be uploaded: {}", name, h.error().describe());
        }
    }
    textures_.emplace(key, handle);
    return handle;
}

void TextureLibrary::reload(const std::filesystem::path& file) {
    const auto it = textures_.find(file.generic_string());
    if (it == textures_.end()) return;
    if (it->second.valid() && it->second.id != renderer_.missing_texture().id) renderer_.destroy_texture(it->second);
    textures_.erase(it);
}

} // namespace noor::game
