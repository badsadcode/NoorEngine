// Tonemapping (ADR-012): the HDR image (already exposed) -> display colours, drawn as one
// full-screen triangle into the sRGB screen (the hardware does the sRGB encoding on write).
// Pixels where nothing was drawn (alpha 0) show the background: the sky, tonemapped like
// everything else, or a fixed clear colour shown exactly.

[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D<float4> hdr;
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState hdr_sampler;

struct PushConstants
{
    float4 clear_color;  // linear display colour, used when mode.y == 1
    uint4 mode;          // x: 0 Khronos PBR Neutral, 1 ACES fit, 2 clamp; y: 1 = fixed clear colour background
};
[[vk::push_constant]] PushConstants pc;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint id : SV_VertexID)
{
    // One triangle covering the screen: (-1,-1), (3,-1), (-1,3).
    VertexOutput output;
    const float2 uv = float2((id << 1) & 2, id & 2);
    output.uv = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Khronos PBR Neutral tone mapper (github.com/KhronosGroup/ToneMapping, Apache-2.0): leaves
// colours below ~0.76 nearly untouched, so materials keep the colours they were authored with.
float3 pbr_neutral(float3 color)
{
    const float start_compression = 0.8 - 0.04;
    const float desaturation = 0.15;
    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression) return color;
    const float d = 1.0 - start_compression;
    const float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return lerp(color, new_peak.xxx, g);
}

// The widely used fit of the ACES film curve by Krzysztof Narkowicz (2015).
float3 aces_fit(float3 x)
{
    x *= 0.6;
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 scene = hdr.Sample(hdr_sampler, input.uv);
    if (scene.a == 0.0 && pc.mode.y == 1) return float4(pc.clear_color.rgb, 1.0);
    const float3 c = max(scene.rgb, 0.0);
    float3 mapped;
    if (pc.mode.x == 1) mapped = aces_fit(c);
    else if (pc.mode.x == 2) mapped = saturate(c);
    else mapped = saturate(pbr_neutral(c));
    return float4(mapped, 1.0);
}
