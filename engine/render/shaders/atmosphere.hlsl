// The live sky (Milestone 8A, part 2, ADR-033): compute shaders that build the atmosphere's tables, the
// sky image and the environment's lighting (prefiltered cube, irradiance) on the GPU, so the sun can move.
//
// This is a port of two CPU reference implementations, which stay the truth these are tested against:
//   - asset::Atmosphere (engine/asset/src/atmosphere.cpp): tables and the sky's ray march;
//   - asset::make_environment (engine/asset/src/environment.cpp): the pyramid, GGX prefiltering, SH9.
// Keep them in step: a change to one is a change to the other (tests/golden Atmosphere_* catch drift).
//
// Entry points (one pipeline each, vulkan_atmosphere.cpp): CSTransmittance, CSMultiple (on a settings
// change), CSSky, CSDownsample, CSCube, CSIrradiance (on a sun change), CSAerial (every frame: the camera's
// aerial perspective, Milestone 8A part 3; asset::Atmosphere::aerial_perspective is its reference).

struct Params
{
    float4 rayleigh;  // xyz scattering per km at sea level, w scale height (km)
    float4 mie;       // x scattering, y absorption (per km), z scale height (km), w g
    float4 ozone;     // xyz absorption per km at the peak, w the layer's centre (km)
    float4 planet;    // x planet radius, y top of the atmosphere (radius, km), z ozone half width, w turbidity
    float4 ground;    // xyz albedo, w observer height above the surface (km)
    float4 sun;       // xyz towards the sun (unit)
    uint4 pass;       // x level (mip), y size (texels), z samples, w unused
    float4 extra;     // x roughness, y cube mip 0's pyramid level, z a base sky texel's solid angle
};
[[vk::push_constant]] Params p;

[[vk::binding(0, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> out_table;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> out_image;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2DArray<float4> out_cube;
[[vk::combinedImageSampler]] [[vk::binding(3, 0)]] Texture2D<float4> transmittance_lut;
[[vk::combinedImageSampler]] [[vk::binding(3, 0)]] SamplerState transmittance_sampler;
[[vk::combinedImageSampler]] [[vk::binding(4, 0)]] Texture2D<float4> multiple_lut;
[[vk::combinedImageSampler]] [[vk::binding(4, 0)]] SamplerState multiple_sampler;
[[vk::combinedImageSampler]] [[vk::binding(5, 0)]] Texture2D<float4> sky;
[[vk::combinedImageSampler]] [[vk::binding(5, 0)]] SamplerState sky_sampler;
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> in_level;
[[vk::binding(7, 0)]] RWStructuredBuffer<float4> sh_out;
struct AerialCamera
{
    float4x4 world_from_clip;
    float4 eye;     // xyz: the camera (metres)
    float4 params;  // x: the volume's far end (km of air), y: distance scale, z: the camera's height above the surface (km),
                    // w: the live sky's scale (inscatter is multiplied by it)
};
[[vk::binding(8, 0)]] ConstantBuffer<AerialCamera> aerial_camera;
[[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> out_volume;
// Clouds (part 4): the layers (vulkan_impl.h CloudsGpu), the light at each layer, the sky without them.
struct Clouds
{
    float4 a[2];  // coverage, density, altitude km, scale km
    float4 b[2];  // drift x km, drift z km
    uint4 flags;  // seed 0, seed 1, enabled 0, enabled 1
    float4 moon;  // towards the moon, w its light relative to the sun's (part 5: the moonlit sky)
    float4 scale; // x: everything these passes write is multiplied by it (precision at night; the frame divides it out)
};
[[vk::binding(10, 0)]] ConstantBuffer<Clouds> clouds;
[[vk::binding(11, 0)]] RWStructuredBuffer<float4> cloud_light;  // [2i] sun, [2i + 1] sky, at layer i
[[vk::binding(12, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> out_background;

#include "clouds.hlsli"

static const float kPi = 3.14159265358979;
static const uint kTW = 256, kTH = 64, kMS = 32;  // atmosphere.h's table sizes

float bottom() { return p.planet.x; }
float top() { return p.planet.y; }
float height_range() { return p.planet.y - p.planet.x; }

// ---------------------------------------------------------------- the atmosphere (atmosphere.cpp)

float ray_sphere(float r, float mu, float radius, bool nearest)
{
    const float disc = r * r * (mu * mu - 1.0) + radius * radius;
    if (disc < 0.0) return -1.0;
    const float s = sqrt(disc);
    return nearest ? -r * mu - s : -r * mu + s;
}

bool hits_ground(float r, float mu) { return mu < 0.0 && r * r * (mu * mu - 1.0) + bottom() * bottom() >= 0.0; }

float phase_rayleigh(float c) { return 3.0 / (16.0 * kPi) * (1.0 + c * c); }
float phase_mie(float c, float g)
{
    const float g2 = g * g;
    const float k = 3.0 / (8.0 * kPi) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + c * c) / pow(max(1e-6, 1.0 + g2 - 2.0 * g * c), 1.5);
}

float mu_of(float u)
{
    const float x = 2.0 * u - 1.0;
    return x * abs(x);
}
float u_of(float mu) { return 0.5 * ((mu < 0.0 ? -sqrt(-mu) : sqrt(mu)) + 1.0); }

struct Medium
{
    float3 scattering_rayleigh;
    float3 scattering_mie;
    float3 extinction;
};

Medium medium(float h)
{
    h = max(0.0, h);
    const float rayleigh = exp(-h / p.rayleigh.w);
    const float mie = exp(-h / p.mie.z) * p.planet.w;
    const float ozone = max(0.0, 1.0 - abs(h - p.ozone.w) / p.planet.z);
    Medium m;
    m.scattering_rayleigh = p.rayleigh.xyz * rayleigh;
    m.scattering_mie = p.mie.x * mie;
    m.extinction = m.scattering_rayleigh + (p.mie.x + p.mie.y) * mie + p.ozone.xyz * ozone;
    return m;
}

float3 transmittance_exact(float h, float mu)
{
    const float r = bottom() + clamp(h, 0.0, height_range());
    if (hits_ground(r, mu)) return 0.0;
    const float ray_len = max(0.0, ray_sphere(r, mu, top(), false));
    const int kSteps = 64;
    const float dt = ray_len / kSteps;
    float3 depth = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        const float t = (float(i) + 0.5) * dt;
        const float ri = sqrt(r * r + t * t + 2.0 * r * mu * t);
        depth += medium(ri - bottom()).extinction * dt;
    }
    return exp(-depth);
}

// The tables are read like the CPU reads them: texel i sits at coordinate i (so u * (n - 1)), which on
// the GPU is the texel centre (i + 0.5) / n.
float3 transmittance(float h, float mu)
{
    const float r = bottom() + clamp(h, 0.0, height_range());
    if (hits_ground(r, mu)) return 0.0;
    const float u = saturate(u_of(clamp(mu, -1.0, 1.0))) * (kTW - 1);
    const float v = sqrt(saturate(h / height_range())) * (kTH - 1);
    return transmittance_lut.SampleLevel(transmittance_sampler, float2((u + 0.5) / kTW, (v + 0.5) / kTH), 0).rgb;
}

float3 multiple_scattering(float h, float mu_s)
{
    const float u = saturate(0.5 * (mu_s + 1.0)) * (kMS - 1);
    const float v = saturate(h / height_range()) * (kMS - 1);
    return multiple_lut.SampleLevel(multiple_sampler, float2((u + 0.5) / kMS, (v + 0.5) / kMS), 0).rgb;
}

float3 div_safe(float3 a, float3 b) { return float3(b.x > 1e-12 ? a.x / b.x : 0.0, b.y > 1e-12 ? a.y / b.y : 0.0, b.z > 1e-12 ? a.z / b.z : 0.0); }

[numthreads(8, 8, 1)]
void CSTransmittance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= kTW || id.y >= kTH) return;
    const float fy = float(id.y) / (kTH - 1);
    const float h = fy * fy * height_range();
    const float r = bottom() + h;
    const float mu_horizon = -sqrt(max(0.0, 1.0 - (bottom() / r) * (bottom() / r)));
    const float mu = max(mu_of(float(id.x) / (kTW - 1)), mu_horizon + 1e-5);
    out_table[id.xy] = float4(transmittance_exact(h, mu), 1.0);
}

[numthreads(8, 8, 1)]
void CSMultiple(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= kMS || id.y >= kMS) return;
    const float h = float(id.y) / (kMS - 1) * height_range();
    const float r = bottom() + h;
    const float mu_s = float(id.x) / (kMS - 1) * 2.0 - 1.0;
    const float3 to_sun = float3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), mu_s, 0.0);
    const int kDirs = 64, kSteps = 20;
    float3 l2 = 0.0, fms = 0.0;
    for (int k = 0; k < kDirs; ++k)
    {
        const float yv = 1.0 - 2.0 * (float(k) + 0.5) / kDirs;
        const float rad = sqrt(max(0.0, 1.0 - yv * yv));
        const float phi = float(k) * 2.39996323;
        const float3 d = float3(rad * cos(phi), yv, rad * sin(phi));
        const bool ground = hits_ground(r, d.y);
        const float ray_len = ground ? ray_sphere(r, d.y, bottom(), true) : ray_sphere(r, d.y, top(), false);
        const float dt = max(0.0, ray_len) / kSteps;
        float3 t_acc = 1.0, l = 0.0, f = 0.0;
        for (int i = 0; i < kSteps; ++i)
        {
            const float t = (float(i) + 0.5) * dt;
            const float3 pos = float3(d.x * t, r + d.y * t, d.z * t);
            const float rp = length(pos);
            const float3 up = pos / rp;
            const Medium m = medium(rp - bottom());
            const float3 scattering = m.scattering_rayleigh + m.scattering_mie;
            const float3 t_sun = transmittance(rp - bottom(), dot(up, to_sun));
            const float3 step_t = exp(-m.extinction * dt);
            const float3 integral = div_safe(1.0 - step_t, m.extinction);
            l += t_acc * (scattering * t_sun / (4.0 * kPi)) * integral;
            f += t_acc * scattering * integral;
            t_acc *= step_t;
        }
        if (ground)
        {
            const float3 pos = float3(d.x * ray_len, r + d.y * ray_len, d.z * ray_len);
            const float mu_g = dot(normalize(pos), to_sun);
            l += t_acc * transmittance(0.0, mu_g) * p.ground.xyz * (max(0.0, mu_g) / kPi);
        }
        l2 += l / kDirs;
        fms += f / kDirs;
    }
    out_table[id.xy] = float4(div_safe(l2, max(1e-3, 1.0 - fms)), 1.0);
}

float3 sky_radiance_from(float r, float3 view, float3 to_sun);

float3 sky_radiance(float3 view, float3 to_sun) { return sky_radiance_from(bottom() + clamp(p.ground.w, 0.0, height_range() - 0.01), view, to_sun); }

// The sky seen from radius `r` (the observer's height): atmosphere.cpp's sky_radiance.
float3 sky_radiance_from(float r, float3 view, float3 to_sun)
{
    const float mu = clamp(view.y, -1.0, 1.0);
    const bool ground = hits_ground(r, mu);
    const float kDistantLandKm = 20.0;  // atmosphere.cpp: below the horizon, distant land
    const float ray_len = ground ? max(kDistantLandKm, ray_sphere(r, mu, bottom(), true)) : max(0.0, ray_sphere(r, mu, top(), false));
    const float c = dot(view, to_sun);
    const float pr = phase_rayleigh(c), pm = phase_mie(c, p.mie.w);
    const int kSteps = 40;
    float3 t_acc = 1.0, l = 0.0;
    float t_prev = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        const float a = (float(i) + 1.0) / kSteps;
        const float t_next = ray_len * a * a;
        const float t = 0.5 * (t_prev + t_next), dt = t_next - t_prev;
        t_prev = t_next;
        const float3 pos = float3(view.x * t, r + view.y * t, view.z * t);
        const float rp = length(pos);
        const float3 up = pos / rp;
        const float h = rp - bottom();
        const Medium m = medium(h);
        const float mu_s = dot(up, to_sun);
        const float3 t_sun = transmittance(h, mu_s);
        const float3 single = (m.scattering_rayleigh * pr + m.scattering_mie * pm) * t_sun;
        const float3 multi = (m.scattering_rayleigh + m.scattering_mie) * multiple_scattering(h, mu_s);
        const float3 step_t = exp(-m.extinction * dt);
        l += t_acc * (single + multi) * div_safe(1.0 - step_t, m.extinction);
        t_acc *= step_t;
    }
    if (ground)
    {
        const float3 pos = float3(view.x * ray_len, r + view.y * ray_len, view.z * ray_len);
        const float mu_g = dot(normalize(pos), to_sun);
        l += t_acc * transmittance(0.0, mu_g) * p.ground.xyz * (max(0.0, mu_g) / kPi);
    }
    return l;
}

// ---------------------------------------------------------------- directions (environment.cpp)

float3 equirect_direction(float u, float v)
{
    const float phi = (u - 0.5) * 2.0 * kPi;
    const float theta = v * kPi;
    return float3(sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi));
}

float2 equirect_uv(float3 d)
{
    const float phi = atan2(d.x, -d.z);
    const float theta = acos(clamp(d.y / max(length(d), 1e-12), -1.0, 1.0));
    float u = phi / (2.0 * kPi) + 0.5;
    u -= floor(u);
    return float2(u, theta / kPi);
}

float3 cube_direction(uint face, uint x, uint y, uint size)
{
    const float sc = 2.0 * (float(x) + 0.5) / float(size) - 1.0;
    const float tc = 2.0 * (float(y) + 0.5) / float(size) - 1.0;
    float3 d;
    switch (face)
    {
    case 0: d = float3(1, -tc, -sc); break;
    case 1: d = float3(-1, -tc, sc); break;
    case 2: d = float3(sc, 1, tc); break;
    case 3: d = float3(sc, -1, -tc); break;
    case 4: d = float3(sc, -tc, 1); break;
    default: d = float3(-sc, -tc, -1); break;
    }
    return normalize(d);
}

// ---------------------------------------------------------------- the sky and its pyramid

CloudLayer cloud_layer(uint i)
{
    CloudLayer l;
    l.coverage = clouds.a[i].x;
    l.density = clouds.a[i].y;
    l.altitude_km = clouds.a[i].z;
    l.scale_km = clouds.a[i].w;
    l.offset_km = clouds.b[i].xy;
    l.seed = clouds.flags[i];
    l.enabled = clouds.flags[2 + i] != 0;
    return l;
}

// The light that reaches each cloud layer (part 4): the sun through the air above it, and the sky above it.
[numthreads(2, 1, 1)]
void CSCloudLight(uint3 id : SV_DispatchThreadID)
{
    const float h = clamp(clouds.a[id.x].z, 0.0, height_range() - 0.01);
    const float3 to_sun = normalize(p.sun.xyz);
    cloud_light[id.x * 2] = float4(transmittance(h, to_sun.y) * clouds.scale.x, 0.0);
    cloud_light[id.x * 2 + 1] = float4(sky_radiance_from(bottom() + h, float3(0, 1, 0), to_sun) * clouds.scale.x, 0.0);
}

// The sky image: without clouds as the background (the sky pass draws sharp clouds over it), with them for
// the environment's lighting (the pyramid, the cube, the irradiance), so an overcast sky lights the scene grey.
[numthreads(8, 8, 1)]
void CSSky(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    out_image.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    const float3 d = equirect_direction((float(id.x) + 0.5) / w, (float(id.y) + 0.5) / h);
    const float3 to_sun = normalize(p.sun.xyz);
    float3 clear = sky_radiance(d, to_sun);
    if (clouds.moon.w > 0.0) clear += sky_radiance(d, normalize(clouds.moon.xyz)) * clouds.moon.w;  // the moonlit sky
    clear *= clouds.scale.x;
    out_background[id.xy] = float4(clear, 1.0);
    float3 color = clear;
    // The higher layer first (it's behind the lower one).
    const uint first = clouds.a[1].z > clouds.a[0].z ? 1u : 0u;
    [unroll] for (uint k = 0; k < 2; ++k)
    {
        const uint i = k == 0 ? first : 1u - first;
        float alpha;
        const float3 c = cloud_layer_shade(cloud_layer(i), float3(0.0, p.ground.w, 0.0), bottom(), d, 3.14159265 / float(h), to_sun, cloud_light[i * 2].rgb,
                                           cloud_light[i * 2 + 1].rgb, alpha);
        color = color * (1.0 - alpha) + c * alpha;
    }
    out_image[id.xy] = float4(color, 1.0);
}

// The next pyramid level: each texel the average of four (environment.cpp's Pyramid).
[numthreads(8, 8, 1)]
void CSDownsample(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    out_image.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    const uint2 s = id.xy * 2;
    const float3 sum = in_level[s].rgb + in_level[s + uint2(1, 0)].rgb + in_level[s + uint2(0, 1)].rgb + in_level[s + uint2(1, 1)].rgb;
    out_image[id.xy] = float4(sum * 0.25, 1.0);
}

// ---------------------------------------------------------------- environment lighting (environment.cpp)

float3 sample_sky(float3 d, float lod) { return sky.SampleLevel(sky_sampler, equirect_uv(d), lod).rgb; }

float2 hammersley(uint i, uint n) { return float2(float(i) / float(n), float(reversebits(i)) * 2.3283064365386963e-10); }

float3 prefilter(float3 n, float roughness, uint samples)
{
    const float alpha = roughness * roughness;
    const float3 up = abs(n.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    const float3 tx = normalize(cross(up, n));
    const float3 ty = cross(n, tx);
    float3 sum = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < samples; ++i)
    {
        const float2 xi = hammersley(i, samples);
        const float phi = 2.0 * kPi * xi.x;
        const float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
        const float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
        const float3 hv = tx * (sin_theta * cos(phi)) + ty * (sin_theta * sin(phi)) + n * cos_theta;
        const float3 l = hv * (2.0 * dot(n, hv)) - n;
        const float n_dot_l = dot(n, l);
        if (n_dot_l <= 0.0) continue;
        const float a2 = alpha * alpha;
        const float f = cos_theta * cos_theta * (a2 - 1.0) + 1.0;
        const float pdf = a2 / (kPi * f * f) / 4.0;
        const float sample_solid_angle = 1.0 / (float(samples) * pdf + 1e-6);
        const float lod = 0.5 * log2(sample_solid_angle / p.extra.z) + 1.0;
        sum += sample_sky(l, lod) * n_dot_l;
        weight += n_dot_l;
    }
    return weight > 0.0 ? sum / weight : sample_sky(n, 0.0);
}

// One mip of the prefiltered cube (z: the face). Mip 0 is the sky filtered to the face's texel size.
[numthreads(8, 8, 1)]
void CSCube(uint3 id : SV_DispatchThreadID)
{
    const uint size = p.pass.y;
    if (id.x >= size || id.y >= size) return;
    const float3 d = cube_direction(id.z, id.x, id.y, size);
    const float3 c = p.pass.x == 0 ? sample_sky(d, p.extra.y) : prefilter(d, p.extra.x, p.pass.z);
    out_cube[id] = float4(c, 1.0);
}

// Irradiance as nine spherical-harmonics coefficients, from one pyramid level (pass.x), in one group.
groupshared float3 g_sh[9][256];

[numthreads(256, 1, 1)]
void CSIrradiance(uint3 id : SV_GroupThreadID)
{
    uint w0, h0, levels;
    sky.GetDimensions(0, w0, h0, levels);
    const uint level = p.pass.x;
    const uint w = max(1u, w0 >> level), h = max(1u, h0 >> level);
    float3 acc[9];
    [unroll] for (int k = 0; k < 9; ++k) acc[k] = 0.0;
    for (uint i = id.x; i < w * h; i += 256)
    {
        const uint px = i % w, py = i / w;
        const float v = (float(py) + 0.5) / float(h);
        const float solid_angle = (2.0 * kPi / float(w)) * (kPi / float(h)) * sin(v * kPi);
        const float2 uv = float2((float(px) + 0.5) / float(w), v);
        const float3 d = equirect_direction(uv.x, uv.y);
        const float3 l = sky.SampleLevel(sky_sampler, uv, float(level)).rgb * solid_angle;  // a texel centre: exact
        const float y[9] = {0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x, 1.092548 * d.x * d.y, 1.092548 * d.y * d.z,
                            0.315392 * (3.0 * d.z * d.z - 1.0), 1.092548 * d.x * d.z, 0.546274 * (d.x * d.x - d.y * d.y)};
        [unroll] for (int k = 0; k < 9; ++k) acc[k] += l * y[k];
    }
    [unroll] for (int k = 0; k < 9; ++k) g_sh[k][id.x] = acc[k];
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (id.x < stride)
            [unroll] for (int k = 0; k < 9; ++k) g_sh[k][id.x] += g_sh[k][id.x + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (id.x == 0)
    {
        const float band[9] = {kPi, 2.0 * kPi / 3.0, 2.0 * kPi / 3.0, 2.0 * kPi / 3.0, kPi / 4.0, kPi / 4.0, kPi / 4.0, kPi / 4.0, kPi / 4.0};
        [unroll] for (int k = 0; k < 9; ++k) sh_out[k] = float4(g_sh[k][0] * band[k], 0.0);
    }
}

// ---------------------------------------------------------------- aerial perspective (part 3)

// One thread per froxel column: march out along the pixel's view ray and write each depth slice: rgb the
// light the air adds (relative to a sun of illuminance 1), a the light that gets through (mean of rgb).
// Slice s ends at ((s + 1) / 32)^2 of the far end: thin slices near the camera, where it matters.
static const uint kAerial = 32;

[numthreads(8, 8, 1)]
void CSAerial(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= kAerial || id.y >= kAerial) return;
    const float2 ndc = (float2(id.xy) + 0.5) / kAerial * 2.0 - 1.0;
    const float4 far_point = mul(aerial_camera.world_from_clip, float4(ndc, 1.0, 1.0));
    const float3 view = normalize(far_point.xyz / far_point.w - aerial_camera.eye.xyz);
    const float3 to_sun = normalize(p.sun.xyz);
    const float r = bottom() + clamp(aerial_camera.params.z, 0.0, height_range() - 0.01);
    const float c = dot(view, to_sun);
    const float pr = phase_rayleigh(c), pm = phase_mie(c, p.mie.w);
    float3 t_acc = 1.0, l = 0.0;
    float t_prev = 0.0;
    for (uint s = 0; s < kAerial; ++s)
    {
        const float f = float(s + 1) / kAerial;
        const float slice_end = aerial_camera.params.x * f * f;
        const float slice_start = t_prev;
        for (uint k = 0; k < 2; ++k)  // two steps per slice
        {
            const float t_next = lerp(slice_start, slice_end, float(k + 1) / 2.0);
            const float t = 0.5 * (t_prev + t_next), dt = t_next - t_prev;
            t_prev = t_next;
            const float3 pos = float3(view.x * t, r + view.y * t, view.z * t);
            const float rp = length(pos);
            const float3 up = pos / rp;
            const float h = max(0.0, rp - bottom());  // below the surface counts as the surface
            const Medium m = medium(h);
            const float mu_s = dot(up, to_sun);
            const float3 t_sun = transmittance(h, mu_s);
            const float3 single = (m.scattering_rayleigh * pr + m.scattering_mie * pm) * t_sun;
            const float3 multi = (m.scattering_rayleigh + m.scattering_mie) * multiple_scattering(h, mu_s);
            const float3 step_t = exp(-m.extinction * dt);
            l += t_acc * (single + multi) * div_safe(1.0 - step_t, m.extinction);
            t_acc *= step_t;
        }
        out_volume[uint3(id.xy, s)] = float4(l * aerial_camera.params.w, dot(t_acc, 1.0 / 3.0));
    }
}
