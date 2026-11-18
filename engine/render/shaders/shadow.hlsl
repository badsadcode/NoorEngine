// Shadow pass (Milestone 5, part 4): draws shadow casters into one sun cascade (depth only). The
// fragment shader exists only to cut out see-through pixels of cutout materials (leaves cast
// leaf-shaped shadows). Same descriptor sets and push constants as mesh.hlsl.

struct PushConstants
{
    uint4 ids;  // x: material slot, y: cascade
    float4 splat_tiling;  // (the scene pass's; unused here)
};
[[vk::push_constant]] PushConstants pc;

struct FrameUniforms
{
    float4x4 clip_from_world;
    float4 camera_position;
    float4 to_sun;
    float4 sun_radiance;
    float4 sky_radiance;
    float4 ground_radiance;
    float4x4 cascades[4];
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;

[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;

struct GpuMaterial
{
    float4 base_color;
    float4 emissive_cutoff;
    float4 params;
    float4 uv;
    uint4 tex0;
    uint4 tex1;
};
[[vk::binding(2, 1)]] StructuredBuffer<GpuMaterial> materials;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
    [[vk::location(5)]] float4 world_row0 : INSTANCE0;  // per instance (ADR-032)
    [[vk::location(6)]] float4 world_row1 : INSTANCE1;
    [[vk::location(7)]] float4 world_row2 : INSTANCE2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    const float4 p = float4(input.position, 1.0);
    const float3 world = float3(dot(input.world_row0, p), dot(input.world_row1, p), dot(input.world_row2, p));
    VertexOutput output;
    output.position = mul(frame.cascades[pc.ids.y], float4(world, 1.0));
    output.uv = input.uv;
    return output;
}

void PSMain(VertexOutput input)
{
    const GpuMaterial mat = materials[pc.ids.x];
    if ((mat.tex1.w & 3u) == 0u) return;  // opaque: depth only (see-through ones cast shadows like cutouts)
    const float2 uv = input.uv * mat.uv.xy + mat.uv.zw;
    const float alpha = mat.base_color.a * textures[mat.tex0.x].Sample(texture_sampler, uv).a;
    if (alpha < mat.emissive_cutoff.w) discard;
}
