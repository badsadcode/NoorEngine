// Rain, snow and drifting dust (Milestone 8B, part 3, ADR-035). No simulation and no data per drop: drop i's place
// is a hash of i and the seed, moved by its fall and the wind over `time`, and wrapped into a box that follows the
// camera, so a drop leaving one side comes back on the other and the box never runs out. Drop i exists while
// i < intensity x the most drops, so more rain adds drops and none jump. Each instance is one drop, a quad made from
// the vertex index; everything about it (place, light, air) is worked out here per drop, so the pixel shader only
// draws its shape. Premultiplied blending, rgb only, like particles (particle.hlsl); the mesh pipeline's layout.

[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;

#include "frame.hlsli"

struct PushConstants
{
    uint4 ids;      // x: kind (0 rain, 1 snow, 2 dust), y: drops drawn, z: seed, w: unused
    float4 params;  // x: time (s), y: drops (intensity x most: the last one fades in), z, w: wind x, z (m/s)
};
[[vk::push_constant]] PushConstants pc;

uint weather_hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float weather_random(uint i, uint salt) { return float(weather_hash(i * 0x9e3779b9u ^ weather_hash(salt + pc.ids.z * 0x85ebca6bu)) >> 8) * (1.0 / 16777216.0); }

struct Kind
{
    float3 box;    // metres: the volume around the camera
    float fall;    // m/s
    float width;   // metres across
    float streak;  // seconds of motion a drop's quad covers (rain's motion blur); 0: round
    float alpha;
};

Kind kind_of(uint k)
{
    Kind r;
    if (k == 0u) { r.box = float3(40, 24, 40); r.fall = 9.0; r.width = 0.012; r.streak = 0.06; r.alpha = 0.22; }
    else if (k == 1u) { r.box = float3(30, 18, 30); r.fall = 1.0; r.width = 0.03; r.streak = 0.0; r.alpha = 0.9; }
    else { r.box = float3(16, 8, 16); r.fall = 0.05; r.width = 0.008; r.streak = 0.0; r.alpha = 0.7; }
    return r;
}

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;  // premultiplied rgb, alpha
    [[vk::location(1)]] float2 uv : TEXCOORD0; // -1..1 across the quad
    [[vk::location(2)]] nointerpolation float round : TEXCOORD1;
};

VertexOutput VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    const float2 c = corners[vertex % 6u];
    const uint k = pc.ids.x;
    const Kind kind = kind_of(k);
    const float t = pc.params.x;
    const float3 wind = float3(pc.params.z, 0.0, pc.params.w);
    // Where drop `instance` is: its own spot in the box, carried by its fall and the wind (snow and dust sway).
    const float3 base = float3(weather_random(instance, 1u), weather_random(instance, 2u), weather_random(instance, 3u)) * kind.box;
    const float speed_jitter = 0.8 + 0.4 * weather_random(instance, 4u);
    float3 velocity = wind + float3(0.0, -kind.fall * speed_jitter, 0.0);
    float3 travelled = velocity * t;
    if (k != 0u)
    {
        const float phase = weather_random(instance, 5u) * 6.2831853;
        const float sway = k == 1u ? 0.35 : 0.6;
        const float rate = k == 1u ? 0.9 : 0.3;
        travelled += float3(sin(t * rate + phase), 0.0, cos(t * rate * 0.8 + phase * 1.7)) * sway;
    }
    const float3 eye = frame.camera_position.xyz;
    const float3 local = frac((base + travelled - eye) / kind.box);  // 0..1 in the box around the camera
    const float3 centre = eye + (local - 0.5) * kind.box;
    // Faded near the box's sides (no popping where drops wrap round) and right in front of the lens.
    const float3 edge = abs(local - 0.5) * 2.0;
    float fade = 1.0 - smoothstep(0.75, 1.0, max(edge.x, max(edge.y, edge.z)));
    const float3 to_eye = eye - centre;
    const float distance = length(to_eye);
    fade *= smoothstep(0.3, 1.0, distance);
    // The last drop fades in as the intensity grows past it (smooth starts).
    fade *= saturate(pc.params.y - float(instance));
    // The quad: a streak along the motion for rain, round for snow and dust. At least about a pixel wide far away.
    const float3 view_dir = to_eye / max(distance, 1e-4);
    float3 right = cross(float3(0, 1, 0), view_dir);
    right = dot(right, right) > 1e-6 ? normalize(right) : float3(1, 0, 0);
    float3 up = cross(view_dir, right);
    const float half_width = max(kind.width, distance * 0.0012) * 0.5;
    float half_length = half_width;
    if (kind.streak > 0.0)
    {
        const float3 across = velocity - view_dir * dot(velocity, view_dir);
        const float len = length(across);
        if (len > 1e-4)
        {
            up = across / len;
            right = normalize(cross(up, view_dir));
            half_length = max(half_width, len * kind.streak * 0.5);
        }
    }
    VertexOutput o;
    const float3 world = centre + right * (c.x * half_width) + up * (c.y * half_length);
    o.position = mul(frame.clip_from_world, float4(world, 1.0));
    o.uv = c;
    o.round = kind.streak > 0.0 ? 0.0 : 1.0;
    // Light, once per drop. Rain: clear water, it shows the sky it reflects and refracts (and a glint of sun).
    // Snow: a white matte flake. Dust: motes that glow looking towards the sun (forward scattering).
    const float lit = sun_shadow(centre, view_dir) * cloud_shadow(centre);
    const float3 sky = environment_irradiance(float3(0, 1, 0)) / kPi;
    float3 light;
    if (k == 0u) light = sky * 0.6 + frame.sun_radiance.rgb * lit * 0.002;
    else if (k == 1u) light = 0.9 * (sky + frame.sun_radiance.rgb * lit / kPi);
    else
    {
        const float g = 0.7, cos_a = dot(-view_dir, frame.to_sun.xyz);
        const float phase = (1.0 - g * g) / (4.0 * kPi * pow(max(1e-4, 1.0 + g * g - 2.0 * g * cos_a), 1.5));
        light = 0.5 * (sky + frame.sun_radiance.rgb * lit * phase);
    }
    float transmittance;
    float3 inscatter;
    atmosphere_between(centre, transmittance, inscatter);
    const float alpha = kind.alpha * fade;
    o.color = float4((light * transmittance + inscatter) * alpha, alpha);
    return o;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    // Round drops: a soft dot. Streaks: soft across, tapering towards both ends.
    const float across = 1.0 - input.uv.x * input.uv.x;
    const float along = input.round > 0.5 ? 1.0 - input.uv.y * input.uv.y : 1.0 - input.uv.y * input.uv.y * input.uv.y * input.uv.y;
    const float shape = saturate(across) * saturate(along);
    if (shape <= 0.002) discard;
    return float4(input.color.rgb * shape, input.color.a * shape);
}
