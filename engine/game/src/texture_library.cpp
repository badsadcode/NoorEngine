#include "noor/game/texture_library.h"

#include "noor/foundation/log.h"

#include <format>
#include <vector>

namespace noor::game {
namespace {
std::filesystem::file_time_type time_of(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}
} // namespace

TextureLibrary::TextureLibrary(render::Renderer& renderer, std::filesystem::path cache_dir)
    : renderer_(renderer), cache_dir_(std::move(cache_dir)) {}

TextureLibrary::~TextureLibrary() {
    for (const auto& [path, e] : textures_)
        if (e.handle.valid() && e.handle.id != renderer_.missing_texture().id) renderer_.destroy_texture(e.handle);
}

render::TextureHandle TextureLibrary::upload(const Result<asset::TextureData>& data, const std::string& name,
                                             render::TextureHandle replace) {
    if (!data) {
        NOOR_LOG_WARN("Game", "Texture {} can't be loaded ({}); showing a checker instead", name, data.error().describe());
        return renderer_.missing_texture();
    }
    // A GPU without BC support gets the pixels decoded on the CPU (4x the memory, but it works).
    const asset::TextureData& source = *data;
    asset::TextureData decoded;
    const asset::TextureData* use = &source;
    if (asset::is_block_compressed(source.format) && !renderer_.supports_block_compression()) {
        decoded = asset::decompress_texture(source);
        use = &decoded;
    }
    std::vector<render::TextureMipData> mips;
    for (const auto& m : use->mips) mips.push_back({m.width, m.height, m.bytes});
    const auto format = [&] {
        switch (use->format) {
        case asset::TextureFormat::Rgba16Float: return render::TextureFormat::Rgba16Float;
        case asset::TextureFormat::Rgba8Unorm: return render::TextureFormat::Rgba8Unorm;
        case asset::TextureFormat::Bc7Srgb: return render::TextureFormat::Bc7Srgb;
        case asset::TextureFormat::Bc7Unorm: return render::TextureFormat::Bc7Unorm;
        case asset::TextureFormat::Bc5Unorm: return render::TextureFormat::Bc5Unorm;
        case asset::TextureFormat::Rgba8Srgb: break;
        }
        return render::TextureFormat::Rgba8Srgb;
    }();
    const render::TextureDesc desc{format, mips, name};
    if (replace.valid() && replace.id != renderer_.missing_texture().id) {
        if (auto s = renderer_.update_texture(replace, desc); s) return replace;
        else NOOR_LOG_WARN("Game", "Texture {} can't be updated: {}", name, s.error().describe());
    }
    if (auto h = renderer_.create_texture(desc)) {
        NOOR_LOG_DEBUG("Game", "Texture {}: {}x{} {}, {} mips, {:.1f} MB", name, use->width(), use->height(), asset::format_name(use->format),
                       use->mips.size(), static_cast<double>(use->gpu_bytes()) / (1024.0 * 1024.0));
        return *h;
    } else {
        NOOR_LOG_WARN("Game", "Texture {} can't be uploaded: {}", name, h.error().describe());
    }
    return renderer_.missing_texture();
}

render::TextureHandle TextureLibrary::get(const std::filesystem::path& file) {
    if (file.empty()) return {};
    const std::string key = file.generic_string();
    if (const auto it = textures_.find(key); it != textures_.end()) return it->second.handle;
    Entry e;
    e.file_time = time_of(file);
    e.meta_time = time_of(file.string() + ".noormeta");
    e.handle = upload(asset::load_texture(file, cache_dir_), file.filename().string());
    textures_.emplace(key, e);
    return e.handle;
}

render::TextureHandle TextureLibrary::get_embedded(const std::filesystem::path& model, int image, std::span<const std::uint8_t> bytes,
                                                   asset::TextureKind kind) {
    // Keyed by content too: a re-exported model with a new picture at the same index gets the new one.
    const auto content = std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const std::string key = std::format("{}#{}#{}#{:x}", model.generic_string(), image, asset::kind_name(kind), content);
    if (const auto it = textures_.find(key); it != textures_.end()) return it->second.handle;
    asset::TextureSettings settings;
    settings.kind = kind;
    const std::string name = std::format("{} image {}", model.filename().string(), image);
    Entry e;
    e.handle = upload(asset::load_texture_memory(bytes, settings, name, cache_dir_), name);
    textures_.emplace(key, e);
    return e.handle;
}

std::size_t TextureLibrary::refresh() {
    std::size_t reloaded = 0;
    for (auto& [key, e] : textures_) {
        if (e.file_time == std::filesystem::file_time_type{} && e.meta_time == std::filesystem::file_time_type{}) continue;  // embedded
        const std::filesystem::path file = key;
        const auto ft = time_of(file);
        const auto mt = time_of(file.string() + ".noormeta");
        if (ft == e.file_time && mt == e.meta_time) continue;
        if (ft == std::filesystem::file_time_type{}) continue;  // being replaced right now: try again later
        e.file_time = ft;
        e.meta_time = mt;
        e.handle = upload(asset::load_texture(file, cache_dir_), file.filename().string(), e.handle);
        NOOR_LOG_INFO("Game", "Reloaded texture {}", file.filename().string());
        ++reloaded;
    }
    return reloaded;
}

} // namespace noor::game
