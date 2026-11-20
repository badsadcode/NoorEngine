// Water and light (Milestone 8D, part 4, ADR-037): what mesh.hlsl and water.hlsl share about water bodies.
//
// The water draws' settings are a storage buffer (set 0, binding 9; vulkan_water.cpp's WaterGpu). Entry 0 is a header:
// map_info.x is how many bodies light the scene (the first draws, at most 8), map_info.y the body the camera may be in
// plus 1 (0: none), map_info.z the debug view (0 none, 1 depth, 2 flow, 3 waves, 4 normals, 5 cost), map.x the water's
// clock (s). Draw i is entry i + 1.
//
// Under a body's level (inside its footprint) the light from above has come down through water: the sky's light has
// lost exp(-absorption depth) of each colour, the sun's the same over its slanted way down (refracted at the surface),
// and the sun's is gathered into caustics by the waves: a net of bright lines drifting over what's below, fading
// out a few metres down. Local lights are untouched (a lamp at the bottom of the sea lights what's near it).
// Requires frame.hlsli.

#ifndef NOOR_WATER_LIGHT_HLSLI
#define NOOR_WATER_LIGHT_HLSLI

struct WaterSettings
{
    float4 map;         // xy: world (x, z) of the depth map's sample (0, 0), z: metres between samples, w: wind (m/s) (header: x the clock)
    uint4 map_info;     // xy: samples across, z: texture slot, w: 1 = has a map (header: x bodies lighting, y camera body + 1)
    float4 foam;        // x: foam, y: foam depth (m), z: crest foam, w: wet (how much darker)
    float4 shore;       // x: wet height (m), y: reflections, z: the wet band's height above the level (m), w: rain (0..1)
    float4 body;        // xyz: its middle (y: the level), w: yaw
    float4 extent;      // xy: half size (m), z: 1 = a sea, w: caustics
    float4 absorption;  // rgb: per metre, w: shafts
    float4 scatter;     // rgb: scattered back (an albedo), w: the main wave's height (m)
};
[[vk::binding(9, 0)]] StructuredBuffer<WaterSettings> water_settings;

static const float kWaterIor = 1.333;

// Is (x, z) inside this body's footprint?
bool water_covers(WaterSettings s, float2 xz)
{
    if (s.extent.z == 2.0) return false;  // a river (8D part 6): light under it isn't dimmed (yet)
    if (s.extent.z != 0.0) return true;
    const float2 d = xz - s.body.xz;
    const float c = cos(s.body.w), sn = sin(s.body.w);
    return abs(c * d.x - sn * d.y) <= s.extent.x && abs(sn * d.x + c * d.y) <= s.extent.y;
}

// The sun's direction under the water (bent towards straight down at the surface), pointing up towards it.
float3 water_sun_direction()
{
    const float3 l = frame.to_sun.xyz;
    const float2 h = l.xz / kWaterIor;
    return float3(h.x, sqrt(saturate(1.0 - dot(h, h))), h.y);
}

// Caustics: two layers of drifting cells; the light gathers along their edges. Returns about 1 on average.
float2 caustic_hash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float caustic_edges(float2 p, float t)
{
    const float2 i = floor(p), f = p - i;
    float d1 = 8.0, d2 = 8.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float2 g = float2(x, y);
            const float2 o = 0.5 + 0.4 * sin(t + 6.2831853 * caustic_hash(i + g));
            const float2 r = g + o - f;
            const float d = dot(r, r);
            if (d < d1)
            {
                d2 = d1;
                d1 = d;
            }
            else if (d < d2)
                d2 = d;
        }
    return sqrt(d2) - sqrt(d1);  // 0 on a cell's edge
}

float caustics_at(float2 p, float t)
{
    // Bent by a slow swirl first, so the net's lines curve and pinch like light through waves rather than tiles.
    const float2 q = p * 1.35;
    const float2 w = q + 0.45 * float2(sin(q.y * 0.9 + t * 0.6) + sin(q.y * 2.3 - t * 0.4), sin(q.x * 1.1 - t * 0.5) + sin(q.x * 1.9 + t * 0.7));
    const float a = caustic_edges(w, t * 0.7);
    const float b = caustic_edges(w * 1.41 + float2(7.1, 3.3), 2.0 - t * 0.55);
    // Thin bright lines, brightest where the two nets cross.
    const float la = exp(-a * 14.0), lb = exp(-b * 14.0);
    return 0.5 + 1.0 * (la + lb) + 2.0 * la * lb;
}

float water_clock() { return water_settings[0].map.x; }

// The light from above that reaches `p`: multiply the sun's light by `sun` and the sky's by `sky` (1, 1 out of water).
// `depth` returns how far under the surface it is (0 out of water), `body` which body (0: none).
void water_light(float3 p, out float3 sun, out float3 sky, out float depth, out uint body)
{
    sun = 1.0;
    sky = 1.0;
    depth = 0.0;
    body = 0u;
    const uint n = water_settings[0].map_info.x;
    for (uint i = 1u; i <= n; ++i)
    {
        const WaterSettings s = water_settings[i];
        const float d = s.body.y - p.y;
        if (d <= 0.0 || !water_covers(s, p.xz)) continue;
        const float3 ls = water_sun_direction();
        sky = exp(-s.absorption.rgb * d);
        sun = exp(-s.absorption.rgb * d / max(ls.y, 0.05));
        // Caustics where the sun's ray came through the surface, fading in below the first few centimetres and out
        // with depth (they blur as the light spreads).
        const float strength = s.extent.w * saturate(d / 0.25) * exp(-d / 7.0);
        if (strength > 0.001)
        {
            const float2 at = p.xz + ls.xz * (d / max(ls.y, 0.05));
            sun *= lerp(1.0, caustics_at(at, water_clock()), saturate(strength));
        }
        depth = d;
        body = i;
        return;
    }
}

#endif
