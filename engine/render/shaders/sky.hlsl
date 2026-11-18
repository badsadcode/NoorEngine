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
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;
[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;
[[vk::binding(3, 0)]] StructuredBuffer<float4> irradiance_sh;  // the fog's ambient light

#include "fog.hlsli"

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
