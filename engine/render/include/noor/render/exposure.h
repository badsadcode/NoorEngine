#pragma once
// Automatic exposure from the light that falls on the scene (Milestone 8A, part 5): a photographer's incident
// light meter, not a histogram. EV100 = log2(E / 2.5) for illuminance E in lux (the meter constant C = 250 at
// ISO 100): 100 klx of noon gives about 15.3, the "sunny 16" rule. At night a meter would make midnight look
// like a dull afternoon, so below 400 lux the exposure is held back (a higher EV100: up to 2.5 stops, reached
// at about 0.2 lux, moonlight): night stays dark, with the moonlit ground and the sky's glow still apart. Deterministic: the same light
// always gives the same exposure (golden images stay stable).

#include <algorithm>
#include <cmath>

namespace noor::render {

// The night bias in stops added to EV100 (0 in daylight, up to +2.5 at night: less exposure, a darker picture).
[[nodiscard]] inline float night_exposure_bias(float lux) noexcept {
    const float stops_below = std::log10(400.0f / std::max(lux, 1e-6f)) * 0.75f;  // 0 at 400 lux
    return std::clamp(stops_below, 0.0f, 2.5f);
}

[[nodiscard]] inline float auto_ev100(float lux) noexcept {
    const float metered = std::log2(std::max(lux, 1e-6f) / 2.5f);
    return std::clamp(metered + night_exposure_bias(lux), -10.0f, 20.0f);
}

} // namespace noor::render
