// Water surfaces (Milestone 8D, part 1, ADR-037). Drawn in the see-through pass over a copy of the opaque scene (its
// colour and depth, set 0 bindings 7 and 8), so every pixel knows how much water lies between the surface and what's
// under it:
//   - light passing through is absorbed per colour (red first, then green: Beer-Lambert, `absorption` per metre),
//     along the view ray under the surface plus the depth the light came down from;
//   - light scattered back from inside the water (`scatter`, lit by the sun and the sky) fills in what's absorbed;
//   - what's under the surface is seen bent (the lookup offset by the surface's slope, `refraction`), unless the
//     offset lands on something in front of the water;
//   - at the shore the water fades in over `shore_fade` metres of depth: no hard line where it meets the ground;
//   - the sky is reflected (the environment's cube, blurred by roughness) by Fresnel, and the sun glints (GGX);
//   - small ripples (a few sine waves in world space, moving with time) bend the normal: waves come in part 2.
// The atmosphere and fog between the camera and the surface apply to the water's own light (reflected, scattered); what's
// seen through already had its own (an approximation: the stretch from the surface down counts as air). A sea is the same
// surface, drawn under the camera out to the horizon. Writes depth and alpha 1.

#include "frame.hlsli"

[[vk::combinedImageSampler]] [[vk::binding(7, 0)]] Texture2D<float4> scene_color;
[[vk::combinedImageSampler]] [[vk::binding(7, 0)]] SamplerState scene_color_sampler;
[[vk::combinedImageSampler]] [[vk::binding(8, 0)]] Texture2D<float> scene_depth;
[[vk::combinedImageSampler]] [[vk::binding(8, 0)]] SamplerState scene_depth_sampler;

struct PushConstants
{
    float4 center_yaw;  // xyz: the surface's middle (y: the level), w: yaw
    float4 size;        // xy: half size (m), z: ripple strength, w: metres per ripple
    float4 absorption;  // rgb: per metre, w: refraction
    float4 scatter;     // rgb, w: shore fade (m)
    float4 params;      // x: roughness, y: time (s), zw: 1 / screen size
    uint4 grid;         // x: cells per side
};
[[vk::push_constant]] PushConstants pc;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world : POSITION1;
};

VertexOutput VSMain(uint vertex : SV_VertexID)
{
    // A grid of cells, two triangles each, over the footprint (-1..1 on both axes before scaling).
    const uint n = max(pc.grid.x, 1u);
    const uint cell = vertex / 6u;
    const uint2 corners[6] = {uint2(0, 0), uint2(1, 0), uint2(1, 1), uint2(0, 0), uint2(1, 1), uint2(0, 1)};
    const uint2 c = uint2(cell % n, cell / n) + corners[vertex % 6u];
    const float2 local = (float2(c) / float(n)) * 2.0 - 1.0;
    const float2 xz = local * pc.size.xy;
    const float cy = cos(pc.center_yaw.w), sy = sin(pc.center_yaw.w);
    const float3 world = pc.center_yaw.xyz + float3(cy * xz.x + sy * xz.y, 0.0, -sy * xz.x + cy * xz.y);
    VertexOutput o;
    o.world = world;
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

// World position of the opaque scene at a pixel (reverse-Z: depth 0 is infinitely far: the sky).
float3 scene_world(float2 uv, float depth)
{
    const float4 p = mul(frame.world_from_clip, float4(uv * 2.0 - 1.0, depth, 1.0));
    return p.xyz / p.w;
}

float d_ggx(float n_dot_h, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float2 inv_size = pc.params.zw;
    const float2 uv = input.position.xy * inv_size;
    const float3 world = input.world;
    const float3 to_eye = frame.camera_position.xyz - world;
    const float3 v = normalize(to_eye);
    const float t = pc.params.y;

    const float2 fw = fwidth(world.xz);
    float lost;
    const float2 slope = ripple_slope(world.xz, t, max(fw.x, fw.y), lost);
    const float3 n = normalize(float3(-slope.x, 1.0, -slope.y));

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
    const float3 below = scene_color.SampleLevel(scene_color_sampler, uv_r, 0).rgb;

    // Through the water: absorbed along the way down (the light) and back up the view ray.
    const float3 transmittance = exp(-pc.absorption.rgb * (view_depth + vertical));
    const float sun_light = max(frame.to_sun.y, 0.0);
    const float shadow = sun_shadow(world, float3(0, 1, 0)) * cloud_shadow(world);
    const float3 ambient = environment_irradiance(float3(0, 1, 0)) / kPi;
    const float3 inside = pc.scatter.rgb * (frame.sun_radiance.rgb * sun_light * shadow / kPi + ambient);

    // The sky reflected, and the sun's glint.
    const float n_dot_v = saturate(dot(n, v));
    const float fresnel = 0.02 + 0.98 * pow(1.0 - n_dot_v, 5.0);
    float3 r = reflect(-v, n);
    r.y = abs(r.y);  // ripples can tip a reflection below the horizon: keep it in the sky
    const float rough = saturate(pc.params.x + lost * pc.size.z * 0.12);  // ripples too small to draw still blur it
    const float3 sky = environment_map.SampleLevel(environment_sampler, environment_direction(r), rough * frame.environment.w).rgb * frame.environment.x;
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

    // The shore: fade in over the first few centimetres of depth.
    const float3 plain = scene_color.SampleLevel(scene_color_sampler, uv, 0).rgb;
    color = lerp(plain, color, saturate(vertical0 / pc.scatter.w));
    return float4(color, 1.0);
}
