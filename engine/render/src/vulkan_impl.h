#pragma once
// Internal to noor::render. Shared state of the Vulkan renderer, split across:
//   vulkan_renderer.cpp  instance, device, swapchain, frame loop, teardown
//   vulkan_resources.cpp GPU memory (VMA), meshes, the mesh pipeline
//   vulkan_capture.cpp   frame readback for automated tests

#include "noor/foundation/log.h"
#include "noor/render/renderer.h"

#include <array>
#include <atomic>
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

struct GpuMesh {
    GpuBuffer vertices;
    GpuBuffer indices;
    std::uint32_t index_count = 0;
    bool alive = false;
};

// Push constants for mesh.hlsl (must match its PushConstants struct; 128 bytes = guaranteed minimum).
struct MeshPushConstants {
    math::Mat4 clip_from_object;
    math::Mat4 world_from_object;
};
static_assert(sizeof(MeshPushConstants) == 128);
static_assert(sizeof(Vertex) == 36, "mesh pipeline vertex layout assumes 3 x float3");

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

    // Mesh pipeline
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline mesh_pipeline = VK_NULL_HANDLE;
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    std::array<FrameSync, kFramesInFlight> frames{};
    std::uint32_t frame_index = 0;

    // GPU timestamps: two queries (start, end) per frame in flight.
    VkQueryPool timestamp_pool = VK_NULL_HANDLE;
    double timestamp_period_ns = 0.0;
    bool timestamps_supported = false;
    std::array<bool, kFramesInFlight> timestamps_written{};

    std::vector<GpuMesh> meshes;  // MeshHandle.id = index + 1
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
    void destroy_mesh_pipeline();
    Result<GpuBuffer> create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible, bool readback);
    void destroy_buffer(GpuBuffer& buffer);
    Status upload_to_device(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer& out);
    Result<MeshHandle> create_mesh(const MeshData& data);
    void destroy_mesh(MeshHandle handle);
    void record_draws(VkCommandBuffer cmd, const FrameDesc& frame);

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
