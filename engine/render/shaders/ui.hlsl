// Game UI (gameplay foundations part 6, ADR-041): what RmlUi draws, as indexed triangles in screen pixels. Its colours
// are CSS's: sRGB, premultiplied by alpha, in the vertices and in its textures (font atlases, images). Here they're
// turned into linear light (the screen's sRGB image encodes them again), still premultiplied, for ONE / 1 - alpha.
// Textures come from the bindless table (slot 0 is white: untextured geometry), sampled clamped at their edges.

[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(4, 1)]] SamplerState clamp_sampler;

struct PushConstants {
    float2 scale;        // 2 / framebuffer size: pixels -> 0..2
    float2 translation;  // pixels, added to every vertex
    uint texture;        // slot in the table
    uint3 unused;
};
[[vk::push_constant]] PushConstants pc;

struct VSIn {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;  // unorm8x4: sRGB, premultiplied
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    output.position = float4((input.position + pc.translation) * pc.scale - 1.0, 0.0, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float3 srgb_to_linear(float3 c) {
    return lerp(pow((c + 0.055) / 1.055, 2.4), c / 12.92, step(c, 0.04045));
}

float4 PSMain(VSOut input) : SV_Target {
    const float4 c = input.color * textures[pc.texture].Sample(clamp_sampler, input.uv);  // premultiplied sRGB
    if (c.a <= 1.0 / 1024.0) return float4(0, 0, 0, 0);
    const float3 straight = saturate(c.rgb / c.a);
    return float4(srgb_to_linear(straight) * c.a, c.a);
}
