#pragma once
// Internal to noor::render. Shared state of the Vulkan renderer, split across:
//   vulkan_renderer.cpp  instance, device, swapchain, frame loop, teardown
//   vulkan_resources.cpp GPU memory (VMA), meshes, the mesh pipeline
//   vulkan_capture.cpp   frame readback for automated tests
//   vulkan_frame.cpp     the HDR image, per-frame lighting uniforms, tonemapping (ADR-012)
//   vulkan_textures.cpp  textures and the bindless texture table (ADR-029)
//   vulkan_overlay.cpp   the HUD

#include "noor/foundation/log.h"
#include "noor/render/renderer.h"
#include "noor/render/shadows.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <cstdint>
#include <format>
#include <limits>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <vk_mem_alloc.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define NOOR_VK_TRY(expr)                                                                          \
    do {                                                                                           \
        if (const VkResult vk_result_ = (expr); vk_result_ != VK_SUCCESS) {                        \
            return ::noor::render::vk_failure(vk_result_, #expr);                                  \
        }                                                                                          \
    } while (false)

namespace noor::render {

// Turns a failed VkResult into an Error. A lost device gets its own code and a plain-language
// message: it means the driver reset or crashed, and every GPU object must be recreated.
[[nodiscard]] inline std::unexpected<Error> vk_failure(VkResult result, const char* what,
                                                      std::source_location where = std::source_location::current()) {
    if (result == VK_ERROR_DEVICE_LOST) {
        return fail(ErrorCode::DeviceLost,
                    std::format("GPU device lost during {} (driver reset, GPU crash or hang). Recovery is not "
                                "implemented yet: restart the application. If it repeats, capture the frame "
                                "with RenderDoc and check the Vulkan log.", what),
                    where);
    }
    return fail(ErrorCode::GraphicsError, std::format("{} failed ({})", what, string_VkResult(result)), where);
}

inline constexpr std::uint32_t kFramesInFlight = 2;
inline constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
inline constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // the lit scene, before tonemapping
// GPU timestamps per frame: start, after shadows, after the scene, after tonemapping, after the gizmos, end.
inline constexpr std::uint32_t kTimestampsPerFrame = 7;
// The aerial perspective volume (Milestone 8A, part 3): 32^3 froxels over the camera's view, slices
// spaced by the square root of the distance out to this far end (km of air, after the distance scale).
inline constexpr std::uint32_t kAerialSize = 32;
inline constexpr float kAerialFarKm = 32.0f;
inline constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;
// The bindless texture table (set 1): slots 0..2 are built in.
inline constexpr std::uint32_t kMaxTextures = 4096;
inline constexpr std::uint32_t kTextureWhite = 0;
inline constexpr std::uint32_t kTextureFlatNormal = 1;
inline constexpr std::uint32_t kTextureMissing = 2;
inline constexpr std::uint32_t kTextureBrdfLut = 3;  // image-based lighting's lookup table
inline constexpr std::uint32_t kFirstUserTexture = 4;
inline constexpr std::uint32_t kAnySlot = ~0u;  // create_texture: the first free user slot
// The material table (set 1, binding 2): slot 0 is the default material.
inline constexpr std::uint32_t kMaxMaterials = 4096;

struct FrameSync {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
};

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;  // only for host-visible buffers
};

struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::uint64_t bytes = 0;
    bool alive = false;
};

struct GpuMesh {
    GpuBuffer vertices;
    GpuBuffer indices;
    std::uint32_t index_count = 0;
    std::uint64_t bytes = 0;
    math::Vec3 center;    // bounding sphere in object space (shadow culling, transparent sorting)
    float radius = 0.0f;
    bool alive = false;
};

// Which draws record_draws records.
enum class DrawPass : std::uint8_t { Opaque, Transparent, Gizmos };

// One instance of a draw (ADR-032), in the per-frame instance buffer (vertex binding 1, per
// instance): the model matrix's top three rows (rotation/scale and translation) and the tint.
struct GpuInstance {
    math::Vec4 row0;
    math::Vec4 row1;
    math::Vec4 row2;
    math::Vec4 tint;  // rgb = linear color, a = blend amount (0 = mesh colors)
};
static_assert(sizeof(GpuInstance) == 64);

// Push constants for mesh.hlsl and shadow.hlsl (must match their PushConstants struct): what a whole
// batch shares. The camera's clip_from_world is in FrameUniforms; matrices come per instance.
struct MeshPushConstants {
    // x: material slot (splat layer 0). Scene pass: y = 0x80000000 | layer 1's slot for a splat (else 0),
    // z, w: layers 2 and 3. Shadow pass: y = cascade.
    std::uint32_t ids[4];
    math::Vec4 splat_tiling;    // UV multiplier per splat layer
};
static_assert(sizeof(MeshPushConstants) == 32);

// One GPU draw of `instance_count` instances of a mesh range (vulkan_instancing.cpp).
struct DrawBatch {
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::uint32_t mesh = 0;  // MeshHandle id
    std::uint32_t first = 0, count = 0;
    std::uint32_t ids[4]{};
    math::Vec4 splat_tiling{1, 1, 1, 1};
    std::uint32_t instance_offset = 0, instance_count = 0;
};

// mesh.hlsl's GpuMaterial (std430), one per material slot.
struct GpuMaterial {
    math::Vec4 base_color;       // linear rgb, alpha
    math::Vec4 emissive_cutoff;  // emissive rgb, alpha cutoff
    math::Vec4 params;           // metallic, roughness, normal scale, occlusion strength
    math::Vec4 uv;               // scale xy, offset xy
    std::uint32_t tex0[4];       // base colour, metallic-roughness, roughness, metallic
    std::uint32_t tex1[4];       // normal, occlusion, emissive, flags (bit 0 cutout, bit 1 blend, bit 2 double-sided)
};
static_assert(sizeof(GpuMaterial) == 96);
static_assert(sizeof(Vertex) == 60, "mesh pipeline vertex layout: float3 position, normal, color, float2 uv, float4 tangent");

// A point or spot light as the shader sees it (mesh.hlsl GpuLight).
struct GpuLight {
    math::Vec4 position_range;   // xyz world position, w range (m)
    math::Vec4 intensity;        // rgb: colour * candela * exposure
    math::Vec4 direction_type;   // xyz spot direction, w: 0 point, 1 spot
    math::Vec4 cone;             // x cos(outer), y cos(inner)
};

// mesh.hlsl's FrameUniforms (set 0, binding 0), one buffer per frame in flight. Radiances are pre-exposed.
struct FrameUniforms {
    math::Mat4 clip_from_world;
    math::Vec4 camera_position;
    math::Vec4 to_sun;
    math::Vec4 sun_radiance;
    math::Vec4 sky_radiance;
    math::Vec4 ground_radiance;
    math::Mat4 cascades[kShadowCascades];  // world -> shadow map clip space
    math::Vec4 cascade_splits;             // far end of each cascade (metres along the view)
    math::Vec4 cascade_texel;              // texel size in metres per cascade
    math::Vec4 shadow_params;              // x: on, y: shadow map size, z: shadow distance
    math::Vec4 camera_forward;
    std::uint32_t light_count[4];
    GpuLight lights[kMaxLocalLights];
    math::Mat4 world_from_clip;          // the sky's view rays
    math::Vec4 environment;              // x: intensity * exposure, y: cos(rotation), z: sin(rotation), w: last mip
    std::uint32_t background[4];         // x: background texture slot, y: sun disc
    math::Vec4 fog;                      // x density, y falloff, z height, w anisotropy (fog.h)
    math::Vec4 fog_albedo;               // rgb
    math::Vec4 aerial;                   // x: 1 = the aerial perspective volume is live, y: its far end (km), z: distance scale
};

// atmosphere.hlsl's push constants (Params): the atmosphere and what this pass works on.
struct LiveSkyPush {
    math::Vec4 rayleigh, mie, ozone, planet, ground, sun;
    std::uint32_t pass[4];
    math::Vec4 extra;
};
static_assert(sizeof(LiveSkyPush) == 128, "push constants: the guaranteed minimum is 128 bytes");

struct PendingCapture {
    GpuBuffer buffer;
    std::uint32_t frame_slot = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool pending = false;
};

struct Renderer::Impl {
    const platform::PresentationTarget* target = nullptr;
    RendererDesc desc;
    bool validation = false;

    // VK_EXT_debug_utils: object names and command labels for RenderDoc/Nsight.
    bool debug_utils = false;
    PFN_vkSetDebugUtilsObjectNameEXT pfn_set_object_name = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT pfn_begin_label = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT pfn_end_label = nullptr;

    template <class Handle>
    void set_name(VkObjectType type, Handle handle, const std::string& name) const {
        if (!pfn_set_object_name || device == VK_NULL_HANDLE || handle == VK_NULL_HANDLE) return;
        VkDebugUtilsObjectNameInfoEXT name_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        name_info.objectType = type;
        name_info.objectHandle = reinterpret_cast<std::uint64_t>(handle);
        name_info.pObjectName = name.c_str();
        pfn_set_object_name(device, &name_info);
    }
    void begin_label(VkCommandBuffer cmd, const char* name, std::array<float, 4> color) const {
        if (!pfn_begin_label) return;
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        std::copy(color.begin(), color.end(), label.color);
        pfn_begin_label(cmd, &label);
    }
    void end_label(VkCommandBuffer cmd) const {
        if (pfn_end_label) pfn_end_label(cmd);
    }

    // Core objects
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool upload_pool = VK_NULL_HANDLE;

    // Swapchain and per-image resources
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format{};
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    std::vector<VkSemaphore> render_finished;  // one per swapchain image
    bool swapchain_supports_capture = false;
    bool swapchain_dirty = true;    // must rebuild (out of date, suboptimal, or first frame)
    bool resize_requested = false;  // window says its size changed; rebuild only if it really did

    // Depth buffer (one, shared by frames in flight; barriers serialize its use on the queue)
    VkImage depth_image = VK_NULL_HANDLE;
    VmaAllocation depth_allocation = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;

    // MSAA (when scene_samples > 1): the scene is drawn into these, then resolved into hdr_image
    VkImage msaa_color_image = VK_NULL_HANDLE;
    VmaAllocation msaa_color_allocation = VK_NULL_HANDLE;
    VkImageView msaa_color_view = VK_NULL_HANDLE;
    VkImage msaa_depth_image = VK_NULL_HANDLE;
    VmaAllocation msaa_depth_allocation = VK_NULL_HANDLE;
    VkImageView msaa_depth_view = VK_NULL_HANDLE;
    std::uint32_t msaa_requested = 4;     // RendererDesc::msaa_samples / set_msaa
    std::uint32_t scene_samples = 0;      // in use (0 until the first swapchain)
    VkSampleCountFlags supported_samples = VK_SAMPLE_COUNT_1_BIT;

    // HDR image the scene is lit into (one, shared by frames in flight like the depth buffer)
    VkImage hdr_image = VK_NULL_HANDLE;
    VmaAllocation hdr_allocation = VK_NULL_HANDLE;
    VkImageView hdr_view = VK_NULL_HANDLE;

    // Sun shadows (vulkan_shadows.cpp): a 2D array, one layer per cascade
    VkImage shadow_image = VK_NULL_HANDLE;
    VmaAllocation shadow_allocation = VK_NULL_HANDLE;
    VkImageView shadow_view = VK_NULL_HANDLE;  // all layers, for sampling
    std::array<VkImageView, kShadowCascades> shadow_layer_views{};
    VkSampler shadow_sampler = VK_NULL_HANDLE;  // comparison
    VkPipeline shadow_pipeline = VK_NULL_HANDLE;
    std::uint32_t shadow_size = 0;
    std::array<ShadowCascade, kShadowCascades> cascades{};  // this frame's

    // The environment (vulkan_environment.cpp): prefiltered cube (frame set, binding 2), SH, background
    struct Environment {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        std::uint32_t mip_count = 1;
        TextureHandle background;
    } environment;
    VkPipeline sky_pipeline = VK_NULL_HANDLE;
    // The environment's irradiance (SH9, 9 float4) for the scene (frame set, binding 3): uploaded by
    // set_environment, written by the live sky's compute shader.
    GpuBuffer irradiance_buffer;
    // The aerial perspective volume the scene samples when there's no live sky (frame set binding 4): one
    // texel that adds nothing and lets everything through.
    struct AerialDefault {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;  // linear, clamped (the live volumes use it too)
    } aerial_default;
    // This frame's camera, kept by update_frame_uniforms for the aerial perspective pass.
    math::Mat4 frame_world_from_clip = math::Mat4::identity();
    math::Vec3 frame_eye{0, 0, 0};

    // The live sky (vulkan_atmosphere.cpp, Milestone 8A part 2): the atmosphere's tables, the sky image
    // and the environment's cube and irradiance, rebuilt by compute shaders when the sun moves.
    struct LiveSky {
        struct Image {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;   // all levels (sampling)
            std::vector<VkImageView> levels;     // one per level (writing)
            std::uint32_t mips = 1, layers = 1;
        };
        Image transmittance, multiple;  // shared: written only when the settings change
        // What the scene reads, one copy per frame in flight: a frame rebuilds its own copy while the
        // previous frame may still be drawing with the other, so no frame waits for another (a shared copy
        // made each frame's compute wait for the previous frame's presentation: half the frame rate).
        struct Copy {
            Image sky, cube;
            Image aerial;                // the camera's aerial perspective (32^3, rebuilt every frame)
            GpuBuffer aerial_camera;     // host-visible: the camera the aerial volume is built for
            VkDescriptorSet aerial_set = VK_NULL_HANDLE;
            GpuBuffer irradiance;   // SH9 (frame set binding 3)
            std::uint32_t sky_slot = 0;  // the sky image's slot in the texture table
            VkDescriptorSet sky_set = VK_NULL_HANDLE, irradiance_set = VK_NULL_HANDLE;
            std::vector<VkDescriptorSet> downsample_sets, cube_sets;
            bool built = false;
            AtmosphereDesc built_for;    // the settings and sun it shows
        };
        std::array<Copy, kFramesInFlight> copies;
        VkSampler table_sampler = VK_NULL_HANDLE, sky_sampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline transmittance_pipeline = VK_NULL_HANDLE, multiple_pipeline = VK_NULL_HANDLE, sky_pipeline = VK_NULL_HANDLE,
                   downsample_pipeline = VK_NULL_HANDLE, cube_pipeline = VK_NULL_HANDLE, irradiance_pipeline = VK_NULL_HANDLE,
                   aerial_pipeline = VK_NULL_HANDLE;
        VkDescriptorSet transmittance_set = VK_NULL_HANDLE, multiple_set = VK_NULL_HANDLE;
        bool active = false;         // the environment is this (set_environment turns it off)
        bool has_desc = false;
        AtmosphereDesc desc;         // what the scene wants now
        bool tables_built = false;
        AtmosphereDesc tables_for;   // what the tables were built for
        std::uint64_t table_builds = 0, sky_builds = 0;
    } live_sky;

    // Per-frame lighting uniforms (vulkan_frame.cpp)
    VkDescriptorSetLayout frame_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool frame_pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> frame_sets{};
    std::array<GpuBuffer, kFramesInFlight> frame_ubos{};

    // The bindless texture table (vulkan_textures.cpp): set 1, binding 0 = images, binding 1 = sampler
    VkDescriptorSetLayout texture_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool texture_pool = VK_NULL_HANDLE;
    VkDescriptorSet texture_set = VK_NULL_HANDLE;
    VkSampler texture_sampler = VK_NULL_HANDLE;
    std::vector<GpuTexture> textures;  // index = slot
    std::uint64_t texture_bytes = 0;   // user textures only
    float max_anisotropy = 1.0f;
    bool bc_supported = false;  // textureCompressionBC
    // Materials (same set, binding 2): a host-visible storage buffer, and CPU copies for choosing pipelines.
    GpuBuffer material_buffer;
    std::vector<MaterialDesc> materials;  // index = slot
    std::vector<bool> material_alive;

    // Mesh pipelines: the lit scene (into the HDR image) and editor gizmos (into the screen)
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline mesh_pipeline = VK_NULL_HANDLE;
    VkPipeline gizmo_pipeline = VK_NULL_HANDLE;
    VkPipeline double_sided_pipeline = VK_NULL_HANDLE;  // the scene, without back-face culling
    VkPipeline blend_pipeline = VK_NULL_HANDLE;         // see-through: blended, no depth writes
    VkPipeline blend_double_sided_pipeline = VK_NULL_HANDLE;
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;  // the screen's, for the gizmo pipeline

    // Tonemapping: HDR image -> screen (vulkan_frame.cpp)
    struct Tonemap {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkFormat color_format = VK_FORMAT_UNDEFINED;
    } tonemap;

    // Overlay (HUD): vulkan_overlay.cpp
    struct Overlay {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkFormat color_format = VK_FORMAT_UNDEFINED;
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImage atlas_image = VK_NULL_HANDLE;
        VmaAllocation atlas_allocation = VK_NULL_HANDLE;
        VkImageView atlas_view = VK_NULL_HANDLE;
        std::array<GpuBuffer, kFramesInFlight> vertices{};
        std::array<std::size_t, kFramesInFlight> capacity{};
    } overlay;

    std::array<FrameSync, kFramesInFlight> frames{};
    std::uint32_t frame_index = 0;

    // GPU timestamps: kTimestampsPerFrame queries per frame in flight.
    VkQueryPool timestamp_pool = VK_NULL_HANDLE;
    double timestamp_period_ns = 0.0;
    bool timestamps_supported = false;
    std::array<bool, kFramesInFlight> timestamps_written{};

    std::vector<GpuMesh> meshes;  // MeshHandle.id = index + 1

    // Instanced drawing (vulkan_instancing.cpp, ADR-032): this frame's batches and their instances.
    using Batch = DrawBatch;
    struct FramePlan {
        std::vector<GpuInstance> instances;
        std::vector<Batch> opaque, transparent, gizmos;
        std::array<std::vector<Batch>, kShadowCascades> shadows;
    } plan;
    std::array<GpuBuffer, kFramesInFlight> instance_buffers{};
    std::array<std::size_t, kFramesInFlight> instance_capacity{};
    bool capture_requested = false;
    PendingCapture capture;

    GpuInfo info;
    FrameStats stats;
    std::atomic<std::uint64_t> validation_errors{0};
    std::atomic<std::uint64_t> validation_warnings{0};
    std::uint64_t frames_presented = 0;
    std::uint64_t swapchain_rebuilds = 0;

    ~Impl() { destroy(); }

    // vulkan_renderer.cpp
    Status create_instance();
    Status pick_gpu();
    Status create_device();
    Status create_frames();
    Status rebuild_swapchain();
    void destroy_swapchain_resources();
    Result<FrameResult> render_frame(const FrameDesc& frame);
    void destroy();

    // vulkan_resources.cpp
    Status create_allocator();
    Status create_depth_buffer();
    void destroy_depth_buffer();
    Status ensure_mesh_pipeline(VkFormat color_format);
    VkResult create_mesh_pipeline(VkFormat color_format, bool display, VkPipeline& out, bool cull_back = true, bool blended = false);
    Status create_msaa_targets();
    void destroy_msaa_targets();
    void destroy_scene_pipelines();  // the ones drawn into the (multisampled) scene: rebuilt when samples change
    // vulkan_frame.cpp
    Status create_hdr_target();
    void destroy_hdr_target();
    Status ensure_frame_uniforms();
    Status create_aerial_default();
    void write_aerial_descriptor(std::uint32_t frame_slot, VkImageView view, VkImageLayout layout);
    Status ensure_tonemap(VkFormat color_format);
    void write_tonemap_descriptor();
    void update_frame_uniforms(const FrameDesc& frame);
    void record_tonemap(VkCommandBuffer cmd, const Lighting& lighting);
    void destroy_frame_resources();
    // vulkan_environment.cpp
    Status create_brdf_lut();
    Status upload_environment(const EnvironmentDesc& desc);
    Status ensure_default_environment();
    Status ensure_sky_pipeline();
    void record_sky(VkCommandBuffer cmd, const Lighting& lighting);
    void destroy_environment_image();
    void destroy_environment();
    // vulkan_atmosphere.cpp
    Status create_live_image(LiveSky::Image& out, VkFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t mips,
                             bool cube, const char* name);
    Status ensure_live_sky();
    Status set_atmosphere(const AtmosphereDesc& desc);
    void record_live_sky(VkCommandBuffer cmd);
    // The sky image, its pyramid, the cube and the irradiance for this frame's copy.
    void record_live_sky_lighting(VkCommandBuffer cmd, LiveSkyPush& push);
    void destroy_live_sky();
    // vulkan_shadows.cpp
    Status ensure_shadows();
    void record_shadows(VkCommandBuffer cmd, const FrameDesc& frame);
    void destroy_shadows();
    // vulkan_textures.cpp
    Status ensure_texture_table();
    Result<TextureHandle> create_texture(const TextureDesc& texture, std::uint32_t slot = kAnySlot);
    void destroy_texture(TextureHandle handle);
    void destroy_textures();
    void write_material(std::uint32_t slot, const MaterialDesc& desc);
    // vulkan_overlay.cpp
    Status ensure_overlay(VkFormat color_format);
    Status upload_overlay_atlas(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> r8);
    Status record_overlay(VkCommandBuffer cmd, std::span<const OverlayVertex> vertices);
    void destroy_overlay();
    void destroy_mesh_pipeline();
    Result<GpuBuffer> create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible, bool readback);
    void destroy_buffer(GpuBuffer& buffer);
    Status upload_to_device(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer& out);
    Result<MeshHandle> create_mesh(const MeshData& data);
    void destroy_mesh(MeshHandle handle);
    // The scene's opaque draws, its see-through ones (sorted back to front), or the editor gizmos.
    void record_draws(VkCommandBuffer cmd, const FrameDesc& frame, DrawPass pass);
    // vulkan_instancing.cpp: culling and batching (call after update_frame_uniforms: it needs the cascades).
    Status plan_frame(const FrameDesc& frame);
    void record_batches(VkCommandBuffer cmd, std::span<const Batch> batches, bool shadow);
    void destroy_instance_buffers();
    [[nodiscard]] std::uint32_t material_slot(const DrawItem& item) const noexcept;
    [[nodiscard]] std::uint32_t material_slot(MaterialHandle material) const noexcept;

    // vulkan_capture.cpp
    Status record_capture(VkCommandBuffer cmd, VkImage image);
    Result<std::optional<CapturedFrame>> take_capture();

    static VKAPI_ATTR VkBool32 VKAPI_CALL on_validation_message(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);
};

// Small helper for layout transitions with synchronization2.
inline void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout,
                          VkImageLayout new_layout, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                          VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace noor::render
