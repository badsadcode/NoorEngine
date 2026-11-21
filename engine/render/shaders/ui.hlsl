// Game UI (gameplay foundations part 6, ADR-041): RmlUi's drawing, effects included (6c). The UI is drawn in its own
// layers (8-bit, holding sRGB values premultiplied by alpha: blended in sRGB like a browser) and the finished one goes
// back onto the screen (PSFinal turns it into linear light for the sRGB swapchain). Textures, layers being read and the
// mask all come from the bindless table (set 1); gradients from a storage buffer (set 0). The maths of gradients,
// filters and blur follows RmlUi's reference renderer (RmlUi_Renderer_GL3.cpp, MIT) so pages look as their authors
// expect.

[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(4, 1)]] SamplerState clamp_sampler;

struct Gradient {
    uint kind;  // 0 linear, 1 radial, 2 conic, 3..5 the repeating ones
    uint stops;
    float2 p;
    float2 v;
    float2 unused;
    float4 positions[4];  // 16 stop positions, four to a vector
    float4 colors[16];
};
[[vk::binding(0, 0)]] StructuredBuffer<Gradient> gradients;

// One push-constant block (128 bytes) for every program; each reads its own part.
struct PushConstants {
    float4 c0, c1, c2, c3;  // geometry: the transform's columns (pixels to pixels); passes: c0 uv offset + scale,
                            // c1 texcoord limits (min xy, max zw), c2 a colour (drop shadow) or a factor (x), c3 texel offset (xy)
    float2 translation;     // geometry: pixels, before the transform
    float2 origin;          // geometry: the UI's top-left on the screen, after the transform
    float2 scale;           // geometry: 2 / target size
    uint texture;           // the texture or layer read
    uint extra;             // geometry: the gradient; passes: a second texture (the mask)
    float4 data[2];         // passes: blur weights (data[0]) or ...
};
[[vk::push_constant]] PushConstants pc;

// The colour matrix doesn't fit beside the rest: passes that need it read it from c0..c3 instead (they have no transform).

// ---- Geometry ----

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
    VSOut o;
    const float2 at = input.position + pc.translation;
    float4 p = pc.c0 * at.x + pc.c1 * at.y + pc.c3;  // z = 0, w = 1
    p.xy += pc.origin * p.w;
    o.position = float4(p.x * pc.scale.x - p.w, p.y * pc.scale.y - p.w, 0.0, p.w);  // Vulkan: y down, like pixels
    o.color = input.color;
    o.uv = input.uv;
    return o;
}

float4 PSDraw(VSOut input) : SV_Target {
    return input.color * textures[pc.texture].Sample(clamp_sampler, input.uv);
}

float stop_position(Gradient g, uint i) { return g.positions[i / 4][i % 4]; }

float4 PSGradient(VSOut input) : SV_Target {
    const Gradient g = gradients[pc.extra];
    float t = 0.0;
    const uint f = g.kind % 3;
    if (f == 0) {
        t = dot(g.v, input.uv - g.p) / dot(g.v, g.v);
    } else if (f == 1) {
        t = length(g.v * (input.uv - g.p));
    } else {
        const float2 d = input.uv - g.p;
        const float2 r = float2(g.v.x * d.x + g.v.y * d.y, -g.v.y * d.x + g.v.x * d.y);  // rotated into the start direction
        t = 0.5 + atan2(-r.x, r.y) / (2.0 * 3.14159265);
    }
    if (g.kind >= 3 && g.stops > 1) {
        const float t0 = stop_position(g, 0), t1 = stop_position(g, g.stops - 1);
        const float span = max(t1 - t0, 1e-6);
        t = t0 + (t - t0 - span * floor((t - t0) / span));
    }
    float4 c = g.colors[0];
    for (uint i = 1; i < g.stops; ++i)
        c = lerp(c, g.colors[i], smoothstep(stop_position(g, i - 1), stop_position(g, i), t));
    return input.color * c;
}

float4 PSClip(VSOut input) : SV_Target { return float4(0, 0, 0, 0); }  // writes only the clip mask

// ---- Passes over a whole layer (a triangle covering the target) ----

struct FullOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

FullOut VSFull(uint id : SV_VertexID) {
    FullOut o;
    const float2 corner = float2((id << 1) & 2, id & 2);  // (0,0) (2,0) (0,2)
    o.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    o.uv = corner * pc.c0.zw + pc.c0.xy;  // uv scale (zw) and offset (xy)
    return o;
}

// The same, uv as it is: for the colour matrix, whose columns fill c0..c3.
FullOut VSFullPlain(uint id : SV_VertexID) {
    FullOut o;
    const float2 corner = float2((id << 1) & 2, id & 2);
    o.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    o.uv = corner;
    return o;
}

float4 sample_layer(float2 uv) { return textures[pc.texture].Sample(clamp_sampler, uv); }
float in_limits(float2 uv) {
    const float2 inside = step(pc.c1.xy, uv) * step(uv, pc.c1.zw);
    return inside.x * inside.y;
}

// A copy, times a factor (opacity: c2.x).
float4 PSPass(FullOut input) : SV_Target { return sample_layer(input.uv) * pc.c2.x; }

// The screen as the UI's backdrop: opaque, whatever the swapchain's alpha says.
float4 PSOpaque(FullOut input) : SV_Target { return float4(sample_layer(input.uv).rgb, 1.0); }

// The colour matrix, on premultiplied colour (no alpha change: the constant column scales by alpha). Its columns come
// in c0..c3 (this pass has no uv offset or scale), so it reads the uv as it is.
float4 PSColorMatrix(FullOut input) : SV_Target {
    const float4 c = sample_layer(input.uv);
    const float3 rgb = (pc.c0 * c.r + pc.c1 * c.g + pc.c2 * c.b + pc.c3 * c.a).rgb;
    return float4(rgb, c.a);
}

// One direction of a 7-tap Gaussian (weights in data[0]: centre, then outwards), inside the limits only.
float4 PSBlur(FullOut input) : SV_Target {
    float4 sum = float4(0, 0, 0, 0);
    [unroll] for (int i = -3; i <= 3; ++i) {
        const float2 uv = input.uv + float(i) * pc.c3.xy;
        sum += sample_layer(uv) * in_limits(uv) * pc.data[0][abs(i)];
    }
    return sum;
}

// The layer's shape (alpha) moved by the shadow's offset (the uv offset) in the shadow's colour (c2).
float4 PSDropShadow(FullOut input) : SV_Target { return sample_layer(input.uv).a * in_limits(input.uv) * pc.c2; }

// The layer times the mask's alpha.
float4 PSMask(FullOut input) : SV_Target {
    return sample_layer(input.uv) * textures[pc.extra].Sample(clamp_sampler, input.uv).a;
}

// The finished UI onto the screen, its sRGB values back into linear light (the swapchain encodes them again). With the
// screen's copy (`extra`, c2.x = 1) it's laid over that in sRGB, as a browser would, and replaces the screen's pixels;
// without one (a swapchain that can't be copied) it's blended over the screen in linear light instead.
float3 srgb_to_linear(float3 c) { return lerp(pow((c + 0.055) / 1.055, 2.4), c / 12.92, step(c, 0.04045)); }
float4 PSFinal(FullOut input) : SV_Target {
    float4 c = sample_layer(input.uv);
    if (pc.c2.x > 0.5) {
        const float3 screen = textures[pc.extra].Sample(clamp_sampler, input.uv).rgb;
        return float4(srgb_to_linear(saturate(c.rgb + screen * (1.0 - c.a))), 1.0);
    }
    if (c.a <= 1.0 / 1024.0) return float4(0, 0, 0, 0);
    return float4(srgb_to_linear(saturate(c.rgb / c.a)) * c.a, c.a);
}
