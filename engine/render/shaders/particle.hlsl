// Particles (Milestone 8B, part 2, ADR-035): each instance is one particle (render::ParticleVertex), drawn as a quad
// of two triangles made here from the vertex index: facing the camera, or stretched along its velocity. Into the HDR
// image in the see-through pass, with premultiplied blending (rgb * alpha added over what's behind times 1 - alpha):
// alpha particles write their alpha, additive ones write 0, so the same blending does both. Lit by the sun (its
// shadows and the clouds') and the sky like everything else, fogged like everything else (frame.hlsli), plus light of
// their own (fire). The shader shares the mesh pipeline's layout: the same sets, and push constants of the same shape.

[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;

#include "frame.hlsli"

// The same 32 bytes as mesh.hlsl's push constants (one pipeline layout for both).
struct PushConstants
{
    uint4 ids;      // x: texture slot (0: a soft dot), y: flipbook columns, z: rows, w: flags (below)
    float4 params;  // x: emission x exposure (nits per unit colour), y: stretch (seconds), z, w: unused
};
[[vk::push_constant]] PushConstants pc;

static const uint kVelocityFacing = 1u;
static const uint kLit = 2u;
static const uint kAdditive = 4u;
static const uint kPremultiplied = 8u;

struct VertexInput
{
    uint vertex : SV_VertexID;
    [[vk::location(0)]] float4 position_size : INSTANCE0;  // xyz world, w width (m)
    [[vk::location(1)]] float4 color : INSTANCE1;          // linear rgb, alpha
    [[vk::location(2)]] float4 velocity_frame : INSTANCE2; // xyz m/s, w flipbook frame
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;      // in the frame (0..1), or the dot's
    [[vk::location(2)]] float3 world : POSITION1;
    [[vk::location(3)]] nointerpolation float frame : TEXCOORD1;
};

VertexOutput VSMain(VertexInput input)
{
    // Two triangles: corners (-1,-1) (1,-1) (1,1), (-1,-1) (1,1) (-1,1).
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    const float2 c = corners[input.vertex % 6u];
    const float3 centre = input.position_size.xyz;
    const float half_size = input.position_size.w * 0.5;
    const float3 to_eye = normalize(frame.camera_position.xyz - centre);
    // The camera's right and up (cameras don't roll here); a camera looking straight up or down uses its forward.
    float3 right = cross(float3(0, 1, 0), to_eye);
    right = dot(right, right) > 1e-6 ? normalize(right) : float3(1, 0, 0);
    float3 up = cross(to_eye, right);
    float2 extent = float2(half_size, half_size);
    if ((pc.ids.w & kVelocityFacing) != 0u)
    {
        // Along the velocity as it crosses the view: long side its motion over `stretch` seconds, short side the size.
        const float3 v = input.velocity_frame.xyz;
        const float3 across = v - to_eye * dot(v, to_eye);
        const float len = length(across);
        if (len > 1e-4)
        {
            up = across / len;
            right = normalize(cross(up, to_eye));
            extent.y = half_size + len * pc.params.y * 0.5;
        }
    }
    VertexOutput o;
    o.world = centre + right * (c.x * extent.x) + up * (c.y * extent.y);
    o.position = mul(frame.clip_from_world, float4(o.world, 1.0));
    o.color = input.color;
    o.uv = c * float2(0.5, -0.5) + 0.5;  // top-left (0, 0)
    o.frame = input.velocity_frame.w;
    return o;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    float4 tex;
    if (pc.ids.x == 0u)
    {
        // No texture: a soft round dot, full in the middle, fading smoothly to nothing at the edge.
        const float r2 = dot(input.uv * 2.0 - 1.0, input.uv * 2.0 - 1.0);
        const float a = saturate(1.0 - r2);
        tex = float4(1.0, 1.0, 1.0, a * a);
    }
    else
    {
        const uint columns = max(1u, pc.ids.y), rows = max(1u, pc.ids.z);
        const uint index = min(uint(max(0.0, input.frame)), columns * rows - 1u);
        const float2 cell = float2(index % columns, index / columns);
        tex = textures[pc.ids.x].Sample(texture_sampler, (cell + input.uv) / float2(columns, rows));
    }
    const bool premultiplied = (pc.ids.w & kPremultiplied) != 0u;
    const float alpha = saturate(input.color.a * tex.a);
    if (alpha <= 1e-4 && !premultiplied) discard;
    // The particle's own colour (premultiplied textures: still multiplied by the texture's alpha).
    const float3 albedo = input.color.rgb * (premultiplied ? tex.rgb / max(tex.a, 1e-4) : tex.rgb);
    float3 light = albedo * pc.params.x;  // emission
    if ((pc.ids.w & kLit) != 0u)
    {
        // A small cloud lit like a matte sphere: the sun (through its shadows and the clouds') and the sky above and
        // in front, each irradiance over pi.
        const float3 to_eye = normalize(frame.camera_position.xyz - input.world);
        const float sun = sun_shadow(input.world, to_eye) * cloud_shadow(input.world);
        const float3 sky = 0.5 * (environment_irradiance(float3(0, 1, 0)) + environment_irradiance(to_eye));
        light += albedo * (frame.sun_radiance.rgb * sun + sky) / kPi;
    }
    // The air and fog between the camera and the particle, as for see-through surfaces.
    float transmittance;
    float3 inscatter;
    atmosphere_between(input.world, transmittance, inscatter);
    // Additive particles only add their own light, dimmed by the air: the fog's light is already behind them.
    const bool additive = (pc.ids.w & kAdditive) != 0u;
    const float3 rgb = (light * transmittance + (additive ? 0.0 : inscatter)) * alpha;
    return float4(rgb, additive ? 0.0 : alpha);
}
