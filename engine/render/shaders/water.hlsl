// Water surfaces (Milestone 8D, ADR-037). Drawn in the see-through pass over a copy of the opaque scene (its colour and
// depth, set 0 bindings 7 and 8), so every pixel knows how much water lies between the surface and what's under it:
//   - light passing through is absorbed per colour (red first, then green: Beer-Lambert, `absorption` per metre),
//     along the view ray under the surface plus the depth the light came down from;
//   - light scattered back from inside the water (`scatter`, lit by the sun and the sky) fills in what's absorbed;
//   - what's under the surface is seen bent (the lookup offset by the surface's slope, `refraction`), unless the
//     offset lands on something in front of the water;
//   - at the shore the water fades in over `shore_fade` metres of depth: no hard line where it meets the ground;
//   - the sky is reflected (the environment's cube, blurred by roughness) by Fresnel, and the sun glints (GGX); part 3:
//     nearby things are reflected by marching the reflected ray through the copied scene (screen-space reflection),
//     the sky standing in where the screen has nothing;
//   - waves (part 2: four Gerstner waves, game/water.h's formula) move the surface in the vertex shader; part 3: each
//     calms where the water is shallow for it (the body's depth map, tanh(k depth)); small ripples (a few sine waves
//     in world space, moving with time) bend the normal on top;
//   - part 3: foam where the water is shallow (round the shore and anything standing in it, surging with the waves)
//     and on crests steep enough to fold (where the waves squeeze the surface together), and wet ground: a flat band
//     just above the water (VSWet/PSWet, drawn before the surface) darkens what's under it near the waterline;
//   - part 4, from under the water: the surface shows the sky (and what's above, traced like a reflection) through a
//     bright window straight overhead (refraction out of the water, Snell's law), and beyond its edge (48.6 degrees)
//     mirrors the water's own glow (total internal reflection). Then two full-screen passes (VSFog/PSFogMul/PSFogAdd)
//     fill the water between the camera and whatever it sees: what gets through multiplied, the light the water
//     scatters added (sunlit and skylit, dimmer with depth, in shafts under the waves, shadowed by what's in the way).
//     Each pixel decides for itself whether its near-plane point is under the surface, so a camera half in sees the
//     waterline cross the screen. Light under the surface reaching objects (dimmed by depth, caustics) is mesh.hlsl's
//     (water_light.hlsli).
// The atmosphere and fog between the camera and the surface apply to the water's own light (reflected, scattered); what's
// seen through already had its own (an approximation: the stretch from the surface down counts as air). A sea is the same
// surface, drawn under the camera out to the horizon. Writes depth and alpha 1.

#include "frame.hlsli"

[[vk::combinedImageSampler]] [[vk::binding(7, 0)]] Texture2D<float4> scene_color;
[[vk::combinedImageSampler]] [[vk::binding(7, 0)]] SamplerState scene_color_sampler;
[[vk::combinedImageSampler]] [[vk::binding(8, 0)]] Texture2D<float> scene_depth;
[[vk::combinedImageSampler]] [[vk::binding(8, 0)]] SamplerState scene_depth_sampler;

// Each draw's settings beyond its push constants (vulkan_water.cpp's WaterGpu), entry grid.w; entry 0 the header.
#include "water_light.hlsli"
// The texture table (set 1): depth maps are R32 float textures in it.
[[vk::binding(0, 1)]] Texture2D<float4> textures[];

struct PushConstants
{
    float4 center_yaw;  // xyz: the surface's middle (y: the level), w: yaw
    float4 size;        // xy: half size (m; the wet band of a sea: metres round the camera), z: ripple strength, w: metres per ripple
    float4 absorption;  // rgb: per metre, w: refraction
    float4 scatter;     // rgb, w: shore fade (m)
    float4 params;      // x: roughness, y: time (s), zw: 1 / screen size
    float4 waves;       // the main wave: x: height (crest to trough, m), y: length (m), z: direction (rad), w: choppiness
    float4 phases;      // the four waves' phases (rad), worked out on the CPU in double precision
    uint4 grid;         // a box: x: cells per side; a sea (z = 1): x: segments round, y: rings out; w: this draw's settings
};
[[vk::push_constant]] PushConstants pc;

static const float kSeaInner = 0.4;  // the sea's first ring (m); the rings then grow by 1 + 2 pi / segments each
static const float kDeep = 1000.0;   // off the depth map: deep water (game/water.h kDeepWater)

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world : POSITION1;
    [[vk::location(1)]] float2 rest : TEXCOORD0;  // the surface point at rest (x, z) that the waves moved here
};

WaterSettings settings() { return water_settings[pc.grid.w]; }

// How deep the water is at world (x, z) over the body's ground, and its slope (d depth / dx, dz): the depth map read
// bilinearly, the same arithmetic as game/water.h's depth_at (exact texel loads, never the sampler's filtering).
float water_depth(float2 p, WaterSettings s, out float2 grad)
{
    grad = 0.0;
    if (s.map_info.w == 0u) return kDeep;
    const float2 u = (p - s.map.xy) / s.map.z;
    const float2 last = float2(s.map_info.xy) - 1.0;
    if (u.x < 0.0 || u.y < 0.0 || u.x > last.x || u.y > last.y) return kDeep;
    const float2 i = min(floor(u), last - 1.0);
    const float2 f = u - i;
    const int2 c = int2(i);
    const Texture2D<float4> map = textures[s.map_info.z];
    const float d00 = map.Load(int3(c, 0)).r, d10 = map.Load(int3(c + int2(1, 0), 0)).r;
    const float d01 = map.Load(int3(c + int2(0, 1), 0)).r, d11 = map.Load(int3(c + int2(1, 1), 0)).r;
    const float a = d00 + (d10 - d00) * f.x, b = d01 + (d11 - d01) * f.x;
    grad = float2((d10 - d00) + ((d11 - d01) - (d10 - d00)) * f.y, b - a) / s.map.z;
    return a + (b - a) * f.y;
}

// How much of a wave is left where the water is `kh` (wavenumber x depth) deep: tanh(kh) (a Pade fit, exactly 1 with
// no slope from 3 on; game/water.h shoal()), and its slope.
float shoal(float kh, out float slope)
{
    slope = 0.0;
    if (kh <= 0.0) return 0.0;
    if (kh >= 3.0) return 1.0;
    const float x2 = kh * kh, num = kh * (27.0 + x2), den = 27.0 + 9.0 * x2;
    slope = ((27.0 + 3.0 * x2) * den - num * 18.0 * kh) / (den * den);
    return num / den;
}

// The four Gerstner waves (game/water.h: the same derivation, keep the tables in step). `cell`: metres between the
// grid's vertices (or a pixel's footprint): a wave shorter than about four of them can't be drawn and fades out.
static const float kWaveLength[4] = {1.0, 0.62, 0.38, 0.24};
static const float kWaveAmplitude[4] = {1.0, 0.5, 0.28, 0.16};
static const float kWaveTurn[4] = {0.0, 0.42, -0.55, 0.95};

// The point at rest `p` moved by the waves (xyz offset); `normal` the surface's normal there; `squeeze` how much the
// waves squeeze the surface together there (the sideways motion's Jacobian: 1 flat, towards 0 on folding crests).
float3 gerstner(float2 p, float cell, WaterSettings s, out float3 normal, out float squeeze)
{
    float3 offset = 0.0;
    float3 tx = float3(1.0, 0.0, 0.0), tz = float3(0.0, 0.0, 1.0);  // the surface's tangents (game/water.h)
    if (pc.waves.x > 0.0)
    {
        float2 dgrad;
        const float depth = water_depth(p, s, dgrad);
        [unroll] for (int i = 0; i < 4; ++i)
        {
            const float angle = pc.waves.z + kWaveTurn[i];
            const float2 d = float2(sin(angle), -cos(angle));
            const float a = 0.5 * pc.waves.x * kWaveAmplitude[i];
            const float k = 6.2831853 / (pc.waves.y * kWaveLength[i]);
            const float q = pc.waves.w / (k * a * 4.0);
            const float fade = saturate(2.0 - cell * k / 1.5707963);  // full at 4 cells a wave, gone at 2
            float slope;
            const float damp = shoal(k * depth, slope);
            const float amp = a * fade * damp;
            const float2 amp_grad = a * fade * slope * k * dgrad;  // where the bottom shelves, the wave's height changes
            const float theta = k * dot(d, p) - pc.phases[i];
            const float c = cos(theta), sn = sin(theta);
            offset += float3(q * amp * d.x * c, amp * sn, q * amp * d.y * c);
            const float sx = amp_grad.x * c - amp * k * d.x * sn, sz = amp_grad.y * c - amp * k * d.y * sn;
            tx += float3(q * d.x * sx, amp_grad.x * sn + amp * k * d.x * c, q * d.y * sx);
            tz += float3(q * d.x * sz, amp_grad.y * sn + amp * k * d.y * c, q * d.y * sz);
        }
    }
    normal = normalize(cross(tz, tx));
    squeeze = tx.x * tz.z - tx.z * tz.x;
    return offset;
}

// The highest the waves lift the water at world (x, z) (their amplitudes, calmed by the depth there).
float wave_reach(float2 p, WaterSettings s)
{
    if (pc.waves.x <= 0.0) return 0.0;
    float2 dgrad;
    const float depth = water_depth(p, s, dgrad);
    float reach = 0.0;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        const float k = 6.2831853 / (pc.waves.y * kWaveLength[i]);
        float slope;
        reach += 0.5 * pc.waves.x * kWaveAmplitude[i] * shoal(k * depth, slope);
    }
    return reach;
}

// A box's footprint: where (x, z) is in its own axes (-1..1 inside).
float2 box_local(float2 xz)
{
    const float2 d = xz - pc.center_yaw.xz;
    const float cy = cos(pc.center_yaw.w), sy = sin(pc.center_yaw.w);
    return float2(cy * d.x - sy * d.y, sy * d.x + cy * d.y) / pc.size.xy;
}

// World position of the opaque scene at a pixel (reverse-Z: depth 0 is infinitely far: the sky).
float3 scene_world(float2 uv, float depth)
{
    const float4 p = mul(frame.world_from_clip, float4(uv * 2.0 - 1.0, depth, 1.0));
    return p.xyz / p.w;
}

// How much of the colour wet ground at `p` keeps (1: dry): wet up to where the waves reach, drying over `wet_height`.
float wet_factor(float3 p, WaterSettings s)
{
    if (p.y <= pc.center_yaw.y) return 1.0 - s.foam.w;  // under the still water: wet
    const float above = p.y - (pc.center_yaw.y + wave_reach(p.xz, s));
    const float wet = 1.0 - saturate(above / max(s.shore.x, 1e-3));
    return 1.0 - s.foam.w * wet;
}

VertexOutput VSMain(uint vertex : SV_VertexID)
{
    const WaterSettings s = settings();
    const uint2 corners[6] = {uint2(0, 0), uint2(1, 0), uint2(1, 1), uint2(0, 0), uint2(1, 1), uint2(0, 1)};
    float2 rest;
    float cell;
    if (pc.grid.z != 0u)
    {
        // The sea: rings round the camera, each a little wider than the last, so a cell covers about the same part of
        // the screen near and far (fine at your feet, kilometres at the horizon).
        const uint segments = max(pc.grid.x, 3u);
        const uint quad = vertex / 6u;
        const uint2 c = uint2(quad % segments, quad / segments) + corners[vertex % 6u];
        const float growth = 1.0 + 6.2831853 / float(segments);
        const float r = c.y == 0u ? 0.0 : kSeaInner * pow(growth, float(c.y - 1u));
        const float angle = 6.2831853 * float(c.x) / float(segments);
        rest = frame.camera_position.xz + r * float2(cos(angle), sin(angle));
        cell = max(r, kSeaInner) * (growth - 1.0);
    }
    else
    {
        // A box: a grid of cells over the footprint (-1..1 on both axes before scaling), turned by its yaw.
        const uint n = max(pc.grid.x, 1u);
        const uint quad = vertex / 6u;
        const uint2 c = uint2(quad % n, quad / n) + corners[vertex % 6u];
        const float2 local = (float2(c) / float(n)) * 2.0 - 1.0;
        const float2 xz = local * pc.size.xy;
        const float cy = cos(pc.center_yaw.w), sy = sin(pc.center_yaw.w);
        rest = pc.center_yaw.xz + float2(cy * xz.x + sy * xz.y, -sy * xz.x + cy * xz.y);
        cell = 2.0 * max(pc.size.x, pc.size.y) / float(n);
    }
    float3 normal;
    float squeeze;
    const float3 world = float3(rest.x, pc.center_yaw.y, rest.y) + gerstner(rest, cell, s, normal, squeeze);
    VertexOutput o;
    o.world = world;
    o.rest = rest;
    o.position = mul(frame.clip_from_world, float4(world, 1.0));
    return o;
}

// Small ripples: the slope (d height / dx, d height / dz) of a few sine waves crossing in different directions.
// `footprint` is how many metres one pixel covers here: a wave shorter than a few pixels would only shimmer (and
// alias into a grid), so it fades out; `lost` returns the share of the slope that went (it roughens the reflection).
float2 ripple_slope(float2 p, float t, float footprint, out float lost)
{
    const float k = 6.2831853 / pc.size.w;
    const float2 dirs[6] = {float2(1.0, 0.0), float2(0.574, 0.819), float2(-0.743, 0.669), float2(0.208, -0.978),
                            float2(-0.966, -0.259), float2(0.848, -0.530)};
    const float scales[6] = {1.0, 1.618, 2.414, 3.732, 5.236, 7.389};
    const float speeds[6] = {0.9, 1.15, 1.4, 1.7, 2.0, 2.3};
    const float offsets[6] = {0.0, 1.7, 4.1, 2.9, 5.3, 0.8};
    float2 slope = 0.0;
    float kept = 0.0, total = 0.0;
    [unroll] for (int i = 0; i < 6; ++i)
    {
        const float f = k * scales[i];
        const float a = 0.05 / sqrt(scales[i]);  // amplitude (of the slope) falls with frequency
        const float fade = saturate(2.0 - footprint * f);  // gone by about a third of a wave per pixel
        const float phase = dot(dirs[i], p) * f + t * speeds[i] * sqrt(f) + offsets[i];
        slope += dirs[i] * cos(phase) * a * fade;
        kept += a * fade;
        total += a;
    }
    lost = 1.0 - kept / total;
    return slope * pc.size.z;
}

// Rain (8D part 5): rings spreading from drops. Two layers of cells (35 and 48 cm), each cell a drop at its own moment of
// a 1.1 s cycle (more cells get one the harder it rains); a ring is a short wave train running out from where it fell,
// fading as it spreads. Returns its slope; rings too small for a pixel fade out.
float2 rain_slope(float2 p, float t, float rain, float footprint)
{
    if (rain <= 0.0) return 0.0;
    float2 slope = 0.0;
    [unroll] for (int layer = 0; layer < 2; ++layer)
    {
        const float cell = layer == 0 ? 0.35 : 0.48;
        const float fade = saturate(1.5 - footprint / (cell * 0.08));
        if (fade <= 0.0) continue;
        const float2 q = p / cell + float2(3.7, 1.3) * float(layer);
        const float2 i = floor(q);
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x)
            {
                const float2 g = i + float2(x, y);
                const float2 h = caustic_hash(g + float2(17.0, 31.0) * float(layer));
                if (h.y > rain) continue;  // no drop in this cell this time
                const float phase = frac(t / 1.1 + h.x * 7.3);
                const float2 d = q - (g + 0.2 + 0.6 * h);
                const float dist = length(d);
                const float r = phase * 0.6;           // the ring's radius, in cells
                const float x0 = (dist - r) / 0.12;
                const float wave = exp(-x0 * x0) * cos(6.2831853 * (dist - r) / 0.15);
                const float strength = (1.0 - phase) * (1.0 - phase) * fade;
                slope += (dist > 1e-4 ? d / dist : 0.0) * wave * strength * 0.35;
            }
    }
    return slope * rain;
}

// Foam's lace: value noise in world space, two sizes drifting different ways, 0..1 (flattened to its average where a
// pixel covers more than its detail).
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float value_noise(float2 p)
{
    const float2 i = floor(p);
    float2 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    const float a = hash12(i), b = hash12(i + float2(1.0, 0.0)), c = hash12(i + float2(0.0, 1.0)), d = hash12(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float foam_lace(float2 p, float t, float footprint)
{
    const float a = value_noise(p * 1.7 + float2(t * 0.13, -t * 0.09));
    const float b = value_noise(p * 4.3 + float2(-t * 0.21, t * 0.17));
    const float c = value_noise(p * 11.1 + float2(t * 0.31, t * 0.23));
    const float lace = a * 0.5 + b * 0.3 + c * 0.2;
    return lerp(lace, 0.5, saturate(footprint * 3.0 - 0.3));
}

float d_ggx(float n_dot_h, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

// Screen-space reflection: the reflected ray marched through the copied scene in growing steps (20, out to about 75 m),
// then narrowed down where it first goes behind something. `confidence` fades to 0 towards the screen's edges and the
// march's end, where the sky takes over.
bool reflected_hit(float3 origin, float3 dir, float2 inv_size, out float2 hit_uv, out float confidence)
{
    hit_uv = 0.0;
    confidence = 0.0;
    const float3 eye = frame.camera_position.xyz;
    // Clip space is linear in the world: a point t along the ray is c0 + cd t (no matrix per step).
    const float4 c0 = mul(frame.clip_from_world, float4(origin, 1.0));
    const float4 cd = mul(frame.clip_from_world, float4(dir, 0.0));
    float t_prev = 0.0, t = 0.25;
    [loop] for (int i = 0; i < 20; ++i)
    {
        const float3 p = origin + dir * t;
        const float4 clip = c0 + cd * t;
        if (clip.w <= 1e-4) return false;
        const float3 ndc = clip.xyz / clip.w;
        const float2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return false;
        const float d = scene_depth.Load(int3(uint2(uv / inv_size), 0));
        if (d > ndc.z)  // reverse-Z: the scene is nearer than the ray here, so the ray has gone behind it
        {
            const float behind = distance(eye, p) - distance(eye, scene_world(uv, d));
            if (behind < max(0.3, 2.0 * (t - t_prev)))
            {
                // Narrow it down between the last step in front and this one.
                float lo = t_prev, hi = t;
                float2 best = uv;
                [unroll] for (int j = 0; j < 5; ++j)
                {
                    const float mid = 0.5 * (lo + hi);
                    const float4 mc = c0 + cd * mid;
                    const float3 mn = mc.xyz / mc.w;
                    const float2 muv = saturate(mn.xy * 0.5 + 0.5);
                    if (scene_depth.Load(int3(uint2(muv / inv_size), 0)) > mn.z)
                    {
                        hi = mid;
                        best = muv;
                    }
                    else
                        lo = mid;
                }
                hit_uv = best;
                const float2 edge = min(best, 1.0 - best);
                confidence = saturate(min(edge.x, edge.y) / 0.08) * saturate((20.0 - float(i)) / 4.0);
                return true;
            }
        }
        t_prev = t;
        t *= 1.33;
    }
    return false;
}

// The surface's height (world y) over (x, z): stepping back from the waves' sideways motion (3 steps: close enough to
// say which side of it a point is, game/water.h does 12 for gameplay).
float surface_height(float2 xz, WaterSettings s)
{
    float3 nn;
    float sq;
    float2 rest = xz;
    [unroll] for (int i = 0; i < 3; ++i)
    {
        const float3 o = gerstner(rest, 0.0, s, nn, sq);
        rest = xz - o.xz;
    }
    return pc.center_yaw.y + gerstner(rest, 0.0, s, nn, sq).y;
}

// Henyey-Greenstein phase, scaled so it averages 1 over the sphere: light keeps going mostly the way it was.
float phase_forward(float cos_theta, float g)
{
    const float g2 = g * g;
    return (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5);
}

// The light the water sends towards the eye from a point `depth` metres under the surface, looking along `dir` (from
// the eye): the sun's (forward-scattered, through the water above it, times `sun_share` for shadow and shafts) and the
// sky's, each times the water's scattering colour. At the surface and seen from above this is the `inside` of PSMain.
float3 water_glow(float depth, float3 dir, float sun_share)
{
    const float d = max(depth, 0.0);
    const float3 ls = water_sun_direction();
    const float3 sun = frame.sun_radiance.rgb * max(frame.to_sun.y, 0.0) / kPi * phase_forward(dot(dir, ls), 0.55) *
                       exp(-pc.absorption.rgb * d / max(ls.y, 0.05)) * sun_share;
    const float3 sky = environment_irradiance(float3(0, 1, 0)) / kPi * exp(-pc.absorption.rgb * d);
    return pc.scatter.rgb * (sun + sky);
}

// The surface from below: through the window straight overhead, the sky (and anything above the water, traced through
// the copied scene like a reflection) bent by the surface; beyond the critical angle, the water's own glow mirrored.
float3 surface_from_below(float3 world, float3 v, float3 n, float2 inv_size, WaterSettings s, float rough)
{
    const float3 nb = -n;  // facing down, towards the eye
    const float cos_i = saturate(dot(v, nb));
    const float sin_t2 = kWaterIor * kWaterIor * (1.0 - cos_i * cos_i);
    // The mirrored water: its glow along the reflected ray, from about as deep as that ray sees into it.
    const float3 r = reflect(-v, nb);
    const float a_mean = max((pc.absorption.r + pc.absorption.g + pc.absorption.b) / 3.0, 0.01);
    const float3 mirrored = water_glow(max(-r.y, 0.0) / a_mean, r, 1.0);
    if (sin_t2 >= 1.0) return mirrored;  // total internal reflection
    const float cos_t = sqrt(1.0 - sin_t2);
    const float3 t = refract(-v, nb, kWaterIor);
    float3 seen = environment_map.SampleLevel(environment_sampler, environment_direction(t), rough * frame.environment.w).rgb * frame.environment.x;
    if (s.shore.y > 0.0)
    {
        float2 hit_uv;
        float confidence;
        if (reflected_hit(world + float3(0.0, 0.02, 0.0), t, inv_size, hit_uv, confidence))
            seen = lerp(seen, scene_color.SampleLevel(scene_color_sampler, hit_uv, 0).rgb, confidence);
    }
    // The sun itself through the window.
    const float alpha = max(rough * rough, 0.004);
    seen += frame.sun_radiance.rgb * d_ggx(saturate(dot(t, frame.to_sun.xyz)), alpha) * 0.25 * sun_shadow(world, float3(0, 1, 0));
    // Towards the window's edge more of it is mirror (Fresnel), and over the last stretch before the critical angle it
    // blends into the mirror outright: the exact edge is a single pixel wide and only shimmers.
    const float f = lerp(0.02 + 0.98 * pow(1.0 - cos_t, 5.0), 1.0, smoothstep(0.85, 1.0, sin_t2));
    return seen * (1.0 - f) + mirrored * f;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const WaterSettings s = settings();
    const float2 inv_size = pc.params.zw;
    const float2 uv = input.position.xy * inv_size;
    const float3 world = input.world;
    const float3 to_eye = frame.camera_position.xyz - world;
    const float3 v = normalize(to_eye);
    const float t = pc.params.y;

    const float2 fw = fwidth(input.rest);
    const float footprint = max(fw.x, fw.y);
    float lost;
    // The wind roughens the ripples (8D part 5); rain rings them.
    const float wind = s.map.w;
    const float2 slope = ripple_slope(world.xz, t, footprint, lost) * (1.0 + wind * 0.12) + rain_slope(world.xz, t, s.shore.w, footprint);
    // The waves' normal at this pixel's point, tilted by the ripples.
    float3 wave_n;
    float squeeze;
    gerstner(input.rest, footprint, s, wave_n, squeeze);
    const float3 n = normalize(float3(wave_n.x - slope.x * wave_n.y, wave_n.y, wave_n.z - slope.y * wave_n.y));
    // Seen from under the water (the camera's body, the camera under its surface): the window and the mirror.
    if (pc.grid.w == water_settings[0].map_info.y)
    {
        const float3 eye = frame.camera_position.xyz;
        if (eye.y < surface_height(eye.xz, s))
        {
            const float rough_below = saturate(pc.params.x + lost * pc.size.z * 0.12);
            return float4(surface_from_below(world, v, n, inv_size, s, rough_below), 1.0);
        }
    }

    // How deep the water is here: what the scene has under this pixel.
    const uint2 px = uint2(input.position.xy);
    const float depth0 = scene_depth.Load(int3(px, 0));
    const float3 floor0 = scene_world(uv, depth0);
    const float view_depth0 = depth0 <= 0.0 ? 1e4 : distance(world, floor0);
    const float vertical0 = depth0 <= 0.0 ? 1e4 : max(world.y - floor0.y, 0.0);

    // What's under it, bent by the surface's slope (more where it's deeper), unless that lands in front of the water.
    float2 uv_r = uv + n.xz * pc.absorption.w * saturate(vertical0);
    uv_r = clamp(uv_r, inv_size * 0.5, 1.0 - inv_size * 0.5);
    float depth_r = scene_depth.Load(int3(uint2(uv_r / inv_size), 0));
    float3 floor_r = scene_world(uv_r, depth_r);
    const bool in_front = depth_r > 0.0 && floor_r.y > world.y - 0.001 && distance(frame.camera_position.xyz, floor_r) < length(to_eye);
    if (in_front)
    {
        uv_r = uv;
        depth_r = depth0;
        floor_r = floor0;
    }
    const float view_depth = depth_r <= 0.0 ? 1e4 : distance(world, floor_r);
    const float vertical = depth_r <= 0.0 ? 1e4 : max(world.y - floor_r.y, 0.0);
    // Ground under water is wet (as the wet band left it round the waterline).
    float3 below = scene_color.SampleLevel(scene_color_sampler, uv_r, 0).rgb;
    if (depth_r > 0.0) below *= wet_factor(floor_r, s);

    // Through the water: absorbed along the way down (the light) and back up the view ray.
    // The light's way down is mesh.hlsl's for the bodies that light the scene (it dims what's under them by depth, and
    // adds caustics); for any further body, counted here.
    const bool lit_below = pc.grid.w <= water_settings[0].map_info.x;
    const float3 transmittance = exp(-pc.absorption.rgb * (view_depth + (lit_below ? 0.0 : vertical)));
    const float sun_light = max(frame.to_sun.y, 0.0);
    const float shadow = sun_shadow(world, float3(0, 1, 0)) * cloud_shadow(world);
    const float3 ambient = environment_irradiance(float3(0, 1, 0)) / kPi;
    const float3 inside = pc.scatter.rgb * (frame.sun_radiance.rgb * sun_light * shadow / kPi + ambient);

    // The sky reflected, and nearby things where the screen has them; the sun's glint.
    const float n_dot_v = saturate(dot(n, v));
    const float fresnel = 0.02 + 0.98 * pow(1.0 - n_dot_v, 5.0);
    float3 r = reflect(-v, n);
    r.y = abs(r.y);  // ripples can tip a reflection below the horizon: keep it in the sky
    const float rough = saturate(pc.params.x + lost * pc.size.z * 0.12 + wind * 0.006);  // ripples too small to draw (and the wind) blur it
    float3 sky = environment_map.SampleLevel(environment_sampler, environment_direction(r), rough * frame.environment.w).rgb * frame.environment.x;
    if (s.shore.y * fresnel > 0.03 && rough < 0.5)  // where the reflection is too faint to see, the sky will do
    {
        float2 hit_uv;
        float confidence;
        if (reflected_hit(world + float3(0.0, 0.02, 0.0), r, inv_size, hit_uv, confidence))
        {
            const float3 seen = scene_color.SampleLevel(scene_color_sampler, hit_uv, 0).rgb;
            sky = lerp(sky, seen, confidence * s.shore.y * saturate(1.0 - rough / 0.5));
        }
    }
    const float3 l = frame.to_sun.xyz;
    const float3 h = normalize(l + v);
    const float alpha = max(rough * rough, 0.0025);
    const float n_dot_l = saturate(dot(n, l));
    const float3 glint = frame.sun_radiance.rgb * d_ggx(saturate(dot(n, h)), alpha) * fresnel * n_dot_l / max(4.0 * n_dot_v, 0.1) * shadow;

    float air_t;
    float3 air_in;
    atmosphere_between(world, air_t, air_in);
    // The air between the camera and the surface: what's below was fogged with the scene already (its whole path), the
    // light from the water itself (scattered inside, reflected) is fogged here; the haze's own light makes up the rest.
    const float3 below_share = transmittance * (1.0 - fresnel);
    const float3 own = (inside * (1.0 - exp(-pc.absorption.rgb * view_depth)) * (1.0 - fresnel) + sky * fresnel + glint) * air_t;
    float3 color = below * below_share + own + air_in * (1.0 - below_share);

    // Foam: in shallow water (round the shore and anything standing in it), surging in as the waves lift the water,
    // and on crests the waves squeeze steep enough to fold. Lit by the sun and the sky like a rough white surface.
    if (s.foam.x > 0.0)
    {
        // Each kind is a strength (0..1) laced by the noise: weak foam shows only where the lace is high, so it breaks
        // up into specks and streaks before it fades, strong foam is solid with holes.
        const float lace = foam_lace(world.xz, t, footprint);
        const float shallow = saturate(1.0 - vertical0 / s.foam.y);
        const float lift = pc.waves.x > 0.0 ? saturate(0.5 + (world.y - pc.center_yaw.y) / pc.waves.x) : 0.5;
        const float shore = saturate((shallow * (0.6 + 0.6 * lift) + lace - 1.05) * 3.0);
        float crest = 0.0;
        if (s.foam.z > 0.0 && pc.waves.w > 0.0)
        {
            const float fold = 1.0 - pc.waves.w * (1.0 - s.foam.z);  // the squeeze where foam starts
            const float strength = saturate((fold - squeeze) / (0.35 * pc.waves.w));
            crest = saturate((strength * 1.1 + lace - 1.15) * 3.0);
        }
        const float foam = saturate(max(shore, crest)) * s.foam.x * 0.92;
        const float3 foam_light = 0.85 * (frame.sun_radiance.rgb * saturate(dot(wave_n, l)) * shadow / kPi + environment_irradiance(wave_n) / kPi);
        color = lerp(color, foam_light * air_t + air_in, foam);
    }

    // The shore: fade in over the first few centimetres of depth.
    float3 plain = scene_color.SampleLevel(scene_color_sampler, uv, 0).rgb;
    if (depth0 > 0.0) plain *= wet_factor(floor0, s);
    color = lerp(plain, color, saturate(vertical0 / pc.scatter.w));
    return float4(color, 1.0);
}

// ---- Wet ground (part 3) ----------------------------------------------------------------------------------------
// A flat band over the body (a sea: round the camera), just above where the waves reach; drawn before the surface with
// the scene's colour multiplied by what it returns, so the ground under it near the waterline comes out darker. What
// the water then covers is drawn over again (from the copy, made wet the same way).

VertexOutput VSWet(uint vertex : SV_VertexID)
{
    const WaterSettings s = settings();
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    const float2 c = corners[vertex % 6u];
    float2 xz;
    if (pc.grid.z != 0u)
        xz = frame.camera_position.xz + c * pc.size.x;
    else
    {
        const float2 local = c * pc.size.xy;
        const float cy = cos(pc.center_yaw.w), sy = sin(pc.center_yaw.w);
        xz = pc.center_yaw.xz + float2(cy * local.x + sy * local.y, -sy * local.x + cy * local.y);
    }
    VertexOutput o;
    o.world = float3(xz.x, pc.center_yaw.y + s.shore.z, xz.y);
    o.rest = xz;
    o.position = mul(frame.clip_from_world, float4(o.world, 1.0));
    return o;
}

float4 PSWet(VertexOutput input) : SV_Target0
{
    const WaterSettings s = settings();
    const float depth0 = scene_depth.Load(int3(uint2(input.position.xy), 0));
    if (depth0 <= 0.0) return 1.0;
    const float3 ground = scene_world(input.position.xy * pc.params.zw, depth0);
    // Only ground inside the body (the band's quad covers whatever is behind it on screen too).
    if (pc.grid.z == 0u)
    {
        const float2 local = box_local(ground.xz);
        if (abs(local.x) > 1.0 || abs(local.y) > 1.0) return 1.0;
    }
    else if (distance(ground.xz, frame.camera_position.xz) > pc.size.x)
        return 1.0;
    return float4(wet_factor(ground, s).xxx, 1.0);
}

// ---- Underwater, seen from inside (part 4) -------------------------------------------------------------------------
// Two full-screen passes after the surfaces, with the camera's body's push constants: PSFogMul multiplies the picture
// by what gets through the water between the near plane and what each pixel shows (the opaque scene's copied depth, or
// the surface where the ray leaves through it), PSFogAdd adds the light the water scatters along the way. A pixel
// whose near-plane point is above the surface is left alone (the camera half in: above the waterline the surface is
// seen from above), and a thin dark line marks the waterline itself.

VertexOutput VSFog(uint vertex : SV_VertexID)
{
    const float2 p = float2((vertex << 1) & 2, vertex & 2);
    VertexOutput o;
    o.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.world = 0.0;
    o.rest = 0.0;
    return o;
}

// Where this pixel's view starts (its point on the near plane) and how far it runs through water. False: it starts in
// the air. `edge`: 0 on the waterline, 1 a pixel or two away.
bool fog_path(float4 position, WaterSettings s, out float3 start, out float3 dir, out float dist, out float edge)
{
    const float2 uv = position.xy * pc.params.zw;
    start = scene_world(uv, 1.0);  // reverse-Z: the near plane
    // The water on the lens isn't a ruler-straight edge: a few millimetres of wobble running along it.
    const float t = pc.params.y;
    const float wobble = 0.0012 * sin(dot(start.xz, float2(61.0, 37.0)) + t * 2.7) + 0.0008 * sin(dot(start.xz, float2(-43.0, 89.0)) - t * 3.9);
    const float above = start.y - surface_height(start.xz, s) + wobble;
    edge = saturate(abs(above) / (1.5 * fwidth(above) + 1e-6));
    dir = normalize(start - frame.camera_position.xyz);
    dist = 0.0;
    if (above > 0.0) return false;
    const float depth0 = scene_depth.Load(int3(uint2(position.xy), 0));
    dist = depth0 > 0.0 ? distance(start, scene_world(uv, depth0)) : 1e5;
    if (dir.y > 1e-4) dist = min(dist, max(pc.center_yaw.y - start.y, 0.0) / dir.y);  // out through the surface
    return true;
}

float4 PSFogMul(VertexOutput input) : SV_Target0
{
    const WaterSettings s = settings();
    float3 start, dir;
    float dist, edge;
    const bool under = fog_path(input.position, s, start, dir, dist, edge);
    const float line_dark = lerp(0.35, 1.0, edge);
    return float4((under ? exp(-pc.absorption.rgb * dist) : 1.0) * line_dark, 1.0);
}

// The sun's shadow at a point, one tap (the fog's shafts take many samples, each needn't be soft).
float sun_shadow_tap(float3 world)
{
    if (frame.shadow_params.x == 0.0) return 1.0;
    const float view_depth = dot(world - frame.camera_position.xyz, frame.camera_forward.xyz);
    if (view_depth > frame.cascade_splits.w) return 1.0;
    uint c = 0;
    [unroll] for (uint i = 0; i < 3; ++i)
        if (view_depth > frame.cascade_splits[i]) c = i + 1;
    const float4 p = mul(frame.cascades[c], float4(world, 1.0));
    return shadow_map.SampleCmpLevelZero(shadow_sampler, float3(p.xy * 0.5 + 0.5, c), p.z);
}

float4 PSFogAdd(VertexOutput input) : SV_Target0
{
    const WaterSettings s = settings();
    float3 start, dir;
    float dist, edge;
    if (!fog_path(input.position, s, start, dir, dist, edge)) return 0.0;
    // March as far as the water lets anything through (where the clearest colour is down to e^-6), in 16 steps with a
    // per-pixel offset (no banding), then the rest of the way as the last step's light.
    const float a_min = max(min(pc.absorption.r, min(pc.absorption.g, pc.absorption.b)), 1e-3);
    const float reach = min(dist, 6.0 / a_min);
    const int steps = 16;
    const float ds = reach / steps;
    const float jitter = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
    const float3 ls = water_sun_direction();
    const float t = pc.params.y;
    const float shafts = s.absorption.w;
    float3 through = 1.0, glow = 0.0, last = 0.0;
    [loop] for (int i = 0; i < steps; ++i)
    {
        const float3 x = start + dir * ((float(i) + jitter) * ds);
        const float d = pc.center_yaw.y - x.y;
        // Shafts: where the sun's ray to this point came through the surface, the waves gathered or spread it.
        const float2 at = x.xz + ls.xz * (max(d, 0.0) / max(ls.y, 0.05));
        const float n = value_noise(at * 0.22 + float2(t * 0.05, t * 0.035)) * 0.6 + value_noise(at * 0.61 - float2(t * 0.09, -t * 0.03)) * 0.4;
        const float lit = smoothstep(0.42, 0.78, n);
        const float shaft = lerp(1.0, 0.08 + 3.2 * lit * lit, shafts * exp(-max(d, 0.0) / 40.0));
        last = water_glow(d, dir, sun_shadow_tap(x) * shaft) * (d > 0.0 ? 1.0 : 0.0);
        const float3 next = through * exp(-pc.absorption.rgb * ds);
        glow += last * (through - next);
        through = next;
    }
    glow += last * through * (1.0 - exp(-pc.absorption.rgb * (dist - reach)));
    return float4(glow, 0.0);
}
