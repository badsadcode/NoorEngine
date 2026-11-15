// Mesh shader: per-vertex color with simple directional (Lambert) lighting and an optional
// per-draw tint (editor selection highlight, gizmos).
// Conventions (ADR-022): column-major matrices, mul(M, v), linear color output
// (the sRGB swapchain encodes on write).

struct PushConstants
{
    float4x4 clip_from_object;  // projection * view * model
    float4 world_col0;          // model 3x3 as columns (normals; rotation/uniform scale for now)
    float4 world_col1;
    float4 world_col2;
    float4 tint;                // rgb linear color, a = how much of it replaces the vertex color
};

[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] nointerpolation float4 tint : TINT;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(pc.clip_from_object, float4(input.position, 1.0));
    output.normal = pc.world_col0.xyz * input.normal.x + pc.world_col1.xyz * input.normal.y + pc.world_col2.xyz * input.normal.z;
    output.color = input.color;
    output.tint = pc.tint;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float3 to_light = normalize(float3(0.4, 0.8, 0.45));
    const float diffuse = saturate(dot(normalize(input.normal), to_light));
    const float3 lit = input.color * (0.25 + 0.75 * diffuse);
    const float3 tinted = input.tint.rgb * (0.55 + 0.45 * diffuse);
    return float4(lerp(lit, tinted, input.tint.a), 1.0);
}
