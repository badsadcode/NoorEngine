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
    float4 irradiance_sh[9];
    float4 environment;         // x: intensity * exposure, y: cos(rotation), z: sin(rotation), w: last mip
    uint4 background;           // x: background texture slot
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;
[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;

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
    // The environment's turn about +Y.
    const float c = frame.environment.y, s = frame.environment.z;
    d = float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    // Equirect: u = 0.5 looks along -Z, grows towards +X; v = 0 straight up (asset/environment.cpp).
    const float u = atan2(d.x, -d.z) / (2.0 * kPi) + 0.5;
    const float v = acos(clamp(d.y, -1.0, 1.0)) / kPi;
    const float3 sky = textures[frame.background.x].SampleLevel(texture_sampler, float2(u, v), 0).rgb;
    return float4(sky * frame.environment.x, 0.0);
}
