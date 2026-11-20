// What every lit scene shader shares (moved out of mesh.hlsl verbatim in Milestone 8B, part 2, for particle.hlsl):
// the frame's uniforms and set-0 bindings, cloud shadows, the sun's shadow, the environment's diffuse light, and
// the atmosphere and fog between the camera and a point.

#ifndef NOOR_FRAME_HLSLI
#define NOOR_FRAME_HLSLI

// A point or spot light (render::LocalLight), intensity already in candela * exposure.
struct GpuLight
{
    float4 position_range;  // xyz world position, w range (m)
    float4 intensity;       // rgb
    float4 direction_type;  // xyz spot direction (directional: the way its light travels), w: 0 point, 1 spot, 2 directional
    float4 cone;            // x cos(outer), y cos(inner)
};

// Per frame (set 0, binding 0). Radiances are already multiplied by the exposure.
struct FrameUniforms
{
    float4x4 clip_from_world;  // projection * view
    float4 camera_position;    // xyz, world
    float4 to_sun;           // xyz: unit vector towards the sun
    float4 sun_radiance;     // rgb: sun colour * illuminance (lux) * exposure
    float4 sky_radiance;     // rgb: sky colour * luminance (nits) * exposure
    float4 ground_radiance;  // rgb: light bounced up from the ground
    float4x4 cascades[4];    // world -> sun shadow map, per cascade
    float4 cascade_splits;   // far end of each cascade (metres along the view)
    float4 cascade_texel;    // a shadow texel's size in metres, per cascade
    float4 shadow_params;    // x: shadows on, y: shadow map size, z: shadow distance
    float4 camera_forward;
    uint4 light_count;       // x: local lights in use
    GpuLight lights[16];
    float4x4 world_from_clip;
    float4 environment;      // x: intensity * exposure, y: cos(rotation), z: sin(rotation), w: last mip
    uint4 background;
    float4 fog;              // x density, y falloff, z height, w anisotropy (fog.hlsli)
    float4 fog_albedo;       // rgb
    float4 aerial;           // x: 1 = aerial perspective, y: the volume's far end (km), z: distance scale
    float4 cloud_a[2];       // per layer: coverage, density, altitude km, scale km
    float4 cloud_b[2];       // per layer: drift x, z km; [0].z planet radius km, [0].w camera height km
    uint4 cloud_flags;       // seed 0, seed 1, enabled 0, enabled 1
    float4 cloud_c;          // part 7b: thickness km 0, 1; volume 0, 1 (1 = a volume)
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] Texture2DArray<float> shadow_map;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerComparisonState shadow_sampler;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] TextureCube<float4> environment_map;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] SamplerState environment_sampler;
// The environment's diffuse light: spherical harmonics (rgb, 9), written by set_environment or the live sky.
[[vk::binding(3, 0)]] StructuredBuffer<float4> irradiance_sh;
// The air between the camera and each point (the live atmosphere sky; otherwise one texel of "no air").
[[vk::combinedImageSampler]] [[vk::binding(4, 0)]] Texture3D<float4> aerial_volume;
[[vk::combinedImageSampler]] [[vk::binding(4, 0)]] SamplerState aerial_sampler;

#include "clouds.hlsli"
#include "fog.hlsli"

// The sun's light that gets through the clouds to this point: cloud shadows that drift with the wind (part 4).
float cloud_shadow(float3 world)
{
    if (frame.cloud_flags.z == 0 && frame.cloud_flags.w == 0) return 1.0;
    CloudLayer layers[2];
    [unroll] for (uint i = 0; i < 2; ++i)
    {
        layers[i].coverage = frame.cloud_a[i].x;
        layers[i].density = frame.cloud_a[i].y;
        layers[i].altitude_km = frame.cloud_a[i].z;
        layers[i].scale_km = frame.cloud_a[i].w;
        layers[i].offset_km = frame.cloud_b[i].xy;
        layers[i].seed = frame.cloud_flags[i];
        layers[i].enabled = frame.cloud_flags[2 + i] != 0;
        layers[i].volume = false;  // shadows use the column's optical depth, the same for both styles (ADR-034)
        layers[i].thickness_km = 1.0;
    }
    return cloud_sun_transmittance(layers, world, frame.to_sun.xyz);
}

static const float kPi = 3.14159265;

// The direction the camera looks along to see `world` (unit): from the eye, or, for an orthographic camera (camera_forward.w
// = 1, Milestone 9A part 1), the camera's forward everywhere.
float3 view_ray(float3 world)
{
    return frame.camera_forward.w > 0.5 ? frame.camera_forward.xyz : normalize(world - frame.camera_position.xyz);
}

// How much sunlight reaches `world` (1 lit, 0 in shadow): the cascade for this distance, a normal
// offset against acne, 3x3 hardware-filtered PCF (a soft edge about two texels wide), and a fade
// over the last 10% of the shadow distance so shadows don't end in a hard line.
float sun_shadow(float3 world, float3 n)
{
    if (frame.shadow_params.x == 0.0) return 1.0;
    const float view_depth = dot(world - frame.camera_position.xyz, frame.camera_forward.xyz);
    if (view_depth > frame.cascade_splits.w) return 1.0;
    uint c = 0;
    [unroll] for (uint i = 0; i < 3; ++i)
        if (view_depth > frame.cascade_splits[i]) c = i + 1;
    const float3 offset = world + n * frame.cascade_texel[c] * 1.5;
    const float4 s = mul(frame.cascades[c], float4(offset, 1.0));
    const float2 uv = s.xy * 0.5 + 0.5;
    const float texel = 1.0 / frame.shadow_params.y;
    float lit = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            lit += shadow_map.SampleCmpLevelZero(shadow_sampler, float3(uv + float2(x, y) * texel, c), s.z);
    lit /= 9.0;
    const float fade = saturate((view_depth - frame.cascade_splits.w * 0.9) / (frame.cascade_splits.w * 0.1));
    return lerp(lit, 1.0, fade);
}

// Image-based lighting (part 5). Directions are turned by the environment's rotation first.
float3 environment_direction(float3 d)
{
    const float c = frame.environment.y, s = frame.environment.z;
    return float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

// Diffuse: irradiance from the environment's spherical harmonics (already cosine-convolved).
float3 environment_irradiance(float3 n)
{
    const float3 d = environment_direction(n);
    const float y[9] = {0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x, 1.092548 * d.x * d.y, 1.092548 * d.y * d.z,
                        0.315392 * (3.0 * d.z * d.z - 1.0), 1.092548 * d.x * d.z, 0.546274 * (d.x * d.x - d.y * d.y)};
    float3 e = 0.0;
    [unroll] for (int i = 0; i < 9; ++i) e += irradiance_sh[i].rgb * y[i];
    return max(e, 0.0) * frame.environment.x;
}

// The atmosphere and the fog between the camera and `world` (Milestone 8A, part 3): what's behind is
// multiplied by `transmittance`, `inscatter` is added (scaled by coverage for see-through surfaces).
void atmosphere_between(float3 world, out float transmittance, out float3 inscatter)
{
    const float3 to_point = world - frame.camera_position.xyz;
    const float distance = length(to_point);
    const float3 dir = to_point / max(distance, 1e-4);
    transmittance = 1.0;
    inscatter = 0.0;
    if (frame.aerial.x != 0.0)
    {
        // The aerial perspective volume: x, y the point on screen, z the square root of its distance
        // (CSAerial's slices: slice s holds the air up to ((s + 1) / 32)^2 of the far end).
        const float4 clip = mul(frame.clip_from_world, float4(world, 1.0));
        const float2 uv = clip.xy / max(clip.w, 1e-6) * 0.5 + 0.5;
        const float slice = sqrt(distance * frame.aerial.z * 0.001 / frame.aerial.y) * 32.0;
        const float4 air = aerial_volume.SampleLevel(aerial_sampler, float3(uv, (clamp(slice - 1.0, 0.0, 31.0) + 0.5) / 32.0), 0);
        const float near = saturate(slice);  // closer than the first slice: fade from no air
        transmittance = lerp(1.0, air.a, near);
        inscatter = air.rgb * near * frame.environment.x;  // relative to the sun's illuminance (the sky's intensity)
    }
    if (frame.fog.x > 0.0)
    {
        Fog f;
        f.density = frame.fog.x;
        f.falloff = frame.fog.y;
        f.height = frame.fog.z;
        f.anisotropy = frame.fog.w;
        f.albedo = frame.fog_albedo.rgb;
        // The sky's average radiance: the irradiance's constant term (cosine-convolved by pi) over pi.
        const float3 ambient = max(irradiance_sh[0].rgb, 0.0) * (0.282095 / kPi) * frame.environment.x;
        float t;
        float3 s;
        fog_apply(f, frame.camera_position.xyz, dir, distance, false, frame.to_sun.xyz, frame.sun_radiance.rgb, ambient, t, s);
        inscatter = inscatter * t + s;  // the air first (further away), then the fog in front of it
        transmittance *= t;
    }
}

#endif
