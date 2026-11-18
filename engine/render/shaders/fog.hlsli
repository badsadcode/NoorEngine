// Height fog (Milestone 8A, part 3, ADR-033), shared by mesh.hlsl and sky.hlsl. A port of
// engine/render/include/noor/render/fog.h, which is the reference (unit-tested): keep them in step.
//
// Full density below `height`, thinning exponentially above it; the optical depth along a ray in closed
// form; lit by the sun (Henyey-Greenstein) and the sky's average radiance, constant along the ray.

#ifndef NOOR_FOG_HLSLI
#define NOOR_FOG_HLSLI

struct Fog
{
    float density;     // extinction per metre at and below `height` (0: no fog)
    float falloff;     // per metre above `height`
    float height;      // metres
    float anisotropy;  // Henyey-Greenstein g
    float3 albedo;
};

float fog_density_at(Fog f, float h) { return f.density * exp(-f.falloff * max(0.0, h - f.height)); }

float fog_depth_above(Fog f, float h0, float dy, float len)
{
    const float start = fog_density_at(f, h0);
    const float k = f.falloff * dy;
    if (abs(k * len) < 1e-4) return start * len;
    return start * (1.0 - exp(-k * len)) / k;
}

float fog_optical_depth(Fog f, float3 origin, float3 dir, float distance)
{
    if (f.density <= 0.0 || distance <= 0.0) return 0.0;
    const float h0 = origin.y, dy = dir.y;
    const float cross = abs(dy) > 1e-7 ? (f.height - h0) / dy : -1.0;
    if (cross <= 0.0 || cross >= distance)
        return (h0 <= f.height && (h0 < f.height || dy <= 0.0)) ? f.density * distance : fog_depth_above(f, h0, dy, distance);
    if (h0 < f.height) return f.density * cross + fog_depth_above(f, f.height, dy, distance - cross);
    return fog_depth_above(f, h0, dy, cross) + f.density * (distance - cross);
}

float fog_optical_depth_to_infinity(Fog f, float3 origin, float3 dir)
{
    if (f.density <= 0.0) return 0.0;
    if (dir.y > 1e-6 && f.falloff > 0.0)
    {
        const float below = origin.y < f.height ? (f.height - origin.y) / dir.y : 0.0;
        return f.density * below + fog_density_at(f, max(origin.y, f.height)) / (f.falloff * dir.y);
    }
    return fog_optical_depth(f, origin, dir, 100000.0);
}

float fog_phase(float cos_angle, float g)
{
    const float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1e-6, 1.0 + g2 - 2.0 * g * cos_angle), 1.5));
}

// The fog between `origin` and a point `distance` metres along unit `dir` (or the sky: `infinity`).
// colour = colour * transmittance + inscatter.
void fog_apply(Fog f, float3 origin, float3 dir, float distance, bool infinity, float3 to_sun, float3 sun_radiance, float3 ambient,
               out float transmittance, out float3 inscatter)
{
    const float tau = infinity ? fog_optical_depth_to_infinity(f, origin, dir) : fog_optical_depth(f, origin, dir, distance);
    transmittance = exp(-tau);
    inscatter = f.albedo * (sun_radiance * fog_phase(dot(dir, to_sun), f.anisotropy) + ambient) * (1.0 - transmittance);
}

#endif
