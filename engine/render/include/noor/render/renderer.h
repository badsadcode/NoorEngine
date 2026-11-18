#pragma once
// NoorEngine renderer: Vulkan 1.3 device, swapchain, GPU meshes, a camera and frame capture for
// automated tests. Milestone 5 (ADR-012): the scene is lit by a sun and a sky in physical units
// into a 16-bit float HDR image, then exposed, tonemapped and drawn to the screen; editor gizmos
// and the HUD go on top after tonemapping, in display colours.
// Vulkan types are hidden behind a pimpl (ADR-002). Conventions: ADR-022.

#include "noor/foundation/error.h"
#include "noor/foundation/math.h"
#include "noor/platform/presentation_target.h"

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

struct MeshData {
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;  // triangle list, counter-clockwise front faces
    std::string_view debug_name;             // shown in RenderDoc/Nsight; optional
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
    Blend,   // see-through blending is not implemented yet: drawn like Cutout (logged once)
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
};

// A point or spot light (Milestone 5, part 4). Intensity is luminous power in lumens, like a
// bulb's box says (a 60 W-equivalent bulb is about 800 lm): a point light sends it everywhere, a
// spot into its cone (so a narrower spot of the same lumens is brighter, like a torch). Light
// fades with the square of the distance and smoothly reaches zero at `range`.
struct LocalLight {
    enum class Type : std::uint8_t { Point, Spot } type = Type::Point;
    math::Vec3 position{0, 0, 0};
    math::Vec3 direction{0, -1, 0};  // spot: the way it points
    math::Vec3 color{1, 1, 1};       // linear
    float lumens = 800.0f;
    float range = 10.0f;             // metres
    float outer_angle = 0.6f;        // spot: half-angle of the cone, radians
    float inner_angle = 0.45f;       // spot: full brightness inside this half-angle
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
    std::uint32_t first_index = 0;    // a range of the mesh's indices (a submesh);
    std::uint32_t index_count = 0;    // 0 = the whole mesh
    // Splatting (terrains, Milestone 6 part 7): up to four materials blended per pixel by the vertex
    // colour, which holds the weights of layers 1, 2 and 3 (layer 0, `material`, gets the rest).
    // Each layer's UVs are multiplied by its tiling before the material's own UV transform.
    bool splat = false;
    std::array<MaterialHandle, 3> splat_layers{};
    math::Vec4 splat_tiling{1, 1, 1, 1};
};

// The 2D overlay (HUD), drawn over the 3D scene with alpha blending. Triangle-list vertices in
// framebuffer pixels (origin top-left). uv.x < 0: a solid vertex (color only); otherwise uv samples
// the overlay atlas as a signed distance field (0.5 = edge), e.g. text.
struct OverlayVertex {
    math::Vec2 position;
    math::Vec2 uv;
    math::Vec4 color;  // linear RGB, straight alpha
};

struct FrameDesc {
    Camera camera;
    Lighting lighting;
    std::span<const DrawItem> draws;
    std::span<const OverlayVertex> overlay;
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
    std::uint32_t msaa_samples = 1;       // samples per pixel of the scene this frame
};

// A copy of a presented frame, read back from the GPU. Pixels are tightly packed
// 4 bytes each, in `format` byte order ("BGRA8_SRGB" or "RGBA8_SRGB").
struct CapturedFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string format;
    std::vector<std::uint8_t> pixels;

    // Returns {r, g, b, a} in sRGB bytes regardless of the stored byte order.
    [[nodiscard]] std::array<std::uint8_t, 4> rgba_at(std::uint32_t x, std::uint32_t y) const;
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

    // Asks the renderer to copy the next presented frame to CPU memory.
    void request_capture() noexcept;
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
