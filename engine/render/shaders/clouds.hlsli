// Layered clouds (Milestone 8A, part 4, ADR-033), shared by sky.hlsl, mesh.hlsl and atmosphere.hlsl. The noise
// and coverage are a port of engine/render/include/noor/render/clouds.h (the unit-tested reference): keep them
// in step. The shading is an approximation for a thin layer, not a volume: lit by the sun as it reaches the
// layer (sun) and the sky above it (ambient), both relative to a sun of illuminance 1; a look towards the sun
// through the cloud's own cover gives bright thin edges and darker thick bodies.

#ifndef NOOR_CLOUDS_HLSLI
#define NOOR_CLOUDS_HLSLI

struct CloudLayer
{
    float coverage;
    float density;      // optical thickness of a full cloud
    float altitude_km;
    float scale_km;
    float2 offset_km;   // the wind's drift so far
    uint seed;
    bool enabled;
};

uint cloud_hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float cloud_lattice(int ix, int iy, uint seed)
{
    const uint h = cloud_hash(cloud_hash(uint(ix) + seed * 0x9e3779b9u) ^ (uint(iy) * 0x85ebca6bu));
    return float(h >> 8) * (1.0 / 16777216.0);
}

float cloud_value_noise(float x, float y, uint seed)
{
    const float fx = floor(x), fy = floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * tx * (tx * (tx * 6.0 - 15.0) + 10.0);
    const float sy = ty * ty * ty * (ty * (ty * 6.0 - 15.0) + 10.0);
    const float a = cloud_lattice(ix, iy, seed), b = cloud_lattice(ix + 1, iy, seed);
    const float c = cloud_lattice(ix, iy + 1, seed), d = cloud_lattice(ix + 1, iy + 1, seed);
    const float top = a + (b - a) * sx;
    return top + ((c + (d - c) * sx) - top) * sy;
}

float cloud_fbm(float x, float y, uint seed, float footprint)
{
    float sum = 0.0, amplitude = 0.5, total = 0.0, cell = 1.0;
    [unroll] for (uint o = 0; o < 5; ++o)
    {
        const float detail = saturate(1.5 - 2.0 * footprint / cell);
        sum += amplitude * (0.5 + (cloud_value_noise(x, y, seed + o * 1013u) - 0.5) * detail);
        cell *= 0.5;
        total += amplitude;
        const float nx = 1.6 * x + 1.2 * y + 17.3, ny = -1.2 * x + 1.6 * y + 5.1;
        x = nx;
        y = ny;
        amplitude *= 0.5;
    }
    return sum / total;
}

float cloud_cover_at(CloudLayer layer, float x_km, float z_km, float footprint_km)
{
    if (!layer.enabled || layer.coverage <= 0.0) return 0.0;
    const float s = max(0.05, layer.scale_km);
    const float n = cloud_fbm((x_km - layer.offset_km.x) / s, (z_km - layer.offset_km.y) / s, layer.seed, footprint_km / s);
    const float threshold = lerp(1.05, -0.25, saturate(layer.coverage));
    return smoothstep(threshold - 0.1, threshold + 0.2, n);
}

// The direct sunlight that reaches a point of the scene (metres) through every layer (clouds.h).
float cloud_sun_transmittance(CloudLayer layers[2], float3 world, float3 to_sun)
{
    if (to_sun.y <= 0.01) return 1.0;
    float tau = 0.0;
    [unroll] for (uint i = 0; i < 2; ++i)
    {
        if (!layers[i].enabled) continue;
        const float rise = layers[i].altitude_km * 1000.0 - world.y;
        if (rise <= 0.0) continue;
        const float3 at = world + to_sun * (rise / to_sun.y);
        tau += cloud_cover_at(layers[i], at.x * 0.001, at.z * 0.001, 0.0) * max(0.0, layers[i].density);
    }
    return exp(-tau);
}

float cloud_phase(float c, float g)
{
    const float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1e-6, 1.0 + g2 - 2.0 * g * c), 1.5));
}

// One layer seen along unit `view` from `eye_km` (x, z in km; height above the ground in km, y), over a
// planet of radius `planet_km`; `pixel_angle` (radians) sets how much detail a pixel can show. Returns the
// cloud's radiance (relative) and its opacity in `alpha`.
float3 cloud_layer_shade(CloudLayer layer, float3 eye_km, float planet_km, float3 view, float pixel_angle, float3 to_sun, float3 sun,
                         float3 ambient, out float alpha)
{
    alpha = 0.0;
    if (!layer.enabled || layer.coverage <= 0.0) return 0.0;
    const float r = planet_km + max(0.0, eye_km.y);
    const float shell = planet_km + layer.altitude_km;
    if (eye_km.y >= layer.altitude_km) return 0.0;  // above the layer: not drawn from above (yet)
    // The far root from inside the shell (always ahead).
    const float disc = r * r * (view.y * view.y - 1.0) + shell * shell;
    if (disc < 0.0) return 0.0;
    const float t = -r * view.y + sqrt(disc);
    if (t <= 0.0) return 0.0;
    const float x = eye_km.x + view.x * t, z = eye_km.z + view.z * t;
    // A pixel's size on the layer: it grows with distance and at grazing angles.
    const float footprint = t * pixel_angle / max(0.05, abs(view.y) + 0.02);
    const float cover = cloud_cover_at(layer, x, z, footprint);
    if (cover <= 0.0) return 0.0;
    const float tau = cover * max(0.0, layer.density);
    // Very far away the layer thins into the haze.
    alpha = (1.0 - exp(-tau)) * exp(-t / 250.0);
    // How much cloud lies between this spot and the sun: a step towards the sun along the layer.
    const float2 towards = normalize(to_sun.xz + float2(1e-4, 0.0)) * layer.scale_km * 0.35 * (1.0 - saturate(to_sun.y));
    const float cover_sun = cloud_cover_at(layer, x + towards.x, z + towards.y, footprint);
    const float self = exp(-cover_sun * layer.density * 0.5);
    const float c = dot(view, to_sun);
    const float phase = (0.7 * cloud_phase(c, 0.6) + 0.3 * cloud_phase(c, -0.3)) * 4.0 * 3.14159265;  // 1 = even
    // Direct light through the thin parts, light that has bounced around inside (it keeps thick clouds from
    // going black), light from the sunlit ground under the base, and the sky above.
    const float3 lit = sun * (phase * 0.25 * self + 0.06 * (1.0 - self) + 0.02);
    return 0.9 * (lit + ambient * (1.0 - 0.3 * cover));
}

#endif
