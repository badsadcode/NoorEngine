#pragma once
// NoorEngine renderer: Vulkan 1.3 device, swapchain, GPU meshes, a camera and frame capture for
// automated tests. Milestone 5 (ADR-012): the scene is lit by a sun and a sky in physical units
// into a 16-bit float HDR image, then exposed, tonemapped and drawn to the screen; editor gizmos
// and the HUD go on top after tonemapping, in display colours.
// Vulkan types are hidden behind a pimpl (ADR-002). Conventions: ADR-022.

#include "noor/foundation/error.h"
#include "noor/foundation/math.h"
#include "noor/platform/presentation_target.h"
#include "noor/render/clouds.h"
#include "noor/render/fog.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace noor::render {

struct RendererDesc {
    // Enable VK_LAYER_KHRONOS_validation if installed. Validation messages go to the log
    // under category "Vulkan" and are counted.
    bool enable_validation = true;
    // Size of each sun shadow cascade (square, 4 cascades). 2048 is sharp to about 80 m.
    std::uint32_t shadow_map_size = 2048;
    // Multisample anti-aliasing for the scene: 1 (off), 2, 4 or 8 samples per pixel, lowered to
    // what the GPU supports. Renderer::set_msaa changes it later.
    std::uint32_t msaa_samples = 4;
    // Linear RGBA background when Lighting::sky_background is false, shown exactly (the swapchain
    // is sRGB, so the GPU encodes it on write).
    std::array<float, 4> clear_color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct GpuInfo {
    std::string name;
    std::string device_type;  // "discrete", "integrated", ...
    std::string api_version;  // "1.4.312"
    std::uint32_t vendor_id = 0;
    std::uint32_t driver_version_raw = 0;
};

enum class FrameResult : std::uint8_t {
    Presented,  // a frame was rendered and queued for display
    Skipped,    // nothing to draw into (minimized, or swapchain being recreated)
};

// ---- Meshes -----------------------------------------------------------------------------

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec3 color{1, 1, 1};       // linear RGB (multiplies the surface colour)
    math::Vec2 uv{0, 0};             // texture coordinates (glTF convention: origin top-left)
    math::Vec4 tangent{1, 0, 0, 1};  // xyz along +u, w = handedness of the bitangent (glTF)
};

// How a vertex of a skinned mesh follows its skeleton (Milestone 8C, ADR-036): four joints of the draw's palette and
// their weights (adding up to 1). The same layout as asset::SkinWeights.
struct SkinVertex {
    std::array<std::uint16_t, 4> joints{};
    math::Vec4 weights{1, 0, 0, 0};
};
static_assert(sizeof(SkinVertex) == 24);

struct MeshData {
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;  // triangle list, counter-clockwise front faces
    std::string_view debug_name;             // shown in RenderDoc/Nsight; optional
    std::span<const SkinVertex> skin{};      // one per vertex for a skinned mesh (8C); empty: rigid
};

// Opaque handle to a mesh living in GPU memory. 0 is never a valid handle.
struct MeshHandle {
    std::uint32_t id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

// ---- Textures (ADR-029) ------------------------------------------------------------------

enum class TextureFormat : std::uint8_t {
    Rgba8Srgb,    // colour: sampled as linear (the GPU decodes sRGB)
    Rgba8Unorm,   // data and normal maps: sampled as stored
    Rgba16Float,  // light values (HDR)
    Bc7Srgb,      // block-compressed colour (16 bytes per 4x4 block)
    Bc7Unorm,     // block-compressed data (ORM, masks)
    Bc5Unorm,     // block-compressed normal maps: X and Y only (the shader rebuilds Z)
    R32Float,     // one float a texel (water's depth maps, 8D): read exactly with Load, never filtered
};

struct TextureMipData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::uint8_t> bytes;  // tightly packed rows
};

struct TextureDesc {
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::span<const TextureMipData> mips;  // full size first, each next one half (rounded down, at least 1)
    std::string_view debug_name;
};

// A texture in GPU memory. Its id is its slot in the shader's texture table; 0 is never a
// texture you created (it is the built-in white).
struct TextureHandle {
    std::uint32_t id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

// ---- Materials (ADR-030) -----------------------------------------------------------------

enum class AlphaMode : std::uint8_t {  // Blend: see-through, drawn after the opaque scene, sorted back to front
    Opaque,
    Cutout,  // pixels with alpha below alpha_cutoff are not drawn (leaves, fences)
    Blend,   // see-through (premultiplied), in the see-through pass, sorted (DrawItem::sort_layer, sort_order, distance)
};

// What the shader needs to draw a surface (glTF metallic-roughness, ADR-012). Factors multiply
// their textures; a texture left empty counts as white (or flat, for the normal map).
struct MaterialDesc {
    math::Vec4 base_color{1, 1, 1, 1};  // linear rgb, alpha
    float metallic = 0.0f;
    float roughness = 0.5f;             // 0 mirror .. 1 matte
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    math::Vec3 emissive{0, 0, 0};       // relative to the exposure: 1 shows about white on screen
    AlphaMode alpha = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;          // draw back faces too (thin leaves, cloth, planes seen from below)
    // The base colour (and cutout) sampled without blending between texels: crisp pixel art (sprites, 8B part 5).
    bool nearest_filter = false;
    // 2D part 3 (ADR-040). Unlit: shows its own colours (base colour x texture, plus emissive), no light, no fog.
    // Coverage: see-through by coverage (sprites, foliage cards): alpha scales everything it shows, reflections too
    // (Blend alone is glass: a clear pane still reflects). Clamp UV: the texture is sampled only inside the 0..1 UV
    // rectangle (after uv_scale / uv_offset: a sprite's frame), half a texel in, so neighbouring frames never bleed in.
    bool unlit = false;
    bool coverage = false;
    bool clamp_uv = false;
    math::Vec2 uv_scale{1, 1};
    math::Vec2 uv_offset{0, 0};
    TextureHandle base_color_texture;          // rgb colour, a alpha
    TextureHandle metallic_roughness_texture;  // glTF packing: G roughness, B metallic (ORM maps fit)
    TextureHandle roughness_texture;           // single channel (red)
    TextureHandle metallic_texture;            // single channel (red)
    TextureHandle normal_texture;              // tangent space, OpenGL convention
    TextureHandle occlusion_texture;           // red
    TextureHandle emissive_texture;            // rgb
};

// A material in the GPU material table. 0 is the built-in default (white, roughness 0.5).
struct MaterialHandle {
    std::uint32_t id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

// ---- Frames -----------------------------------------------------------------------------

struct Camera {
    math::Mat4 view = math::Mat4::identity();  // world -> view (see math::look_at)
    float fov_y = math::radians(60.0f);
    float near_plane = 0.1f;                   // reverse-Z infinite far plane
    // Orthographic (Milestone 9A, part 1: 2D games, top-down and isometric views): the whole height shown, in metres;
    // 0 = perspective. A parallel view has a far plane.
    float ortho_height = 0.0f;
    float far_plane = 1000.0f;
    [[nodiscard]] bool orthographic() const noexcept { return ortho_height > 0.0f; }
    // How far away something `distance` metres off looks, for choosing levels of detail: a parallel view shows everything
    // at one size, as a perspective view of fov_y would at the distance where it shows ortho_height.
    [[nodiscard]] float lod_distance(float distance) const noexcept {
        return orthographic() ? 0.5f * ortho_height / std::tan(std::clamp(fov_y, 0.01f, 3.0f) * 0.5f) : distance;
    }
    // Clip from view for this camera at `aspect` (width / height).
    [[nodiscard]] math::Mat4 projection(float aspect) const noexcept {
        return orthographic() ? math::orthographic_reverse_z(ortho_height, aspect, near_plane, std::max(far_plane, near_plane * 2.0f))
                              : math::perspective_reverse_z(fov_y, aspect, near_plane);
    }
};

// How the scene is lit and exposed (ADR-012). Physical units: the sun in lux, the sky in nits
// (cd/mÂ˛); the camera's exposure as EV100 (sunny day â‰ 15, overcast â‰ 12, bright room â‰ 8).
// The defaults are a clear day, so a scene without lights still looks right.
enum class Tonemapper : std::uint8_t {
    Neutral,  // Khronos PBR Neutral: keeps material colours as authored (default)
    Aces,     // the common ACES film fit: more contrast, shifts very bright colours
    None,     // clamp: for debugging exposure
};

// ---- Environments (image-based lighting, Milestone 5 part 5) ----------------------------

// What lights the scene from all around and shows behind it: a prefiltered cubemap (mip m = what a
// surface of roughness m / (mips - 1) reflects), diffuse irradiance as spherical harmonics, and a
// background texture (equirectangular, RGBA16F) drawn as the sky. Values are in the environment's
// own units; Lighting::environment_intensity (nits per unit) and the exposure scale them.
struct EnvironmentDesc {
    std::uint32_t face_size = 0;
    std::uint32_t mip_count = 0;
    // RGBA 16-bit float texels, mip by mip, each mip face by face (+X, -X, +Y, -Y, +Z, -Z).
    std::span<const std::uint16_t> cube;
    std::array<math::Vec3, 9> irradiance_sh{};
    TextureHandle background;  // an equirect RGBA16F texture (create_texture); none: no sky drawn
};

// The live atmosphere sky (Milestone 8A, part 2, ADR-033): the parameters of asset::AtmosphereSettings
// (the render module doesn't depend on asset: game::EnvironmentLibrary copies them) and the sun.
struct AtmosphereDesc {
    float planet_radius_km = 6360.0f;
    float atmosphere_height_km = 100.0f;
    math::Vec3 rayleigh_scattering{5.802e-3f, 13.558e-3f, 33.1e-3f};
    float rayleigh_height_km = 8.0f;
    float mie_scattering = 3.996e-3f;
    float mie_absorption = 4.40e-3f;
    float mie_height_km = 1.2f;
    float mie_g = 0.8f;
    math::Vec3 ozone_absorption{0.650e-3f, 1.881e-3f, 0.085e-3f};
    float ozone_centre_km = 25.0f, ozone_half_width_km = 15.0f;
    math::Vec3 ground_albedo{0.3f, 0.3f, 0.3f};
    float turbidity = 1.0f;
    float altitude_m = 0.0f;
    math::Vec3 to_sun{0, 1, 0};  // towards the sun (unit)
    // Aerial perspective (part 3): scene distances are multiplied by this before the air between the camera
    // and a surface is worked out. 1 is physical (a hill 1 km away gets 1 km of air); a small world can make
    // its far edge look further away; 0 turns it off.
    float aerial_distance_scale = 1.0f;
};

// A point or spot light (Milestone 5, part 4). Intensity is luminous power in lumens, like a
// bulb's box says (a 60 W-equivalent bulb is about 800 lm): a point light sends it everywhere, a
// spot into its cone (so a narrower spot of the same lumens is brighter, like a torch). Light
// fades with the square of the distance and smoothly reaches zero at `range`.
// Directional lights after the first (Milestone 8A, part 5: a second sun) are LocalLights of type Directional:
// light from `direction` everywhere (no range, no shadows), `lux` on a surface facing it.
struct LocalLight {
    enum class Type : std::uint8_t { Point, Spot, Directional } type = Type::Point;
    math::Vec3 position{0, 0, 0};
    math::Vec3 direction{0, -1, 0};  // spot: the way it points
    math::Vec3 color{1, 1, 1};       // linear
    float lumens = 800.0f;
    float range = 10.0f;             // metres
    float outer_angle = 0.6f;        // spot: half-angle of the cone, radians
    float inner_angle = 0.45f;       // spot: full brightness inside this half-angle
    float lux = 0.0f;                // directional: illuminance
};

// A planet, moon or ring system in the sky (Milestone 8A, part 5): a sphere at a direction and angular size, lit
// by the real sun and any extra suns (Lambert), with optional rings (an annulus in the plane across `ring_axis`,
// in body radii) that the body shades. The moon is one of these.
struct CelestialBody {
    math::Vec3 direction{0, 1, 0};   // towards it (unit)
    float angular_radius = 0.01f;    // radians
    math::Vec3 albedo{0.5f, 0.5f, 0.5f};
    math::Vec3 ring_axis{0, 1, 0};
    float ring_inner = 0.0f, ring_outer = 0.0f;  // body radii; outer 0: no rings
    math::Vec3 ring_color{0.8f, 0.75f, 0.65f};
};
inline constexpr std::size_t kMaxCelestialBodies = 8;

// What the sky pass draws besides the sky itself (part 5).
struct SkyObjects {
    math::Vec3 sun_direction{0, 1, 0};  // towards the real sun (at night the sun light may be the moon)
    math::Vec3 sun_outer{0, 0, 0};      // its colour x illuminance outside any air (lights the bodies)
    math::Vec3 sun_seen{0, 0, 0};       // the same as seen from here through the air (its disc)
    std::vector<CelestialBody> bodies;  // up to kMaxCelestialBodies
    bool stars = false;
    float star_brightness = 1.0f;       // 1 = real starlight
    float sidereal_hours = 0.0f;        // how far the sky has turned (15 degrees an hour)
    float latitude = 50.0f;             // the celestial pole's height above the north horizon
    std::uint32_t star_seed = 1;
    bool space = false;                 // no air: nothing below the horizon is hidden, no extinction
    math::Vec3 zenith_transmittance{1, 1, 1};  // the air straight up (dims stars and bodies by the air mass)
    // The moon's glow in the atmosphere sky: its direction and its light relative to the sun's (0: none).
    math::Vec3 moon_direction{0, 1, 0};
    float moon_relative = 0.0f;
};

// The shader handles this many local lights per frame (the nearest to the camera are kept).
inline constexpr std::uint32_t kMaxLocalLights = 16;

struct Lighting {
    math::Vec3 sun_direction = math::normalize(math::Vec3{-0.4f, -0.8f, -0.45f});  // the way sunlight travels
    math::Vec3 sun_color{1.0f, 0.96f, 0.9f};  // linear
    float sun_illuminance = 100000.0f;        // lux
    math::Vec3 sky_color{0.42f, 0.6f, 1.0f};     // linear; the background and the light from above
    float sky_luminance = 8000.0f;               // nits
    math::Vec3 ground_color{0.32f, 0.29f, 0.26f};  // linear, relative to the sky: light bounced from below
    float ev100 = 14.0f;
    Tonemapper tonemapper = Tonemapper::Neutral;
    // Where nothing is drawn: the sky (true), or the renderer's clear colour shown exactly as given
    // (not lit, exposed or tonemapped: tests and the editor's empty view).
    bool sky_background = true;
    // The environment (set_environment): nits per environment unit, and its turn about the vertical
    // axis (radians). Without an environment the renderer lights with a plain grey one.
    float environment_intensity = 8000.0f;
    float environment_rotation = 0.0f;
    // Draw the sun's disc in the sky (0.53 degrees across, from sun_color * sun_illuminance): on for
    // the atmosphere sky, whose background leaves the sun out.
    bool sun_disc = false;
    // Height fog (Milestone 8A, part 3; noor/render/fog.h): over everything drawn and the sky. Density 0: none.
    FogParams fog;
    // Cloud layers (Milestone 8A, part 4; noor/render/clouds.h): drawn in the sky, shading the sun on the
    // ground, lighting the scene through the live sky. Only with the atmosphere sky (set_atmosphere).
    std::array<CloudLayer, kCloudLayers> clouds{};
    // Seconds of game time: the clouds drift with it (the same time always gives the same sky).
    float time = 0.0f;
    // The sun's disc, the moon, planets and stars (part 5). With sun_disc, the disc is drawn at sky.sun_direction.
    SkyObjects sky;
    // Rain, snow and drifting dust (Milestone 8B, part 3, ADR-035): no simulation, each drop worked out in the vertex
    // shader from its number, `time` and the wind, in a box that follows the camera. Intensities 0..1 (how many of
    // the most drops there can be: rain 200,000, snow 60,000, dust 20,000); `wind` in m/s.
    struct Weather {
        float rain = 0.0f, snow = 0.0f, dust = 0.0f;
        math::Vec3 wind{0, 0, 0};
        std::uint32_t seed = 1;
    } weather;
    // Sun shadows (cascaded shadow maps): on/off and how far from the camera they reach.
    bool sun_shadows = true;
    float shadow_distance = 80.0f;
    std::vector<LocalLight> lights;  // point and spot lights

    // The multiplier from scene light to the HDR image: 1 / (1.2 * 2^EV100).
    [[nodiscard]] float exposure() const noexcept;
};

struct DrawItem {
    MeshHandle mesh;
    math::Mat4 world_from_object = math::Mat4::identity();
    // rgb = linear color, a = how much of it replaces the mesh's own colors (0 = none).
    math::Vec4 tint{0, 0, 0, 0};
    // Overlay items (editor gizmos) are drawn after tonemapping, in front of everything, in their
    // own display colours (not lit, not exposed). They still depth-test against each other.
    bool overlay = false;
    bool cast_shadow = true;
    MaterialHandle material;          // none: the default material
    // A skinned mesh (8C): world x inverse bind per joint of its skin. Its vertices are moved by these (the shader's
    // linear blend skinning) and world_from_object is ignored. The span must stay valid until the frame is rendered.
    std::span<const math::Mat4> skin_palette{};
    std::uint32_t first_index = 0;    // a range of the mesh's indices (a submesh);
    std::uint32_t index_count = 0;    // 0 = the whole mesh
    // Splatting (terrains, Milestone 6 part 7): up to four materials blended per pixel by the vertex
    // colour, which holds the weights of layers 1, 2 and 3 (layer 0, `material`, gets the rest).
    // Each layer's UVs are multiplied by its tiling before the material's own UV transform.
    bool splat = false;
    std::array<MaterialHandle, 3> splat_layers{};
    math::Vec4 splat_tiling{1, 1, 1, 1};
    // Order in the see-through pass (2D part 3, ADR-040): by layer (lower first, so behind), then by order (lower
    // first), then back to front by distance (along the view for an orthographic camera). 0 and 0: by distance alone,
    // as all see-through things were before. Opaque and cutout draws are depth-tested and ignore these.
    std::int32_t sort_layer = 0;
    float sort_order = 0.0f;
};

// The 2D overlay (HUD), drawn over the 3D scene with alpha blending. Triangle-list vertices in
// framebuffer pixels (origin top-left). uv.x < 0: a solid vertex (color only); otherwise uv samples
// the overlay atlas as a signed distance field (0.5 = edge), e.g. text.
struct OverlayVertex {
    math::Vec2 position;
    math::Vec2 uv;
    math::Vec4 color;  // linear RGB, straight alpha
};

// ---- Particles (Milestone 8B, part 2, ADR-035) ----------------------------------------------

// One particle as the GPU draws it: a quad facing the camera (or stretched along its velocity), in the
// see-through pass. 48 bytes, read per instance.
struct ParticleVertex {
    math::Vec3 position;            // world, metres
    float size = 0.1f;              // the quad's width, metres
    math::Vec4 color{1, 1, 1, 1};   // linear rgb, straight alpha
    math::Vec3 velocity;            // m/s (velocity-facing particles stretch along it)
    float frame = 0.0f;             // flipbook frame (0 .. frames - 1)
};
static_assert(sizeof(ParticleVertex) == 48);

enum class ParticleBlend : std::uint8_t {
    Alpha,          // smoke, dust: covers what's behind by its alpha (sort the particles back to front)
    Additive,       // fire, sparks, magic: adds light, never darkens (order doesn't matter)
    Premultiplied,  // a texture whose colour is already multiplied by its alpha (both at once: glowing smoke)
};
enum class ParticleFacing : std::uint8_t { Camera, Velocity };

// One emitter's particles, drawn with one instanced draw call.
struct ParticleDraw {
    std::span<const ParticleVertex> particles;  // Alpha: back to front for the camera
    TextureHandle texture;                       // none: a soft round dot
    std::uint32_t frames_x = 1, frames_y = 1;    // a flipbook grid in the texture (left to right, top to bottom)
    ParticleBlend blend = ParticleBlend::Alpha;
    ParticleFacing facing = ParticleFacing::Camera;
    bool lit = true;          // lit by the sun (with its shadows and the clouds') and the sky; false: only `emission`
    float emission = 0.0f;    // light of its own: nits per unit of colour (before exposure); fire about 2,000-20,000
    float stretch = 0.0f;     // Velocity facing: the quad stretches over this many seconds of motion
    math::Vec3 center;        // where it is, for ordering among see-through things and culling
    float radius = 0.0f;      // how far its particles reach from `center`
};

// ---- Water (Milestone 8D, part 1, ADR-037) ----------------------------------------------------

// How deep a water body is over its ground (8D part 3; game/water.h builds it from the terrains under it): a grid of
// samples in world x and z, `depth` = the water level minus the ground's height (negative: dry land), row by row (z
// outer). Read bilinearly between samples, the same arithmetic on the CPU and in the shader; off the grid the water is
// deep. Shared and immutable: the renderer keeps one texture per map it is given.
struct WaterDepthMap {
    math::Vec2 origin;          // world (x, z) of sample (0, 0)
    float cell = 1.0f;          // metres between samples
    std::uint32_t nx = 0, nz = 0;
    std::vector<float> depth;   // nx * nz
};

// A river's course (8D part 6; game/water.h builds it from the component's points): its centre line sampled about every
// metre, each sample with its surface's height (centre.y), half its width, how far along the river it is, which way the
// water runs there (unit, x and z) and how steeply the surface falls (metres per metre). Shared and immutable.
struct WaterPath {
    struct Sample {
        math::Vec3 centre;
        float half_width = 2.0f;
        float along = 0.0f;
        math::Vec2 tangent{1.0f, 0.0f};
        float slope = 0.0f;
    };
    std::vector<Sample> samples;  // at least 2
    float speed = 1.0f;           // m/s, downstream
};

// One water surface: a level rectangle (a lake's, a pond's), drawn over a copy of the opaque scene so it knows how deep
// it is at every pixel. Radiometric settings are physical-ish and art-directed (docs/water/WATER.md).
struct WaterDraw {
    math::Vec3 center;                              // the surface's middle, world (its y is the water level)
    math::Vec2 half_size{5.0f, 5.0f};               // metres along its own x and z
    float yaw = 0.0f;                               // turned about +Y (radians)
    math::Vec3 absorption{0.45f, 0.09f, 0.06f};     // per metre, linear rgb: red goes first, then green
    math::Vec3 scatter{0.02f, 0.05f, 0.06f};        // light scattered back from inside the water (an albedo)
    float refraction = 0.04f;                       // how far what's under is bent (screen fraction, at 1 m deep)
    float shore_fade = 0.12f;                       // metres of depth over which the water fades in at the shore
    float roughness = 0.06f;                        // blurs the sky's reflection and spreads the sun's glint
    float ripple = 1.0f;                            // small ripples' strength (0: a mirror)
    float ripple_scale = 1.2f;                      // metres per ripple
    double time = 0.0;                              // seconds (the ripples move with it)
    bool endless = false;                           // a sea: under the camera out to the horizon (center's y only;
                                                    // half_size and yaw are ignored)
    // Waves (8D part 2; game/water.h has the formula): the main wave's height (crest to trough, metres; 0: none), length
    // (metres), direction (radians towards which, 0 = -Z) and choppiness (0..0.9), and the four waves' phases at `time`
    // (radians; game::set_water_time works them out in double precision).
    float wave_height = 0.0f;
    float wave_length = 8.0f;
    float wave_direction = 0.0f;
    float choppiness = 0.5f;
    std::array<float, 4> wave_phase{};
    // Shorelines and reflections (8D part 3): the depth over the ground (waves calm where it's shallow; null: deep
    // everywhere), foam (overall, the shallow depth it gathers in, on steep crests), wet ground (how much darker, and
    // how high above the waterline), and the share of screen-space reflections (0: the sky only).
    std::shared_ptr<const WaterDepthMap> depth_map;
    float foam = 0.5f;
    float foam_depth = 0.3f;
    float crest_foam = 0.0f;
    float wet = 0.35f;
    float wet_height = 0.12f;
    float reflections = 1.0f;
    // Underwater (8D part 4): sunlight's caustics on what's under the water (0: none, 1: the default strength) and
    // light shafts in the water seen from inside it (0..1). The environment's underwater settings, if it has any,
    // replace the water's own while the camera is in this body (`underwater`).
    float caustics = 1.0f;
    float shafts = 0.5f;
    struct Underwater {
        math::Vec3 absorption{0.45f, 0.09f, 0.06f};
        math::Vec3 scatter{0.02f, 0.05f, 0.06f};
        float caustics = 1.0f;
        float shafts = 0.5f;
    };
    std::optional<Underwater> underwater;
    // The weather on it (8D part 5; drawing only, gameplay's surface doesn't change): rain (0..1) rings spreading from
    // drops, the ground wind (m/s) roughening its ripples and blurring its reflections.
    float rain = 0.0f;
    float wind_speed = 0.0f;
    // A river (8D part 6): a ribbon along this course instead of a box or a sea (center/half_size: its bounds).
    std::shared_ptr<const WaterPath> river;
};

struct FrameDesc;
// Under water (8D part 4): how many stops the exposure opens up with the camera this deep in a body (half the light
// the water has taken by then; 0 at the surface and out of water). render_frame applies it.
[[nodiscard]] float underwater_exposure_shift(const FrameDesc& frame);

struct FrameDesc {
    Camera camera;
    Lighting lighting;
    std::span<const DrawItem> draws;
    std::span<const OverlayVertex> overlay;
    std::span<const ParticleDraw> particles;  // Milestone 8B: drawn in the see-through pass, sorted with see-through meshes
    std::span<const WaterDraw> water;         // Milestone 8D: drawn first in the see-through pass, over the copied scene
    // Water's debug views (8D part 7): 0 none, 1 depth (how much water is under each pixel), 2 flow (which way and how
    // fast it runs), 3 waves (how far they lift the surface), 4 normals, 5 cost (how hard each pixel worked).
    std::uint32_t water_debug = 0;
};

struct FrameStats {
    std::uint32_t draw_calls = 0;  // GPU draw calls: one draws many instances (ADR-032)
    std::uint64_t triangles = 0;   // in the scene pass (all instances)
    std::uint64_t instances = 0;   // draw items drawn in the scene pass (after culling)
    std::uint32_t culled = 0;      // draw items skipped because they were outside the camera's view
    // CPU time spent recording and submitting the frame (excludes waiting for the GPU/vsync).
    double cpu_record_ms = 0.0;
    // GPU time of the most recent frame whose results are available (lags 1â€“2 frames).
    // Negative when timestamps are unsupported or not available yet.
    double gpu_ms = -1.0;
    // GPU time per pass of that frame, in order (Environment, Shadows, Scene, Tonemap, Gizmos, HUD); empty when unknown.
    struct Pass {
        const char* name = "";
        double ms = 0.0;
    };
    std::vector<Pass> passes;
    // Shadow-map draws (in draw_calls too) and casters skipped because they were outside a cascade.
    std::uint32_t shadow_draw_calls = 0;
    std::uint32_t shadow_draws_culled = 0;
    std::uint32_t transparent_draws = 0;  // see-through (blend) draws, sorted back to front
    std::uint32_t particle_draws = 0;     // emitters drawn (one draw call each), and their particles
    std::uint64_t particles = 0;
    std::uint64_t weather_drops = 0;      // rain, snow and dust drawn (8B part 3)
    std::uint32_t water_draws = 0;        // water surfaces drawn (8D); with any, the scene was copied for them
    std::uint32_t msaa_samples = 1;       // samples per pixel of the scene this frame
};

// A copy of a presented frame, read back from the GPU. Pixels are tightly packed
// 4 bytes each, in `format` byte order ("BGRA8_SRGB" or "RGBA8_SRGB").
struct CapturedFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string format;
    std::vector<std::uint8_t> pixels;
    // The scene's depth after the see-through pass (water included), one float a pixel (reverse-Z: 0 is infinitely
    // far), when asked for and drawn without MSAA (8D part 2: water's drawn height read back); empty otherwise.
    std::vector<float> depth;

    // Returns {r, g, b, a} in sRGB bytes regardless of the stored byte order.
    [[nodiscard]] std::array<std::uint8_t, 4> rgba_at(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] float depth_at(std::uint32_t x, std::uint32_t y) const {
        const std::size_t i = static_cast<std::size_t>(y) * width + x;
        return i < depth.size() ? depth[i] : 0.0f;
    }
};

class Renderer {
public:
    [[nodiscard]] static Result<std::unique_ptr<Renderer>> create(const platform::PresentationTarget& target,
                                                                  const RendererDesc& desc);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Uploads a mesh to GPU memory. Blocks until the upload finishes (fine for Milestone 1;
    // asynchronous streaming comes with the asset system).
    [[nodiscard]] Result<MeshHandle> create_mesh(const MeshData& data);
    void destroy_mesh(MeshHandle mesh);

    // Uploads a texture (all its mips) and gives it a slot in the shader's texture table. Blocks
    // until the upload finishes. Fails with a clear message when the table is full (4096).
    [[nodiscard]] Result<TextureHandle> create_texture(const TextureDesc& desc);
    void destroy_texture(TextureHandle texture);
    // GPU memory held by textures created with create_texture.
    [[nodiscard]] std::uint64_t texture_memory() const noexcept;
    // GPU memory held by meshes (vertex and index buffers).
    [[nodiscard]] std::uint64_t mesh_memory() const noexcept;
    // GPU memory of the frame's images: HDR scene, depth, MSAA images, shadow cascades.
    [[nodiscard]] std::uint64_t render_target_memory() const noexcept;
    // Scene anti-aliasing: 1, 2, 4 or 8 samples (lowered to what the GPU supports; applied from the
    // next frame). msaa_samples() is what is in use.
    void set_msaa(std::uint32_t samples) noexcept;
    [[nodiscard]] std::uint32_t msaa_samples() const noexcept;
    // The GPU can sample BC7/BC5 textures (every desktop GPU we target; false: decompress first).
    [[nodiscard]] bool supports_block_compression() const noexcept;
    // A magenta-and-black checker for textures that couldn't be loaded (visible on purpose).
    [[nodiscard]] TextureHandle missing_texture() const noexcept;
    // Sets the environment that lights the scene and shows behind it. Waits for the GPU (it replaces
    // what frames in flight use): call it when the environment changes, not every frame.
    [[nodiscard]] Status set_environment(const EnvironmentDesc& desc);
    // Makes the environment a live atmosphere sky, built on the GPU: the background, the reflections and
    // the light from all around follow the sun. Call it every frame with the current sun: it only
    // records work when something changed (the sun: the sky and its lighting; the settings: the tables
    // too). The first call waits for the GPU; set_environment switches back to a fixed environment.
    [[nodiscard]] Status set_atmosphere(const AtmosphereDesc& desc);
    // How many times the live sky was rebuilt (its tables, the sky and its lighting): tests and stats.
    [[nodiscard]] std::uint64_t atmosphere_table_builds() const noexcept;
    [[nodiscard]] std::uint64_t atmosphere_sky_builds() const noexcept;

    // Replaces a texture's pixels, keeping its handle (so materials using it follow). Waits for the GPU.
    [[nodiscard]] Status update_texture(TextureHandle texture, const TextureDesc& desc);

    // Materials live in a table of 4096 the shader reads by index. Updating one takes effect
    // from the next frame; a frame already in flight may still see the old values.
    [[nodiscard]] Result<MaterialHandle> create_material(const MaterialDesc& desc);
    void update_material(MaterialHandle material, const MaterialDesc& desc);
    void destroy_material(MaterialHandle material);

    // Draws `frame.draws` from `frame.camera`, lit by `frame.lighting`, then the gizmos and the HUD.
    [[nodiscard]] Result<FrameResult> render_frame(const FrameDesc& frame);
    // Clears to the clear colour only.
    [[nodiscard]] Result<FrameResult> render_frame() {
        FrameDesc empty;
        empty.lighting.sky_background = false;
        return render_frame(empty);
    }

    // Asks the renderer to copy the next presented frame to CPU memory (and its depth: only without MSAA).
    void request_capture(bool with_depth = false) noexcept;
    // Returns the captured frame once it is available (waits for the GPU if needed),
    // or nothing if no capture was requested/completed yet.
    [[nodiscard]] Result<std::optional<CapturedFrame>> take_capture();

    // Call when the window's pixel size changed; the swapchain is rebuilt before the next frame.
    void notify_resized() noexcept;

    void set_clear_color(const std::array<float, 4>& linear_rgba) noexcept;

    // The overlay's texture: one R8 image (width x height bytes, row by row), e.g. a font's
    // distance-field glyphs. Replacing it waits for the GPU; do it at start, not every frame.
    [[nodiscard]] Status set_overlay_atlas(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> r8);

    [[nodiscard]] const GpuInfo& gpu_info() const noexcept;
    [[nodiscard]] const FrameStats& last_frame_stats() const noexcept;
    [[nodiscard]] bool validation_enabled() const noexcept;
    [[nodiscard]] std::uint64_t validation_error_count() const noexcept;
    [[nodiscard]] std::uint64_t validation_warning_count() const noexcept;
    [[nodiscard]] std::uint64_t frames_presented() const noexcept;
    [[nodiscard]] std::uint64_t swapchain_rebuilds() const noexcept;
    // Size of the images currently being presented (zero before the first swapchain).
    [[nodiscard]] platform::PixelSize swapchain_size() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::render
