// Milestone 1 mesh shader: per-vertex color with simple directional (Lambert) lighting.
// Conventions (ADR-022): column-major matrices, mul(M, v), linear color output
// (the sRGB swapchain encodes on write).

struct PushConstants
{
    float4x4 clip_from_object;   // projection * view * model
    float4x4 world_from_object;  // model (rotation/translation only for now; normals use its 3x3)
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
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(pc.clip_from_object, float4(input.position, 1.0));
    output.normal = mul((float3x3)pc.world_from_object, input.normal);
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float3 to_light = normalize(float3(0.4, 0.8, 0.45));
    const float diffuse = saturate(dot(normalize(input.normal), to_light));
    const float3 lit = input.color * (0.25 + 0.75 * diffuse);
    return float4(lit, 1.0);
}
