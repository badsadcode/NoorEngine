#pragma once
// Environments (ADR-012 image-based lighting, Milestone 5 part 5): an HDRI (an equirectangular
// .hdr photo of everything around a point) or a procedural sky, turned into what the shader needs:
//
//   - a prefiltered cubemap: mip 0 is the sharp environment, each next mip what a rougher surface
//     reflects (GGX-convolved, roughness = mip / (mips - 1));
//   - irradiance as 9 spherical-harmonics coefficients: the soft light a matte surface gets from
//     every direction, already convolved with the cosine lobe.
//
// All on the CPU, deterministic, and cached in <project>/.noor/cache (like textures). Values stay in
// the HDRI's own units; the scene's `intensity` (nits per unit) and the camera exposure scale them.
#include "noor/foundation/error.h"
#include "noor/foundation/math.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace noor::asset {

inline constexpr int kEnvironmentVersion = 1;

// A float RGB image in equirectangular layout: x = longitude, y = latitude (top row = straight up).
struct EquirectImage {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgb;  // width * height * 3
};

struct EnvironmentData {
    std::uint32_t face_size = 0;  // mip 0 faces are face_size x face_size
    // mips[m][face]: RGBA 16-bit float texels; faces in Vulkan order +X, -X, +Y, -Y, +Z, -Z.
    std::vector<std::array<std::vector<std::uint16_t>, 6>> mips;
    std::array<math::Vec3, 9> irradiance_sh{};  // E(n) = sum of sh[i] * Y_i(n) (band order 00, 1-1, 10, 11, 2-2, 2-1, 20, 21, 22)
    EquirectImage background;                   // the source, for drawing the sky behind everything
};

// The direction an equirect pixel centre (u, v in 0..1) looks at, and back. u = 0.5 looks along −Z
// (the default forward), u grows towards +X; v = 0 is straight up.
[[nodiscard]] math::Vec3 equirect_direction(float u, float v) noexcept;
[[nodiscard]] math::Vec2 equirect_uv(math::Vec3 direction) noexcept;
// The direction of a cubemap texel centre (Vulkan face order and orientation).
[[nodiscard]] math::Vec3 cube_direction(int face, std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept;
// Evaluates irradiance spherical harmonics for a unit normal.
[[nodiscard]] math::Vec3 evaluate_sh(const std::array<math::Vec3, 9>& sh, math::Vec3 n) noexcept;

// Processes an equirect image. `face_size` 0: chosen from the image (width / 4, 32..256).
[[nodiscard]] EnvironmentData make_environment(const EquirectImage& image, std::uint32_t face_size = 0);

// A simple sky without a photo: `zenith` overhead fading to a brighter `horizon`, `ground` below.
[[nodiscard]] EquirectImage procedural_sky(math::Vec3 zenith, math::Vec3 horizon, math::Vec3 ground,
                                           std::uint32_t width = 256, std::uint32_t height = 128);

// Loads an .hdr file and processes it, through the cache (empty `cache_dir`: no cache).
[[nodiscard]] Result<EnvironmentData> load_environment(const std::filesystem::path& hdr_file, const std::filesystem::path& cache_dir);

} // namespace noor::asset
