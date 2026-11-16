// The 2D overlay (HUD), drawn over the 3D scene: solid shapes, and text from a signed-distance-field
// atlas (R8: 0.5 = the glyph's edge, more is inside), sharp at any size.
// Vertices are in pixels with the origin top-left; uv.x < 0 marks a solid (untextured) vertex.

struct PushConstants {
    float2 scale;  // 2 / framebuffer size: pixels -> 0..2
};
[[vk::push_constant]] PushConstants pc;

[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D<float> atlas;
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState atlas_sampler;

struct VSIn {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    output.position = float4(input.position * pc.scale - 1.0, 0.0, 1.0);  // Vulkan: y down, like the pixels
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float coverage_at(float2 uv, float width) {
    return smoothstep(0.5 - width, 0.5 + width, atlas.Sample(atlas_sampler, uv));
}

float4 PSMain(VSOut input) : SV_Target {
    // Derivatives first (uniform control flow), then decide.
    const float2 uv = abs(input.uv);
    const float width = max(fwidth(atlas.Sample(atlas_sampler, uv)) * 0.7, 1e-4);
    // Small text covers several atlas texels per pixel: one sample would skip thin strokes (an "n"
    // losing its arch), so average four spread over the pixel's footprint.
    const float2 dx = ddx(uv) * 0.25, dy = ddy(uv) * 0.25;
    const float coverage = 0.25 * (coverage_at(uv - dx - dy, width) + coverage_at(uv + dx - dy, width) +
                                   coverage_at(uv - dx + dy, width) + coverage_at(uv + dx + dy, width));
    const float alpha = input.uv.x < 0.0 ? 1.0 : coverage;
    return float4(input.color.rgb, input.color.a * alpha);
}
