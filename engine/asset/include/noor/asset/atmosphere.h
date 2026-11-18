#pragma once
// A physically based atmosphere (Milestone 8A, part 1, ADR-033): the sky's colour and brightness from
// the sun's direction, and the colour of the sunlight that reaches the ground.
//
// The model is the one most engines use today (Bruneton 2008, Hillaire 2020): a planet with an
// atmosphere of air molecules (Rayleigh scattering: blue skies, red sunsets), aerosols (Mie scattering:
// haze, the glow round the sun) and ozone (absorption: the sky's deep blue at dusk), each thinning with
// height. Two tables are built once per setting:
//   - transmittance: how much light survives a straight path out of the atmosphere, by height and angle;
//   - multiple scattering: light that has bounced more than once, approximated as in Hillaire's paper.
// The sky is then a ray march from the observer, using both tables.
//
// Units: lengths in kilometres (the model's) and metres (the settings' altitude), radiance relative to a
// sun of illuminance 1 at the top of the atmosphere: multiply by the sun's lux to get nits.
//
// This is the CPU reference implementation (part 1). Part 2 moves the same maths to the GPU for a
// moving sun; this one stays as the reference its results are compared against.

#include "noor/asset/environment.h"
#include "noor/foundation/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace noor::asset {

struct AtmosphereSettings {
    // Earth by default (the coefficients of Hillaire 2020 / Bruneton's reference implementation).
    float planet_radius_km = 6360.0f;
    float atmosphere_height_km = 100.0f;
    math::Vec3 rayleigh_scattering{5.802e-3f, 13.558e-3f, 33.1e-3f};  // per km at sea level (R, G, B)
    float rayleigh_height_km = 8.0f;                                  // density falls by e every this many km
    float mie_scattering = 3.996e-3f;                                 // per km at sea level
    float mie_absorption = 4.40e-3f;
    float mie_height_km = 1.2f;
    float mie_g = 0.8f;  // how forward the aerosols scatter (the glow round the sun)
    math::Vec3 ozone_absorption{0.650e-3f, 1.881e-3f, 0.085e-3f};  // per km at the layer's peak
    float ozone_centre_km = 25.0f, ozone_half_width_km = 15.0f;    // a tent-shaped layer
    math::Vec3 ground_albedo{0.3f, 0.3f, 0.3f};                    // what the planet's surface reflects
    // Scene-facing knobs (the environment component's fields):
    float turbidity = 1.0f;     // multiplies the aerosols: 1 a clear day, 3-5 hazy, 10 thick haze
    float altitude_m = 0.0f;    // the observer's height above the planet's surface

    // A text key of every setting (for caches).
    [[nodiscard]] std::string key() const;
};

// The direction towards the sun (unit; +Y up, north along -Z, east along +X) at `hours` of local solar
// time (12 = the sun at its highest), at `latitude_deg` (positive north) on `day_of_year` (1-365;
// 172 is about the June solstice, 80 the March equinox). The usual simple model: the declination from
// the day, the hour angle from the time (15 degrees an hour); no equation of time or refraction, which
// move the sun by less than a degree or about 16 minutes. Below the horizon when y < 0.
[[nodiscard]] math::Vec3 solar_direction(float hours, float latitude_deg, float day_of_year = 172.0f) noexcept;

class Atmosphere {
public:
    explicit Atmosphere(const AtmosphereSettings& settings);

    [[nodiscard]] const AtmosphereSettings& settings() const noexcept { return s_; }

    // Transmittance from a point at `height_km` above the surface, looking along a direction whose cosine
    // with the local up is `cos_zenith`, out of the atmosphere (0 if the ray hits the planet). Exact ray
    // march (not the table): what the tests check the table against.
    [[nodiscard]] math::Vec3 transmittance_exact(float height_km, float cos_zenith) const;
    // The same from the table (bilinear), as the sky uses it.
    [[nodiscard]] math::Vec3 transmittance(float height_km, float cos_zenith) const;

    // The colour and strength of sunlight reaching the observer (the settings' altitude), for a sun in
    // direction `to_sun` (unit, +Y up): 1 = all of it. Black when the sun is below the horizon.
    [[nodiscard]] math::Vec3 sun_transmittance(math::Vec3 to_sun) const;

    // The sky's radiance seen from the observer looking along `view` (unit), for a sun of illuminance 1
    // in direction `to_sun`. Below the horizon: the lit ground (and the air in front of it). The sun's own
    // disc isn't included (the renderer draws it).
    [[nodiscard]] math::Vec3 sky_radiance(math::Vec3 view, math::Vec3 to_sun) const;

    // Aerial perspective (part 3): what the air between the observer (`observer_km` above the surface)
    // and a point `distance_km` along `view` adds (inscatter, relative to a sun of illuminance 1) and lets
    // through (transmittance). Distant hills turn into sky this way. The GPU builds a volume of these for
    // the camera every frame (shaders/atmosphere.hlsl CSAerial: same steps, same maths).
    struct Aerial {
        math::Vec3 inscatter{0, 0, 0};
        math::Vec3 transmittance{1, 1, 1};
    };
    [[nodiscard]] Aerial aerial_perspective(math::Vec3 view, float distance_km, math::Vec3 to_sun, float observer_km) const;

    // The whole sky as an equirect image (asset::equirect_direction's layout), for the environment
    // pipeline: background, reflections and the light from all around.
    [[nodiscard]] EquirectImage sky_image(math::Vec3 to_sun, std::uint32_t width = 384, std::uint32_t height = 192) const;

    // Built once per settings (they're a few megabytes of tables) and shared. Thread-safe.
    [[nodiscard]] static std::shared_ptr<const Atmosphere> shared(const AtmosphereSettings& settings);

private:
    AtmosphereSettings s_;
    float bottom_ = 0, top_ = 0;  // radii (km)
    static constexpr std::uint32_t kTW = 256, kTH = 64;   // transmittance table
    static constexpr std::uint32_t kMS = 32;              // multiple scattering table (kMS x kMS)
    std::vector<math::Vec3> transmittance_;
    std::vector<math::Vec3> multiple_;

    struct Medium {
        math::Vec3 scattering_rayleigh;
        math::Vec3 scattering_mie;
        math::Vec3 extinction;
    };
    [[nodiscard]] Medium medium(float height_km) const;
    [[nodiscard]] math::Vec3 multiple_scattering(float height_km, float cos_sun) const;
};

} // namespace noor::asset
