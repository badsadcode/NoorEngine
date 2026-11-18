// Mesh shader (ADR-012). Two pipelines are built from it:
//   scene  (kDisplay = false): lit by the sun and the sky in physical units, pre-exposed, into the
//          16-bit float HDR image; tonemapping comes later in tonemap.hlsl.
//   gizmos (kDisplay = true):  editor gizmos after tonemapping: their own display colours with a
//          little shading, straight into the sRGB screen.
// Conventions (ADR-022): column-major matrices, mul(M, v); colours are linear.
//
// Surfaces (ADR-030): the draw's material from the material table (set 1, binding 2), its textures
// from the bindless table (set 1, binding 0). The vertex colour (or the draw's tint) multiplies
// the base colour, except for splats (terrains, Milestone 6 part 7): there the vertex colour holds
// the weights of material layers 1-3 (layer 0 gets the rest) and the four materials are blended.

[[vk::constant_id(0)]] const bool kDisplay = false;

struct PushConstants
{
    float4 world_row0;  // the model matrix's top three rows (3x4: rotation/scale + translation)
    float4 world_row1;
    float4 world_row2;
    float4 tint;        // rgb linear colour, a = how much of it replaces the vertex colour
    uint4 ids;          // x: material slot (splat layer 0); y: 0x80000000 | layer 1 for a splat; z, w: layers 2, 3
    float4 splat_tiling;  // UV multiplier per splat layer
};
[[vk::push_constant]] PushConstants pc;

// The bindless texture table (ADR-012): every texture, indexed by slot; one shared sampler.
[[vk::binding(0, 1)]] Texture2D<float4> textures[];
[[vk::binding(1, 1)]] SamplerState texture_sampler;

// The material table (render::MaterialDesc, vulkan_impl.h GpuMaterial).
struct GpuMaterial
{
    float4 base_color;       // linear rgb, alpha
    float4 emissive_cutoff;  // emissive rgb, alpha cutoff
    float4 params;           // metallic, roughness, normal scale, occlusion strength
    float4 uv;               // scale xy, offset xy
    uint4 tex0;              // base colour, metallic-roughness (G, B), roughness (R), metallic (R)
    uint4 tex1;              // normal, occlusion (R), emissive, flags (bit 0: cutout, bit 1: see-through, bit 2: double-sided)
};
[[vk::binding(2, 1)]] StructuredBuffer<GpuMaterial> materials;

// A point or spot light (render::LocalLight), intensity already in candela * exposure.
struct GpuLight
{
    float4 position_range;  // xyz world position, w range (m)
    float4 intensity;       // rgb
    float4 direction_type;  // xyz spot direction, w: 0 point, 1 spot
    float4 cone;            // x cos(outer), y cos(inner)
};

// Per frame (set 0, binding 0). Radiances are already multiplied by the exposure.
struct FrameUniforms
{
    float4x4 clip_from_world;  // projection * view
    float4 camera_position;    // xyz, world
    float4 to_sun;           // xyz: unit vector towards the sun
    float4 sun_radiance;     // rgb: sun colour * illuminance (lux) * exposure
    float4 sky_radiance;     // rgb: sky colour * luminance (nits) * exposure
    float4 ground_radiance;  // rgb: light bounced up from the ground
    float4x4 cascades[4];    // world -> sun shadow map, per cascade
    float4 cascade_splits;   // far end of each cascade (metres along the view)
    float4 cascade_texel;    // a shadow texel's size in metres, per cascade
    float4 shadow_params;    // x: shadows on, y: shadow map size, z: shadow distance
    float4 camera_forward;
    uint4 light_count;       // x: local lights in use
    GpuLight lights[16];
    float4x4 world_from_clip;
    float4 irradiance_sh[9]; // the environment's diffuse light, spherical harmonics (rgb)
    float4 environment;      // x: intensity * exposure, y: cos(rotation), z: sin(rotation), w: last mip
    uint4 background;
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameUniforms> frame;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] Texture2DArray<float> shadow_map;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerComparisonState shadow_sampler;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] TextureCube<float4> environment_map;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] SamplerState environment_sampler;
static const uint kBrdfLut = 3;  // built-in texture slot

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
    [[vk::location(5)]] float3 weights : TEXCOORD1;  // splats: layers 1-3
};

bool is_splat() { return (pc.ids.y & 0x80000000u) != 0u; }

float3 to_world_vector(float3 v)
{
    return float3(dot(pc.world_row0.xyz, v), dot(pc.world_row1.xyz, v), dot(pc.world_row2.xyz, v));
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 p = float4(input.position, 1.0);
    output.world_position = float3(dot(pc.world_row0, p), dot(pc.world_row1, p), dot(pc.world_row2, p));
    output.position = mul(frame.clip_from_world, float4(output.world_position, 1.0));
    output.normal = to_world_vector(input.normal);  // rotation/uniform scale; non-uniform scale later
    output.tangent = float4(to_world_vector(input.tangent.xyz), input.tangent.w);
    output.color = lerp(is_splat() ? float3(1.0, 1.0, 1.0) : input.color, pc.tint.rgb, pc.tint.a);
    output.weights = is_splat() ? input.color : float3(0.0, 0.0, 0.0);
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

// How much sunlight reaches `world` (1 lit, 0 in shadow): the cascade for this distance, a normal
// offset against acne, 3x3 hardware-filtered PCF (a soft edge about two texels wide), and a fade
// over the last 10% of the shadow distance so shadows don't end in a hard line.
float sun_shadow(float3 world, float3 n)
{
    if (frame.shadow_params.x == 0.0) return 1.0;
    const float view_depth = dot(world - frame.camera_position.xyz, frame.camera_forward.xyz);
    if (view_depth > frame.cascade_splits.w) return 1.0;
    uint c = 0;
    [unroll] for (uint i = 0; i < 3; ++i)
        if (view_depth > frame.cascade_splits[i]) c = i + 1;
    const float3 offset = world + n * frame.cascade_texel[c] * 1.5;
    const float4 s = mul(frame.cascades[c], float4(offset, 1.0));
    const float2 uv = s.xy * 0.5 + 0.5;
    const float texel = 1.0 / frame.shadow_params.y;
    float lit = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            lit += shadow_map.SampleCmpLevelZero(shadow_sampler, float3(uv + float2(x, y) * texel, c), s.z);
    lit /= 9.0;
    const float fade = saturate((view_depth - frame.cascade_splits.w * 0.9) / (frame.cascade_splits.w * 0.1));
    return lerp(lit, 1.0, fade);
}

// Light from the point and spot lights: inverse-square falloff that reaches zero smoothly at the
// light's range (windowed, as in Unreal and Filament), spots masked by a soft cone.
float3 local_lights(float3 world, float3 n, float3 v, float3 base_color, float metallic, float roughness)
{
    float3 sum = 0.0;
    for (uint i = 0; i < frame.light_count.x; ++i)
    {
        const GpuLight light = frame.lights[i];
        const float3 to_light = light.position_range.xyz - world;
        const float d2 = max(dot(to_light, to_light), 1e-4);
        const float range = light.position_range.w;
        const float window = saturate(1.0 - (d2 * d2) / (range * range * range * range));
        float falloff = window * window / d2;
        const float3 l = to_light * rsqrt(d2);
        if (light.direction_type.w > 0.5)
            falloff *= smoothstep(light.cone.x, light.cone.y, dot(-l, light.direction_type.xyz));
        if (falloff > 0.0) sum += brdf_direct(n, v, l, base_color, metallic, roughness, light.intensity.rgb * falloff);
    }
    return sum;
}

// Image-based lighting (part 5). Directions are turned by the environment's rotation first.
float3 environment_direction(float3 d)
{
    const float c = frame.environment.y, s = frame.environment.z;
    return float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

// Diffuse: irradiance from the environment's spherical harmonics (already cosine-convolved).
float3 environment_irradiance(float3 n)
{
    const float3 d = environment_direction(n);
    const float y[9] = {0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x, 1.092548 * d.x * d.y, 1.092548 * d.y * d.z,
                        0.315392 * (3.0 * d.z * d.z - 1.0), 1.092548 * d.x * d.z, 0.546274 * (d.x * d.x - d.y * d.y)};
    float3 e = 0.0;
    [unroll] for (int i = 0; i < 9; ++i) e += frame.irradiance_sh[i].rgb * y[i];
    return max(e, 0.0) * frame.environment.x;
}

// Specular: the prefiltered environment for this roughness, scaled by the split-sum BRDF lookup.
float3 environment_specular(float3 r, float roughness, float n_dot_v, float3 f0)
{
    const float3 prefiltered = environment_map.SampleLevel(environment_sampler, environment_direction(r), roughness * frame.environment.w).rgb;
    const float2 lut_uv = clamp(float2(n_dot_v, roughness), 0.5 / 64.0, 1.0 - 0.5 / 64.0);  // the sampler repeats: stay inside
    const float2 ab = textures[kBrdfLut].SampleLevel(texture_sampler, lut_uv, 0).rg;
    return prefiltered * frame.environment.x * (f0 * ab.x + ab.y);
}

// The normal map (tangent space, OpenGL convention: +Y up the texture) turned into a world normal.
// Only X and Y are read; Z is rebuilt (BC5 normal maps store two channels).
float3 apply_normal_map(float3 n, float4 tangent, float2 uv, uint slot, float strength)
{
    const float2 xy = textures[slot].Sample(texture_sampler, uv).xy * 2.0 - 1.0;
    const float3 sampled = float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
    const float3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
    const float3 b = cross(n, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
    const float3 m = float3(sampled.xy * strength, sampled.z);
    return normalize(t * m.x + b * m.y + n * m.z);
}

// One material's surface at a pixel: everything the lighting needs.
struct Surface
{
    float3 base_color;
    float alpha;
    float3 normal;
    float roughness;
    float metallic;
    float occlusion;
    float3 emissive;
};

Surface sample_surface(uint slot, float2 surface_uv, float3 n, float4 tangent)
{
    const GpuMaterial mat = materials[slot];
    const float2 uv = surface_uv * mat.uv.xy + mat.uv.zw;
    const float4 base_sample = textures[mat.tex0.x].Sample(texture_sampler, uv);
    Surface s;
    s.alpha = mat.base_color.a * base_sample.a;
    s.base_color = mat.base_color.rgb * base_sample.rgb;
    s.normal = mat.tex1.x != 1u ? apply_normal_map(n, tangent, uv, mat.tex1.x, mat.params.z) : n;
    const float3 mr = textures[mat.tex0.y].Sample(texture_sampler, uv).rgb;
    s.roughness = saturate(mat.params.y * mr.g * textures[mat.tex0.z].Sample(texture_sampler, uv).r);
    s.metallic = saturate(mat.params.x * mr.b * textures[mat.tex0.w].Sample(texture_sampler, uv).r);
    s.occlusion = lerp(1.0, textures[mat.tex1.y].Sample(texture_sampler, uv).r, mat.params.w);
    s.emissive = mat.emissive_cutoff.rgb * textures[mat.tex1.z].Sample(texture_sampler, uv).rgb;
    return s;
}

float4 PSMain(VertexOutput input, bool front : SV_IsFrontFace) : SV_Target
{
    float3 n = normalize(input.normal);
    if (kDisplay)
    {
        // Gizmos: readable shapes in their own colours, a fixed light from above-front.
        const float diffuse = saturate(dot(n, normalize(float3(0.4, 0.8, 0.45))));
        return float4(input.color * (0.55 + 0.45 * diffuse), 1.0);
    }
    const GpuMaterial mat = materials[pc.ids.x];
    if (!is_splat())
    {
        const float2 uv = input.uv * mat.uv.xy + mat.uv.zw;
        if ((mat.tex1.w & 1u) != 0u && mat.base_color.a * textures[mat.tex0.x].Sample(texture_sampler, uv).a < mat.emissive_cutoff.w) discard;  // cutout
    }
    if (!front) n = -n;  // double-sided: the back face's normal points the other way
    const float4 tangent = input.tangent * (front ? 1.0 : -1.0);
    Surface surf;
    if (is_splat())
    {
        // Four layers blended by weight (layers with almost no weight are skipped).
        const float4 w = float4(saturate(1.0 - input.weights.x - input.weights.y - input.weights.z), saturate(input.weights));
        const uint slots[4] = {pc.ids.x, pc.ids.y & 0x7FFFFFFFu, pc.ids.z, pc.ids.w};
        surf.base_color = 0.0;
        surf.alpha = 1.0;
        surf.normal = 0.0;
        surf.roughness = 0.0;
        surf.metallic = 0.0;
        surf.occlusion = 0.0;
        surf.emissive = 0.0;
        float total = 0.0;
        [unroll] for (uint i = 0; i < 4; ++i)
        {
            if (w[i] < 0.004) continue;
            const Surface s = sample_surface(slots[i], input.uv * pc.splat_tiling[i], n, tangent);
            surf.base_color += s.base_color * w[i];
            surf.normal += s.normal * w[i];
            surf.roughness += s.roughness * w[i];
            surf.metallic += s.metallic * w[i];
            surf.occlusion += s.occlusion * w[i];
            surf.emissive += s.emissive * w[i];
            total += w[i];
        }
        const float k = 1.0 / max(total, 1e-4);
        surf.base_color *= k;
        surf.normal = normalize(surf.normal + n * 1e-4);
        surf.roughness *= k;
        surf.metallic *= k;
        surf.occlusion *= k;
        surf.emissive *= k;
    }
    else
    {
        surf = sample_surface(pc.ids.x, input.uv, n, tangent);
    }
    n = surf.normal;
    const float alpha = surf.alpha;
    const float3 v = normalize(frame.camera_position.xyz - input.world_position);
    const float3 base_color = input.color * surf.base_color;
    const float roughness = surf.roughness;
    const float metallic = surf.metallic;
    const float occlusion = surf.occlusion;
    const float3 emissive = surf.emissive;

    const float shadow = sun_shadow(input.world_position, normalize(input.normal) * (front ? 1.0 : -1.0));
    float3 color = brdf_direct(n, v, frame.to_sun.xyz, base_color, metallic, roughness, frame.sun_radiance.rgb * shadow);
    color += local_lights(input.world_position, n, v, base_color, metallic, roughness);
    // The environment (image-based lighting): diffuse irradiance and prefiltered reflections.
    const float n_dot_v = max(dot(n, v), 1e-4);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);
    const float3 r = reflect(-v, n);
    const float3 diffuse = base_color * (1.0 - metallic) * environment_irradiance(n) / kPi;
    const float3 reflection = environment_specular(r, roughness, n_dot_v, f0) * occlusion;
    color += diffuse * occlusion;
    if ((mat.tex1.w & 2u) != 0u)
    {
        // See-through (premultiplied alpha): the light the surface sends back is scaled by how much
        // of it is there; reflections and glow stay whole (a clear window still reflects the sky).
        const float a = saturate(alpha);
        return float4(color * a + reflection + emissive, a);
    }
    color += reflection;
    color += emissive;  // relative to the exposure (already "exposed"): 1 is about white on screen
    return float4(color, 1.0);  // alpha 1: something was drawn here (tonemap.hlsl)
}
