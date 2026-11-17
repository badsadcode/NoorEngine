#include "noor/game/environment_library.h"

#include "noor/asset/environment.h"
#include "noor/foundation/log.h"

#include <bit>
#include <format>
#include <vector>

namespace noor::game {
namespace {

std::uint16_t half_of(float value) {
    const std::uint32_t f = std::bit_cast<std::uint32_t>(std::clamp(value, 0.0f, 65000.0f));
    const int exponent = static_cast<int>((f >> 23) & 0xFF) - 127 + 15;
    if (exponent <= 0) return 0;
    std::uint32_t half = (static_cast<std::uint32_t>(exponent) << 10) | ((f & 0x007FFFFFu) >> 13);
    if ((f & 0x1FFFu) >= 0x1000u) ++half;
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(half, 0x7BFFu));
}

std::filesystem::file_time_type time_of(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

} // namespace

EnvironmentLibrary::EnvironmentLibrary(render::Renderer& renderer, std::filesystem::path cache_dir)
    : renderer_(renderer), cache_dir_(std::move(cache_dir)) {}

EnvironmentLibrary::~EnvironmentLibrary() {
    if (background_.valid()) renderer_.destroy_texture(background_);
}

void EnvironmentLibrary::apply(const SceneView::Environment& env) {
    const std::string key = env.hdri.empty()
                                ? std::format("sky {} {} {} {} {} {} {} {} {}", env.zenith.x, env.zenith.y, env.zenith.z, env.horizon.x,
                                              env.horizon.y, env.horizon.z, env.ground.x, env.ground.y, env.ground.z)
                                : std::format("hdri {} {}", env.hdri.generic_string(), time_of(env.hdri).time_since_epoch().count());
    if (key == key_) return;
    key_ = key;

    asset::EnvironmentData data;
    if (env.hdri.empty()) {
        data = asset::make_environment(asset::procedural_sky(env.zenith, env.horizon, env.ground, 128, 64), 32);
    } else if (auto loaded = asset::load_environment(env.hdri, cache_dir_)) {
        data = std::move(*loaded);
    } else {
        NOOR_LOG_WARN("Game", "Environment {} can't be loaded ({}); using a plain sky", env.hdri.filename().string(),
                      loaded.error().describe());
        data = asset::make_environment(asset::procedural_sky({0.4f, 0.4f, 0.45f}, {0.6f, 0.6f, 0.6f}, {0.3f, 0.3f, 0.3f}, 64, 32), 16);
    }

    // The background (equirect, RGBA16F, one mip: the sky shader reads the top level).
    const auto& bg = data.background;
    std::vector<std::uint16_t> texels(static_cast<std::size_t>(bg.width) * bg.height * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(bg.width) * bg.height; ++i) {
        texels[i * 4] = half_of(bg.rgb[i * 3]);
        texels[i * 4 + 1] = half_of(bg.rgb[i * 3 + 1]);
        texels[i * 4 + 2] = half_of(bg.rgb[i * 3 + 2]);
        texels[i * 4 + 3] = half_of(1.0f);
    }
    const render::TextureMipData mip{bg.width, bg.height, {reinterpret_cast<const std::uint8_t*>(texels.data()), texels.size() * 2}};
    auto background = renderer_.create_texture({render::TextureFormat::Rgba16Float, {&mip, 1}, "Environment background"});

    std::vector<std::uint16_t> cube;
    for (const auto& m : data.mips)
        for (const auto& face : m) cube.insert(cube.end(), face.begin(), face.end());
    render::EnvironmentDesc desc;
    desc.face_size = data.face_size;
    desc.mip_count = static_cast<std::uint32_t>(data.mips.size());
    desc.cube = cube;
    desc.irradiance_sh = data.irradiance_sh;
    desc.background = background ? *background : render::TextureHandle{};
    if (auto s = renderer_.set_environment(desc); !s) {
        NOOR_LOG_WARN("Game", "Environment can't be set: {}", s.error().describe());
        if (background) renderer_.destroy_texture(*background);
        return;
    }
    if (background_.valid()) renderer_.destroy_texture(background_);
    background_ = desc.background;
}

} // namespace noor::game
