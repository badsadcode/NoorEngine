// Mesh shader (ADR-012). Two pipelines are built from it:
//   scene  (kDisplay = false): lit by the sun and the sky in physical units, pre-exposed, into the
//          16-bit float HDR image; tonemapping comes later in tonemap.hlsl.
//   gizmos (kDisplay = true):  editor gizmos after tonemapping: their own display colours with a
//          little shading, straight into the sRGB screen.
// Conventions (ADR-022): column-major matrices, mul(M, v); colours are linear.
//
// Surfaces have no materials yet (Milestone 5, part 3): the colour is the vertex colour, or the
// draw's tint, and every surface is a rough dielectric (roughness 0.5, metallic 0).

[[vk::constant_id(0)]] const bool kDisplay = false;

struct PushConstants
{
    float4x4 clip_from_object;  // projection * view * model
    float4 world_row0;          // the model matrix's top three rows (3x4: rotation/scale + translation)
    float4 world_row1;
    float4 world_row2;
    float4 tint;                // rgb linear colour, a = how much of it replaces the vertex colour
};
[[vk::push_constant]] PushConstants pc;

// Per frame (set 0, binding 0). Radiances are already multiplied by the exposure.
struct FrameUniforms
{
    float4 camera_position;  // xyz, world
    float4 to_sun;           // xyz: unit vector towards the sun
    float4 sun_radiance;     // rgb: sun colour * illuminance (lux) * exposure
    float4 sky_radiance;     // rgb: sky colour * luminance (nits) * exposure
    float4 ground_radiance;  // rgb: light bounced up from the ground
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
    [[vk::location(4)]] float4 tangent : TANGENT;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 world_position : POSITION1;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
    [[vk::location(4)]] float4 tangent : TANGENT;
};

float3 to_world_vector(float3 v)
{
    return float3(dot(pc.world_row0.xyz, v), dot(pc.world_row1.xyz, v), dot(pc.world_row2.xyz, v));
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(pc.clip_from_object, float4(input.position, 1.0));
    const float4 p = float4(input.position, 1.0);
    output.world_position = float3(dot(pc.world_row0, p), dot(pc.world_row1, p), dot(pc.world_row2, p));
    output.normal = to_world_vector(input.normal);  // rotation/uniform scale; non-uniform scale later
    output.tangent = float4(to_world_vector(input.tangent.xyz), input.tangent.w);
    output.color = lerp(input.color, pc.tint.rgb, pc.tint.a);
    output.uv = input.uv;
    return output;
}

// ---- BRDF (docs/rendering/PBR.md) ----
// Cook-Torrance specular: GGX distribution, height-correlated Smith visibility, Schlick Fresnel;
// Lambert diffuse. Roughness is perceptual (alpha = roughness^2).

static const float kPi = 3.14159265;

float d_ggx(float n_dot_h, float alpha)
{
    const float a2 = alpha * alpha;
    const float f = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1.0;
    return a2 / (kPi * f * f);
}

float v_smith_ggx_correlated(float n_dot_v, float n_dot_l, float alpha)
{
    const float a2 = alpha * alpha;
    const float gv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2);
    const float gl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

float3 f_schlick(float3 f0, float v_dot_h)
{
    const float f = pow(1.0 - v_dot_h, 5.0);
    return f + f0 * (1.0 - f);
}

// Light leaving the surface towards the eye, for light arriving from `l` with `radiance`.
float3 brdf_direct(float3 n, float3 v, float3 l, float3 base_color, float metallic, float roughness, float3 radiance)
{
    const float n_dot_l = saturate(dot(n, l));
    if (n_dot_l <= 0.0) return 0.0;
    const float3 h = normalize(v + l);
    const float n_dot_v = max(dot(n, v), 1e-4);
    const float n_dot_h = saturate(dot(n, h));
    const float v_dot_h = saturate(dot(v, h));
    const float alpha = max(roughness * roughness, 0.002);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);
    const float3 f = f_schlick(f0, v_dot_h);
    const float3 specular = d_ggx(n_dot_h, alpha) * v_smith_ggx_correlated(n_dot_v, n_dot_l, alpha) * f;
    const float3 diffuse = (1.0 - f) * (1.0 - metallic) * base_color / kPi;
    return (diffuse + specular) * radiance * n_dot_l;
}

// The split-sum environment BRDF (scale, bias for F0) without a lookup table: the analytic fit
// from Brian Karis's "Physically Based Shading on Mobile" (Unreal Engine blog, 2014). Replaced
// by a real lookup table with image-based lighting (part 5).
float2 env_brdf_approx(float roughness, float n_dot_v)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

float3 hemisphere(float up)
{
    return lerp(frame.ground_radiance.rgb, frame.sky_radiance.rgb, up * 0.5 + 0.5);
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float3 n = normalize(input.normal);
    if (kDisplay)
    {
        // Gizmos: readable shapes in their own colours, a fixed light from above-front.
        const float diffuse = saturate(dot(n, normalize(float3(0.4, 0.8, 0.45))));
        return float4(input.color * (0.55 + 0.45 * diffuse), 1.0);
    }
    const float3 v = normalize(frame.camera_position.xyz - input.world_position);
    const float3 base_color = input.color;
    const float metallic = 0.0;
    const float roughness = 0.5;

    float3 color = brdf_direct(n, v, frame.to_sun.xyz, base_color, metallic, roughness, frame.sun_radiance.rgb);
    // Ambient until image-based lighting (part 5): a hemisphere from the ground up to the sky.
    // Diffuse: irradiance pi * L times albedo / pi. Specular: the sky seen in the reflection
    // direction, weighted by the environment BRDF (every surface reflects at least ~4% of it).
    const float n_dot_v = max(dot(n, v), 1e-4);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);
    const float2 env = env_brdf_approx(roughness, n_dot_v);
    const float3 r = reflect(-v, n);
    color += base_color * (1.0 - metallic) * hemisphere(n.y);
    color += (f0 * env.x + env.y) * hemisphere(r.y);
    return float4(color, 1.0);  // alpha 1: something was drawn here (tonemap.hlsl)
}
