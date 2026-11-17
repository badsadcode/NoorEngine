#pragma once
// Image-based lighting maths that runs on the CPU (Milestone 5, part 5).
#include <cstdint>
#include <vector>

namespace noor::render {

// The split-sum environment BRDF lookup table (Karis 2013): for (n·v, roughness) on a size x size
// grid (texel centres), the scale and bias to apply to F0: specular = prefiltered * (F0 * A + B).
// Returns A, B pairs, row by row (rows: roughness 0..1, columns: n·v 0..1). Deterministic.
[[nodiscard]] std::vector<float> brdf_lut(std::uint32_t size = 64, std::uint32_t samples = 256);

} // namespace noor::render
