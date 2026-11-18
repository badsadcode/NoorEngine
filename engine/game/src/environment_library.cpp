#include "noor/game/environment_library.h"

#include "noor/asset/atmosphere.h"
#include "noor/asset/environment.h"
#include "noor/foundation/log.h"

#include <bit>
#include <chrono>
#include <cmath>
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
    if (env.atmosphere && !cpu_atmosphere_) {
        // The live sky: the renderer rebuilds it on the GPU when the sun or the settings change.
        const asset::AtmosphereSettings& a = env.atmosphere_settings;
        render::AtmosphereDesc desc;
        desc.planet_radius_km = a.planet_radius_km;
        desc.atmosphere_height_km = a.atmosphere_height_km;
        desc.rayleigh_scattering = a.rayleigh_scattering;
        desc.rayleigh_height_km = a.rayleigh_height_km;
        desc.mie_scattering = a.mie_scattering;
        desc.mie_absorption = a.mie_absorption;
        desc.mie_height_km = a.mie_height_km;
        desc.mie_g = a.mie_g;
        desc.ozone_absorption = a.ozone_absorption;
        desc.ozone_centre_km = a.ozone_centre_km;
        desc.ozone_half_width_km = a.ozone_half_width_km;
        desc.ground_albedo = a.ground_albedo;
        desc.turbidity = a.turbidity;
        desc.altitude_m = a.altitude_m;
        desc.to_sun = env.to_sun;
        if (auto s = renderer_.set_atmosphere(desc); !s) {
            NOOR_LOG_WARN("Game", "Atmosphere sky can't be set: {}", s.error().describe());
            return;
        }
        if (key_ != "live atmosphere") {
            key_ = "live atmosphere";
            if (background_.valid()) renderer_.destroy_texture(background_);  // the previous fixed sky's
            background_ = {};
        }
        return;
    }
    const std::string key = env.atmosphere ? std::format("atmosphere {} {:.4f} {:.4f} {:.4f}", env.atmosphere_settings.key(), env.to_sun.x,
                                                         env.to_sun.y, env.to_sun.z)
                            : env.hdri.empty()
                                ? std::format("sky {} {} {} {} {} {} {} {} {}", env.zenith.x, env.zenith.y, env.zenith.z, env.horizon.x,
                                              env.horizon.y, env.horizon.z, env.ground.x, env.ground.y, env.ground.z)
                                : std::format("hdri {} {}", env.hdri.generic_string(), time_of(env.hdri).time_since_epoch().count());
    if (key == key_) return;
    key_ = key;

    asset::EnvironmentData data;
    if (env.atmosphere) {
        // Rebuilt when the sun moves (on the CPU for now; Milestone 8A part 2 moves it to the GPU).
        const auto start = std::chrono::steady_clock::now();
        const auto atmosphere = asset::Atmosphere::shared(env.atmosphere_settings);
        const auto built = std::chrono::steady_clock::now();
        data = asset::make_environment(atmosphere->sky_image(env.to_sun, 512, 256), 64);
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        NOOR_LOG_INFO("Game", "Atmosphere sky built in {:.1f} ms (tables {:.1f} ms, sun elevation {:.1f} deg)",
                      ms(start, std::chrono::steady_clock::now()), ms(start, built), std::asin(std::clamp(env.to_sun.y, -1.0f, 1.0f)) * (180.0f / math::kPi));
    } else if (env.hdri.empty()) {
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
