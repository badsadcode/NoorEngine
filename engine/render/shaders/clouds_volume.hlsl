// Volumetric cloud layers (Milestone 8A, part 7b, ADR-034), two compute passes:
//   CSNoise  bakes the volume noises (clouds.hlsli cloud_billow and cloud_detail, which tile) into a 3D texture, once.
//   CSMarch  ray-marches every volume layer for each pixel of a half-resolution image: rgb the clouds' radiance
//            (relative to a sun of 1, like the flat layers), a what they let through. The sky pass (sky.hlsl) lays it
//            over the sky where the flat layers would be drawn.
// The march is a port of clouds.h's cloud_volume_march (the tested reference); the only difference is that the noises
// are read from the texture instead of computed (the same functions, baked at 32 texels per first-octave cell).
// Deterministic: each pixel's samples are jittered by a fixed pattern of the pixel alone.

#include "clouds.hlsli"

// ---- CSNoise ----
static const uint kNoiseSize = 128;  // texels across the tile (kCloudNoisePeriod cells)
[[vk::binding(4, 0)]] [[vk::image_format("rgba8")]] RWTexture3D<float4> noise_out;  // CSNoise only

[numthreads(4, 4, 4)]
void CSNoise(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= kNoiseSize)) return;
    const float3 p = (float3(id) + 0.5) / float(kNoiseSize) * float(kCloudNoisePeriod);
    noise_out[id] = float4(cloud_billow(p.x, p.y, p.z), cloud_detail(p.x, p.y, p.z), 0.0, 1.0);
}

// ---- CSMarch ----
// The frame's uniforms (vulkan_impl.h FrameUniforms), up to the clouds: the fields after them aren't read here.
struct FrameUniforms
{
    float4x4 clip_from_world;
    float4 camera_position;
    float4 to_sun;
    float4 sun_radiance;
    float4 sky_radiance;
    float4 ground_radiance;
    float4x4 cascades[4];
    float4 cascade_splits;
    float4 cascade_texel;
    float4 shadow_params;
    float4 camera_forward;
    uint4 light_count;
    float4 lights[64];
    float4x4 world_from_clip;
    float4 environment;
    uint4 background;
    float4 fog;
    float4 fog_albedo;
    float4 aerial;
    float4 cloud_a[2];
    float4 cloud_b[2];
    uint4 cloud_flags;
    float4 cloud_c;
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;
[[vk::binding(1, 0)]] StructuredBuffer<float4> cloud_light;  // [2i] sun, [2i + 1] sky at layer i (relative)
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] Texture3D<float4> noise;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] SamplerState noise_sampler;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> volume_out;

CloudLayer cloud_layer(uint i)
{
    CloudLayer l;
    l.coverage = frame.cloud_a[i].x;
    l.density = frame.cloud_a[i].y;
    l.altitude_km = frame.cloud_a[i].z;
    l.scale_km = frame.cloud_a[i].w;
    l.offset_km = frame.cloud_b[i].xy;
    l.seed = frame.cloud_flags[i];
    l.enabled = frame.cloud_flags[2 + i] != 0;
    l.thickness_km = i == 0 ? frame.cloud_c.x : frame.cloud_c.y;
    l.volume = (i == 0 ? frame.cloud_c.z : frame.cloud_c.w) > 0.5;
    return l;
}

// clouds.h cloud_volume_density, with the noises read from the baked tile.
float volume_density(CloudLayer layer, float3 puff_offset, float3 detail_offset, float x_km, float height_km, float z_km, float footprint_km,
                     bool detail)
{
    if (!layer.enabled || layer.coverage <= 0.0) return 0.0;
    const float thickness = max(0.05, layer.thickness_km);
    const float h = (height_km - layer.altitude_km) / thickness;
    if (h <= 0.0 || h >= 1.0) return 0.0;
    const float cover = cloud_cover_at(layer, x_km, z_km, footprint_km);
    float base = cover * cloud_height_profile(h, cover);
    if (base <= 0.0) return 0.0;
    const float tile = 1.0 / float(kCloudNoisePeriod);
    const float ps = max(0.05, layer.scale_km) * kVolumePuffScale;
    const float keep_puffs = saturate(1.5 - 2.0 * footprint_km / (0.25 * ps));
    if (kVolumePuff > 0.0 && keep_puffs > 0.0)
    {
        const float3 q = float3((x_km - layer.offset_km.x) / ps, height_km / ps, (z_km - layer.offset_km.y) / ps) + puff_offset;
        const float carve = kVolumePuff * keep_puffs * (1.0 - noise.SampleLevel(noise_sampler, q * tile, 0).r);
        base = saturate((base - carve) / max(1e-3, 1.0 - carve));
        if (base <= 0.0) return 0.0;
    }
    const float s = max(0.05, layer.scale_km) * kVolumeDetailScale;
    const float keep = detail ? saturate(1.5 - 2.0 * footprint_km / (0.25 * s)) : 0.0;
    if (keep <= 0.0) return base;
    const float3 q = float3((x_km - layer.offset_km.x) / s, height_km / s, (z_km - layer.offset_km.y) / s) + detail_offset;
    const float erosion = kVolumeErosion * keep * noise.SampleLevel(noise_sampler, q * tile, 0).g;
    return saturate((base - erosion) / (1.0 - erosion));
}

// clouds.h cloud_volume_march.
float3 volume_march(CloudLayer layer, float3 eye_km, float planet_km, float3 view, float3 to_sun, float3 sun, float3 ambient, int steps,
                    int light_steps, float jitter, float pixel_angle, out float transmittance_out)
{
    transmittance_out = 1.0;
    if (!layer.enabled || !layer.volume || layer.coverage <= 0.0 || layer.density <= 0.0) return 0.0;
    const float thickness = max(0.05, layer.thickness_km);
    const float r = planet_km + max(0.0, eye_km.y);
    const float inner = planet_km + layer.altitude_km, outer = inner + thickness;
    if (r >= outer) return 0.0;
    const float disc_outer = r * r * (view.y * view.y - 1.0) + outer * outer;
    if (disc_outer < 0.0) return 0.0;
    float t0 = 0.0, t1 = -r * view.y + sqrt(disc_outer);
    const float disc_inner = r * r * (view.y * view.y - 1.0) + inner * inner;
    if (r < inner)
    {
        if (disc_inner < 0.0) return 0.0;
        t0 = -r * view.y + sqrt(disc_inner);
    }
    else if (view.y < 0.0 && disc_inner >= 0.0)
    {
        t1 = -r * view.y - sqrt(disc_inner);
    }
    if (t1 <= t0 || t1 <= 0.0) return 0.0;
    t1 = min(t1, t0 + kCloudMarchKm);
    const int n = clamp(int(ceil((t1 - t0) / (thickness / 16.0))), min(24, max(1, steps)), max(1, steps));
    const float dt = (t1 - t0) / float(n);
    const float c = dot(view, to_sun);
    const float spread = pixel_angle / max(0.05, abs(view.y) + 0.02);
    const float3 puff_offset = cloud_noise_offset(layer.seed);
    const float3 detail_offset = cloud_noise_offset(layer.seed + 1u);
    float transmittance = 1.0;
    float3 radiance = 0.0;
    for (int i = 0; i < n && transmittance > 0.01; ++i)
    {
        const float t = t0 + (float(i) + jitter) * dt;
        const float x = eye_km.x + view.x * t, z = eye_km.z + view.z * t;
        const float height = sqrt(max(0.0, t * t + r * r + 2.0 * r * t * view.y)) - planet_km;
        const float d = volume_density(layer, puff_offset, detail_offset, x, height, z, t * spread, true);
        if (d <= 0.0) continue;
        const float sigma = cloud_volume_extinction(layer, d) * exp(-t / kCloudHazeKm);
        float tau_sun = 0.0;
        if (to_sun.y > -0.2)
        {
            const float to_top = (layer.altitude_km + thickness - height) / max(0.1, to_sun.y + 0.05);
            const float ds = min(to_top, 3.0 * thickness) / float(max(1, light_steps));
            for (int j = 0; j < light_steps; ++j)
            {
                const float s = (float(j) + 0.5) * ds;
                tau_sun += cloud_volume_extinction(layer, volume_density(layer, puff_offset, detail_offset, x + to_sun.x * s, height + to_sun.y * s,
                                                                         z + to_sun.z * s, t * spread, false)) * ds;
            }
        }
        float direct = 0.0, a = 1.0, b = 1.0, k = 1.0;
        [unroll] for (int o = 0; o < 3; ++o)
        {
            const float phase = 0.7 * cloud_phase(c, kVolumeSilver * k) + 0.3 * cloud_phase(c, -0.3 * k);
            direct += a * phase * exp(-b * tau_sun);
            a *= 0.5;
            b *= 0.5;
            k *= 0.5;
        }
        const float hf = saturate((height - layer.altitude_km) / thickness);
        const float3 light = sun * (direct * 4.0 * 3.14159265 * kVolumeDirect + kVolumeDiffuse * cloud_diffuse_transmittance(tau_sun) + 0.01) +
                             ambient * (kVolumeAmbientBase + (1.0 - kVolumeAmbientBase) * hf);
        const float step_t = exp(-sigma * dt);
        radiance += light * (transmittance * (1.0 - step_t));
        transmittance *= step_t;
    }
    transmittance_out = transmittance;
    return radiance;
}

static const int kMarchSteps = 64;      // at most, along a look near the horizon (fewer straight up)
static const int kMarchLightSteps = 4;  // towards the sun

float3 view_ray(float2 pixel, float2 size)
{
    const float2 ndc = (pixel + 0.5) / size * 2.0 - 1.0;  // the sky pass's full-screen ndc, at this pixel's centre
    const float4 p = mul(frame.world_from_clip, float4(ndc, 1.0, 1.0));
    return normalize(p.xyz / p.w - frame.camera_position.xyz);
}

[numthreads(8, 8, 1)]
void CSMarch(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    volume_out.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    const float2 size = float2(w, h);
    const float3 view = view_ray(float2(id.xy), size);
    const float pixel_angle = length(view_ray(float2(id.xy) + float2(1.0, 0.0), size) - view);  // how much sky one pixel covers
    const float3 eye_km = float3(frame.camera_position.x * 0.001, frame.cloud_b[0].w, frame.camera_position.z * 0.001);
    const float jitter = interleaved_gradient_noise(float2(id.xy));
    // Volume layers, the higher one first (the nearer one is laid over it).
    float3 radiance = 0.0;
    float transmittance = 1.0;
    const uint first = frame.cloud_a[1].z > frame.cloud_a[0].z ? 1u : 0u;
    [loop] for (uint k = 0; k < 2; ++k)
    {
        const uint i = k == 0 ? first : 1u - first;
        const CloudLayer layer = cloud_layer(i);
        if (!layer.volume) continue;
        float t;
        const float3 c = volume_march(layer, eye_km, frame.cloud_b[0].z, view, frame.to_sun.xyz, cloud_light[i * 2].rgb, cloud_light[i * 2 + 1].rgb,
                                      kMarchSteps, kMarchLightSteps, jitter, pixel_angle, t);
        radiance = radiance * t + c;
        transmittance *= t;
    }
    volume_out[id.xy] = float4(radiance, transmittance);
}
