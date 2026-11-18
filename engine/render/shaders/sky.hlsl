// The sky (Milestone 5, part 5): the environment's background texture (equirectangular) drawn
// behind everything, as a full-screen triangle at infinite distance. Alpha 0 marks "background" for
// the tonemap pass, like the clear colour.

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
    float4 lights[64];          // GpuLight[16] (not used here)
    float4x4 world_from_clip;
    float4 environment;         // x: intensity * exposure, y: cos(rotation), z: sin(rotation), w: last mip
    uint4 background;           // x: background texture slot, y: 1 = draw the sun's disc
    float4 fog;                 // x density, y falloff, z height, w anisotropy (fog.hlsli)
    float4 fog_albedo;
    float4 aerial;
    float4 cloud_a[2];       // per layer: coverage, density, altitude km, scale km
    float4 cloud_b[2];       // per layer: drift x, z km; [0].z planet radius km, [0].w camera height km
    uint4 cloud_flags;       // seed 0, seed 1, enabled 0, enabled 1
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;
[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;
[[vk::binding(3, 0)]] StructuredBuffer<float4> irradiance_sh;  // the fog's ambient light
[[vk::binding(5, 0)]] StructuredBuffer<float4> cloud_light;    // [2i] sun, [2i + 1] sky at cloud layer i (relative)

#include "clouds.hlsli"
#include "fog.hlsli"

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
    return l;
}

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

VertexOutput VSMain(uint id : SV_VertexID)
{
    VertexOutput output;
    const float2 uv = float2((id << 1) & 2, id & 2);
    output.ndc = uv * 2.0 - 1.0;
    output.position = float4(output.ndc, 0.0, 1.0);  // depth 0: infinitely far (reverse-Z)
    return output;
}

static const float kPi = 3.14159265;

float4 PSMain(VertexOutput input) : SV_Target
{
    // The view ray through this pixel: unproject a point on the near plane.
    const float4 p = mul(frame.world_from_clip, float4(input.ndc, 1.0, 1.0));
    float3 d = normalize(p.xyz / p.w - frame.camera_position.xyz);
    const float3 world_d = d;
    const float pixel_angle = length(fwidth(world_d));  // how much sky one pixel covers (clouds' detail)
    // The environment's turn about +Y.
    const float c = frame.environment.y, s = frame.environment.z;
    d = float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    // Equirect: u = 0.5 looks along -Z, grows towards +X; v = 0 straight up (asset/environment.cpp).
    const float u = atan2(d.x, -d.z) / (2.0 * kPi) + 0.5;
    const float v = acos(clamp(d.y, -1.0, 1.0)) / kPi;
    const float3 sky = textures[frame.background.x].SampleLevel(texture_sampler, float2(u, v), 0).rgb;
    float3 color = sky * frame.environment.x;
    if (frame.background.y != 0)
    {
        // The sun's disc: 0.2665 degrees in radius (6.8e-5 sr), radiance = illuminance / solid angle,
        // darker towards the rim (linear limb darkening, 0.6; its average over the disc is 0.8).
        const float cos_radius = 0.99998918;
        const float cos_sun = dot(world_d, frame.to_sun.xyz);
        if (cos_sun > cos_radius)
        {
            const float r2 = saturate((1.0 - cos_sun) / (1.0 - cos_radius));  // (angle / radius)^2
            const float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - r2));
            color += frame.sun_radiance.rgb * (limb / (0.8 * 6.8e-5));
            color = min(color, 60000.0);  // the HDR target is half floats
        }
    }
    // Cloud layers (part 4), sharp per pixel, the higher one first; over the sun's disc.
    if (frame.cloud_flags.z != 0 || frame.cloud_flags.w != 0)
    {
        const float3 eye_km = float3(frame.camera_position.x * 0.001, frame.cloud_b[0].w, frame.camera_position.z * 0.001);
        const uint first = frame.cloud_a[1].z > frame.cloud_a[0].z ? 1u : 0u;
        [unroll] for (uint k = 0; k < 2; ++k)
        {
            const uint i = k == 0 ? first : 1u - first;
            float alpha;
            const float3 c = cloud_layer_shade(cloud_layer(i), eye_km, frame.cloud_b[0].z, world_d, pixel_angle, frame.to_sun.xyz, cloud_light[i * 2].rgb,
                                               cloud_light[i * 2 + 1].rgb, alpha);
            color = color * (1.0 - alpha) + c * frame.environment.x * alpha;  // relative to the sun's illuminance
        }
    }
    // Height fog in front of the sky (to infinity): on the horizon the sky becomes the fog's colour, which
    // is where distant fogged ground ends up too, so the two meet without a seam.
    if (frame.fog.x > 0.0)
    {
        Fog f;
        f.density = frame.fog.x;
        f.falloff = frame.fog.y;
        f.height = frame.fog.z;
        f.anisotropy = frame.fog.w;
        f.albedo = frame.fog_albedo.rgb;
        const float3 ambient = max(irradiance_sh[0].rgb, 0.0) * (0.282095 / kPi) * frame.environment.x;
        float t;
        float3 s;
        fog_apply(f, frame.camera_position.xyz, world_d, 0.0, true, frame.to_sun.xyz, frame.sun_radiance.rgb, ambient, t, s);
        color = color * t + s;
    }
    return float4(color, 0.0);
}
