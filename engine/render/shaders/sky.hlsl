// The sky (Milestone 5, part 5): the environment's background texture (equirectangular) drawn
// behind everything, as a full-screen triangle at infinite distance. Alpha 0 marks "background" for
// the tonemap pass, like the clear colour.

struct FrameUniforms
{
    float4x4 clip_from_world;
    float4 camera_position;
    float4 to_sun;
    float4 sun_radiance;
    float4 sky_radiance;
    float4 ground_radiance;
    float4x4 cascades[4];
    float4 cascade_splits;
    float4 cascade_texel;
    float4 shadow_params;
    float4 camera_forward;
    uint4 light_count;
    float4 lights[64];          // GpuLight[16] (not used here)
    float4x4 world_from_clip;
    float4 environment;         // x: intensity * exposure, y: cos(rotation), z: sin(rotation), w: last mip
    uint4 background;           // x: background texture slot, y: 1 = draw the sun's disc
    float4 fog;                 // x density, y falloff, z height, w anisotropy (fog.hlsli)
    float4 fog_albedo;
    float4 aerial;
    float4 cloud_a[2];       // per layer: coverage, density, altitude km, scale km
    float4 cloud_b[2];       // per layer: drift x, z km; [0].z planet radius km, [0].w camera height km
    uint4 cloud_flags;       // seed 0, seed 1, enabled 0, enabled 1
    float4 sky_sun_direction;  // towards the real sun
    float4 sky_sun_seen;       // its disc's colour x illuminance through the air (exposed)
    float4 sky_sun_outer;      // its colour x illuminance outside the air (exposed): lights the bodies
    float4 sky_params;         // x: 1 space, y: star brightness x exposure (0 off), z: sidereal hours, w: latitude
    float4 sky_extinction;     // the air straight up (rgb transmittance)
    uint4 sky_counts;          // x: bodies, y: star seed
    float4 bodies[32];         // 8 x (direction, angular radius), (albedo, -), (ring axis, inner), (ring colour, outer)
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;
[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;
[[vk::binding(3, 0)]] StructuredBuffer<float4> irradiance_sh;  // the fog's ambient light
[[vk::binding(5, 0)]] StructuredBuffer<float4> cloud_light;    // [2i] sun, [2i + 1] sky at cloud layer i (relative)

#include "clouds.hlsli"
#include "fog.hlsli"
#include "stars.hlsli"

CloudLayer cloud_layer(uint i)
{
    CloudLayer l;
    l.coverage = frame.cloud_a[i].x;
    l.density = frame.cloud_a[i].y;
    l.altitude_km = frame.cloud_a[i].z;
    l.scale_km = frame.cloud_a[i].w;
    l.offset_km = frame.cloud_b[i].xy;
    l.seed = frame.cloud_flags[i];
    l.enabled = frame.cloud_flags[2 + i] != 0;
    return l;
}

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

VertexOutput VSMain(uint id : SV_VertexID)
{
    VertexOutput output;
    const float2 uv = float2((id << 1) & 2, id & 2);
    output.ndc = uv * 2.0 - 1.0;
    output.position = float4(output.ndc, 0.0, 1.0);  // depth 0: infinitely far (reverse-Z)
    return output;
}

static const float kPi = 3.14159265;

// The light on a surface facing `n` from the real sun and any extra suns (exposed lux), outside the air.
float3 suns_on(float3 n, bool both_faces)
{
    const float sun = dot(n, frame.sky_sun_direction.xyz);
    float3 e = frame.sky_sun_outer.rgb * (both_faces ? abs(sun) : max(0.0, sun));
    for (uint i = 0; i < frame.light_count.x; ++i)
    {
        const float4 dt = frame.lights[i * 4 + 2];
        if (dt.w < 1.5) continue;
        const float c = dot(n, -dt.xyz);
        e += frame.lights[i * 4 + 1].rgb * (both_faces ? abs(c) : max(0.0, c));
    }
    return e;
}

// The light from what lies beyond the air along `d` (part 5): stars, then planets and moons in front of them
// (Lambert spheres lit by every sun, rings shaded by their planet), before the air dims it.
float3 beyond_the_sky(float3 d, float pixel_angle)
{
    float3 behind = 0.0;
    if (frame.sky_params.y > 0.0)
        behind = starlight(d, frame.sky_params.w, frame.sky_params.z, frame.sky_counts.y, pixel_angle) * frame.sky_params.y;
    for (uint i = 0; i < min(frame.sky_counts.x, 8u); ++i)
    {
        const float4 b0 = frame.bodies[i * 4 + 0], b1 = frame.bodies[i * 4 + 1], b2 = frame.bodies[i * 4 + 2], b3 = frame.bodies[i * 4 + 3];
        const float3 c = b0.xyz;
        const float ang = b0.w, r = sin(ang);
        const float extent = ang * max(1.0, b3.w);
        const float cos_d = dot(d, c);
        if (cos_d < cos(extent + 2.0 * pixel_angle)) continue;
        // The sphere (centre at distance 1, radius r: it covers `ang`).
        const float h = cos_d * cos_d - (1.0 - r * r);
        const float t_sphere = h > 0.0 ? cos_d - sqrt(h) : 1e9;
        const float cover = saturate((ang - acos(clamp(cos_d, -1.0, 1.0))) / max(pixel_angle, 1e-6) + 0.5);
        float3 planet = 0.0;
        if (h > 0.0)
        {
            const float3 n = normalize(d * t_sphere - c);
            planet = b1.rgb / kPi * suns_on(n, false);
        }
        // Rings: an annulus in the plane through the centre across the ring axis.
        float ring_alpha = 0.0, t_ring = 1e9;
        float3 ring = 0.0;
        if (b3.w > 0.0)
        {
            const float3 a = b2.xyz;
            const float denom = dot(d, a);
            if (abs(denom) > 1e-6)
            {
                t_ring = dot(c, a) / denom;
                const float3 q = d * t_ring - c;
                const float rr = length(q) / r;
                if (t_ring > 0.0 && rr > b2.w && rr < b3.w)
                {
                    const float band = 0.6 + 0.4 * sin(rr * 41.0) * sin(rr * 13.7 + 1.3);
                    // The planet's shadow: does the way from here to the sun pass through the sphere?
                    const float3 l = frame.sky_sun_direction.xyz;
                    const float bb = dot(q, l), cc = dot(q, q) - r * r;
                    const float lit = (bb < 0.0 && bb * bb - cc > 0.0) ? 0.08 : 1.0;
                    ring = b3.rgb / kPi * suns_on(a, true) * lit * band;
                    ring_alpha = saturate(0.85 * band);
                }
            }
        }
        // Back to front: the ring behind the planet, or in front of it.
        if (t_ring < t_sphere)
        {
            behind = lerp(behind, planet, cover);
            behind = lerp(behind, ring, ring_alpha);
        }
        else
        {
            behind = lerp(behind, ring, ring_alpha);
            behind = lerp(behind, planet, cover);
        }
    }
    if (frame.sky_params.x == 0.0)  // on a planet with air: hidden below the horizon, dimmed by the air mass
    {
        if (d.y < 0.0) return 0.0;
        const float airmass = 1.0 / (d.y + 0.025 * exp(-11.0 * d.y));
        behind *= pow(max(frame.sky_extinction.rgb, 1e-4), airmass);
    }
    return behind;
}

// The disc of a sun: 0.2665 degrees in radius (6.8e-5 sr), radiance = illuminance / solid angle, darker towards
// the rim (linear limb darkening, 0.6; its average over the disc is 0.8).
float3 sun_disc(float3 d, float3 to_sun, float3 illuminance)
{
    const float cos_radius = 0.99998918;
    const float cos_sun = dot(d, to_sun);
    if (cos_sun <= cos_radius) return 0.0;
    const float r2 = saturate((1.0 - cos_sun) / (1.0 - cos_radius));  // (angle / radius)^2
    const float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - r2));
    return illuminance * (limb / (0.8 * 6.8e-5));
}

float4 PSMain(VertexOutput input) : SV_Target
{
    // The view ray through this pixel: unproject a point on the near plane.
    const float4 p = mul(frame.world_from_clip, float4(input.ndc, 1.0, 1.0));
    float3 d = normalize(p.xyz / p.w - frame.camera_position.xyz);
    const float3 world_d = d;
    const float pixel_angle = length(fwidth(world_d));  // how much sky one pixel covers (clouds' detail)
    // The environment's turn about +Y.
    const float c = frame.environment.y, s = frame.environment.z;
    d = float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    // Equirect: u = 0.5 looks along -Z, grows towards +X; v = 0 straight up (asset/environment.cpp).
    const float u = atan2(d.x, -d.z) / (2.0 * kPi) + 0.5;
    const float v = acos(clamp(d.y, -1.0, 1.0)) / kPi;
    const float3 sky = textures[frame.background.x].SampleLevel(texture_sampler, float2(u, v), 0).rgb;
    float3 color = sky * frame.environment.x;
    // What lies beyond the air (stars, planets, the moon), seen through it: the sky's own light is in front.
    color += beyond_the_sky(world_d, pixel_angle);
    if (frame.background.y != 0)
    {
        // The real sun's disc, and the discs of extra suns (part 5).
        color += sun_disc(world_d, frame.sky_sun_direction.xyz, frame.sky_sun_seen.rgb);
        for (uint i = 0; i < frame.light_count.x; ++i)
        {
            const float4 dt = frame.lights[i * 4 + 2];
            if (dt.w > 1.5) color += sun_disc(world_d, -dt.xyz, frame.lights[i * 4 + 1].rgb);
        }
        color = min(color, 60000.0);  // the HDR target is half floats
    }
    // Cloud layers (part 4), sharp per pixel, the higher one first; over the sun's disc.
    if (frame.cloud_flags.z != 0 || frame.cloud_flags.w != 0)
    {
        const float3 eye_km = float3(frame.camera_position.x * 0.001, frame.cloud_b[0].w, frame.camera_position.z * 0.001);
        const uint first = frame.cloud_a[1].z > frame.cloud_a[0].z ? 1u : 0u;
        [unroll] for (uint k = 0; k < 2; ++k)
        {
            const uint i = k == 0 ? first : 1u - first;
            float alpha;
            const float3 c = cloud_layer_shade(cloud_layer(i), eye_km, frame.cloud_b[0].z, world_d, pixel_angle, frame.to_sun.xyz, cloud_light[i * 2].rgb,
                                               cloud_light[i * 2 + 1].rgb, alpha);
            color = color * (1.0 - alpha) + c * frame.environment.x * alpha;  // relative to the sun's illuminance
        }
    }
    // Height fog in front of the sky (to infinity): on the horizon the sky becomes the fog's colour, which
    // is where distant fogged ground ends up too, so the two meet without a seam.
    if (frame.fog.x > 0.0)
    {
        Fog f;
        f.density = frame.fog.x;
        f.falloff = frame.fog.y;
        f.height = frame.fog.z;
        f.anisotropy = frame.fog.w;
        f.albedo = frame.fog_albedo.rgb;
        const float3 ambient = max(irradiance_sh[0].rgb, 0.0) * (0.282095 / kPi) * frame.environment.x;
        float t;
        float3 s;
        fog_apply(f, frame.camera_position.xyz, world_d, 0.0, true, frame.to_sun.xyz, frame.sun_radiance.rgb, ambient, t, s);
        color = color * t + s;
    }
    return float4(color, 0.0);
}
