// Vulkan renderer core: instance, device, swapchain, frame loop, teardown.
// Resources (meshes, pipeline, memory) live in vulkan_resources.cpp, readback in vulkan_capture.cpp.

#include "vulkan_impl.h"

#include "noor/platform/presentation_target.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>

namespace noor::render {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

std::string version_string(std::uint32_t v) {
    return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
}

std::string_view device_type_name(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

bool has_layer(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::ranges::any_of(layers, [&](const auto& l) { return std::strcmp(l.layerName, name) == 0; });
}

bool has_device_extension(VkPhysicalDevice gpu, const char* name) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, exts.data());
    return std::ranges::any_of(exts, [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
}

} // namespace

// Validation errors (our API misuse) are logged as errors and fail the smoke test.
// Errors of other types come from the loader/environment, e.g. a third-party implicit
// layer such as an overlay failing to load; they are real but not our bug, so they are
// logged as warnings with a "[loader]" prefix and counted as warnings.
VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::Impl::on_validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    {
        auto* self = static_cast<Impl*>(user);
        const char* text = data && data->pMessage ? data->pMessage : "(no message)";
        const bool is_validation = (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0;
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) && is_validation) {
            self->validation_errors.fetch_add(1);
            NOOR_LOG_ERROR("Vulkan", "{}", text);
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            self->validation_warnings.fetch_add(1);
            NOOR_LOG_WARN("Vulkan", "[loader] {}", text);
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            self->validation_warnings.fetch_add(1);
            NOOR_LOG_WARN("Vulkan", "{}", text);
        } else {
            NOOR_LOG_DEBUG("Vulkan", "{}", text);
        }
        return VK_FALSE;
    }
}

// ------------------------------------------------------------------ instance

Status Renderer::Impl::create_instance() {
    std::uint32_t loader_version = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&loader_version);
    if (loader_version < VK_API_VERSION_1_3) {
        return fail(ErrorCode::Unsupported,
                    std::format("Vulkan loader is {}, NoorEngine needs 1.3+", version_string(loader_version)));
    }

    auto platform_exts = target->vulkan_instance_extensions();
    if (!platform_exts) {
        return std::unexpected(platform_exts.error());
    }
    std::vector<const char*> extensions = *platform_exts;
    std::vector<const char*> layers;

    validation = desc.enable_validation && has_layer(kValidationLayer);
    if (desc.enable_validation && !validation) {
        NOOR_LOG_WARN("Render", "Validation requested but {} is not installed; continuing without it",
                      kValidationLayer);
    }
    if (validation) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "NoorEngine";
    app.pEngineName = "NoorEngine";
    app.engineVersion = VK_MAKE_API_VERSION(0, 0, 0, 1);
    app.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT messenger_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_info.pfnUserCallback = &Impl::on_validation_message;
    messenger_info.pUserData = this;

    VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();
    if (validation) {
        create_info.pNext = &messenger_info;  // also catches messages during instance creation/destruction
    }
    NOOR_VK_TRY(vkCreateInstance(&create_info, nullptr, &instance));

    if (validation) {
        auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create_messenger) {
            NOOR_VK_TRY(create_messenger(instance, &messenger_info, nullptr, &messenger));
        }
    }

    NOOR_LOG_INFO("Render", "Vulkan instance created (loader {}, validation {})", version_string(loader_version),
                  validation ? "on" : "off");

    auto surf = target->create_vulkan_surface(instance);
    if (!surf) {
        return std::unexpected(surf.error());
    }
    surface = *surf;
    return {};
}

// ------------------------------------------------------------------ device

Status Renderer::Impl::pick_gpu() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(instance, &count, gpus.data());

    int best_score = -1;
    for (VkPhysicalDevice candidate : gpus) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);

        const auto reject = [&](std::string_view why) {
            NOOR_LOG_INFO("Render", "GPU '{}' rejected: {}", props.deviceName, why);
        };

        if (props.apiVersion < VK_API_VERSION_1_3) {
            reject("Vulkan 1.3 not supported");
            continue;
        }
        if (!has_device_extension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            reject("no swapchain extension");
            continue;
        }

        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(candidate, &f2);
        if (!f13.synchronization2 || !f13.dynamicRendering) {
            reject("synchronization2/dynamicRendering missing");
            continue;
        }

        // One queue family that can do graphics and present (true on all desktop GPUs we target).
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        std::optional<std::uint32_t> family;
        for (std::uint32_t i = 0; i < family_count; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                family = i;
                break;
            }
        }
        if (!family) {
            reject("no queue family with graphics + present");
            continue;
        }

        const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 1000
                          : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 100
                                                                                       : 10;
        if (score > best_score) {
            best_score = score;
            gpu = candidate;
            queue_family = *family;
            info.name = props.deviceName;
            info.device_type = std::string(device_type_name(props.deviceType));
            info.api_version = version_string(props.apiVersion);
            info.vendor_id = props.vendorID;
            info.driver_version_raw = props.driverVersion;
            timestamp_period_ns = props.limits.timestampPeriod;
            timestamps_supported = families[*family].timestampValidBits > 0 && props.limits.timestampPeriod > 0.0f;
        }
    }

    if (gpu == VK_NULL_HANDLE) {
        return fail(ErrorCode::Unsupported, "No GPU meets NoorEngine's requirements (Vulkan 1.3, see log)");
    }
    NOOR_LOG_INFO("Render", "Using GPU '{}' ({}, Vulkan {})", info.name, info.device_type, info.api_version);
    return {};
}

Status Renderer::Impl::create_device() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.synchronization2 = VK_TRUE;
    f13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create_info.pNext = &f2;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = extensions;
    NOOR_VK_TRY(vkCreateDevice(gpu, &create_info, nullptr, &device));
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    return {};
}

Status Renderer::Impl::create_frames() {
    for (FrameSync& frame : frames) {
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = queue_family;
        NOOR_VK_TRY(vkCreateCommandPool(device, &pool_info, nullptr, &frame.pool));

        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = frame.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        NOOR_VK_TRY(vkAllocateCommandBuffers(device, &alloc, &frame.cmd));

        VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        NOOR_VK_TRY(vkCreateSemaphore(device, &sem_info, nullptr, &frame.image_available));

        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        NOOR_VK_TRY(vkCreateFence(device, &fence_info, nullptr, &frame.in_flight));
    }

    if (timestamps_supported) {
        VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_info.queryCount = kFramesInFlight * 2;
        NOOR_VK_TRY(vkCreateQueryPool(device, &query_info, nullptr, &timestamp_pool));
    } else {
        NOOR_LOG_WARN("Render", "GPU timestamps not supported on this queue; GPU frame time unavailable");
    }

    VkCommandPoolCreateInfo upload_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    upload_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    upload_info.queueFamilyIndex = queue_family;
    NOOR_VK_TRY(vkCreateCommandPool(device, &upload_info, nullptr, &upload_pool));
    return {};
}

// ------------------------------------------------------------------ swapchain

// Per-image views, semaphores and the depth buffer. Caller guarantees the GPU is idle.
void Renderer::Impl::destroy_swapchain_resources() {
    for (VkSemaphore s : render_finished) {
        vkDestroySemaphore(device, s, nullptr);
    }
    render_finished.clear();
    for (VkImageView v : image_views) {
        vkDestroyImageView(device, v, nullptr);
    }
    image_views.clear();
    destroy_depth_buffer();
}

Status Renderer::Impl::rebuild_swapchain() {
    const platform::PixelSize pixels = target->pixel_size();
    if (pixels.width == 0 || pixels.height == 0) {
        return {};  // minimized; stay dirty and try again later
    }

    VkSurfaceCapabilitiesKHR caps{};
    NOOR_VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        return fail(ErrorCode::Unsupported, "Swapchain images cannot be color attachments on this GPU");
    }
    swapchain_supports_capture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    VkExtent2D new_extent = caps.currentExtent;
    if (new_extent.width == std::numeric_limits<std::uint32_t>::max()) {
        new_extent.width = std::clamp(pixels.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        new_extent.height = std::clamp(pixels.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (new_extent.width == 0 || new_extent.height == 0) {
        return {};
    }

    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats.data());
    if (formats.empty()) {
        return fail(ErrorCode::GraphicsError, "Surface reports no formats");
    }
    surface_format = formats.front();
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = f;
            break;
        }
    }

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create_info.surface = surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = new_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             (swapchain_supports_capture ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u);
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = caps.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always available; vsync
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = swapchain;

    vkDeviceWaitIdle(device);
    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    NOOR_VK_TRY(vkCreateSwapchainKHR(device, &create_info, nullptr, &new_swapchain));
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapchain = new_swapchain;
    extent = new_extent;

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    images.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());

    destroy_swapchain_resources();
    render_finished.resize(count, VK_NULL_HANDLE);
    for (VkSemaphore& s : render_finished) {
        VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        NOOR_VK_TRY(vkCreateSemaphore(device, &sem_info, nullptr, &s));
    }
    image_views.resize(count, VK_NULL_HANDLE);
    for (std::uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = surface_format.format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        NOOR_VK_TRY(vkCreateImageView(device, &view_info, nullptr, &image_views[i]));
    }
    if (auto status = create_depth_buffer(); !status) {
        return status;
    }
    if (auto status = ensure_mesh_pipeline(surface_format.format); !status) {
        return status;
    }

    swapchain_dirty = false;
    ++swapchain_rebuilds;
    NOOR_LOG_INFO("Render", "Swapchain built: {}x{}, {} images, format {}", extent.width, extent.height, count,
                  string_VkFormat(surface_format.format));
    return {};
}

// ------------------------------------------------------------------ frame

Result<FrameResult> Renderer::Impl::render_frame(const FrameDesc& frame_desc) {
    if (resize_requested) {
        resize_requested = false;
        const platform::PixelSize pixels = target->pixel_size();
        if (pixels.width != extent.width || pixels.height != extent.height) {
            swapchain_dirty = true;
        }
    }
    if (swapchain_dirty) {
        if (auto status = rebuild_swapchain(); !status) {
            return std::unexpected(status.error());
        }
        if (swapchain_dirty) {
            return FrameResult::Skipped;  // still minimized
        }
    }

    FrameSync& frame = frames[frame_index];
    NOOR_VK_TRY(vkWaitForFences(device, 1, &frame.in_flight, VK_TRUE, kNoTimeout));

    // This slot's previous frame has finished on the GPU, so its timestamps are ready.
    if (timestamps_supported && timestamps_written[frame_index]) {
        std::uint64_t ticks[2]{};
        if (vkGetQueryPoolResults(device, timestamp_pool, frame_index * 2, 2, sizeof(ticks), ticks,
                                  sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ticks[1] >= ticks[0]) {
            stats.gpu_ms = static_cast<double>(ticks[1] - ticks[0]) * timestamp_period_ns / 1.0e6;
        }
    }

    std::uint32_t image_index = 0;
    const VkResult acquired =
        vkAcquireNextImageKHR(device, swapchain, kNoTimeout, frame.image_available, VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty = true;
        return FrameResult::Skipped;
    }
    if (acquired == VK_SUBOPTIMAL_KHR) {
        swapchain_dirty = true;  // still usable this frame; rebuild next frame
    } else if (acquired != VK_SUCCESS) {
        return vk_failure(acquired, "vkAcquireNextImageKHR");
    }

    const auto record_start = std::chrono::steady_clock::now();
    NOOR_VK_TRY(vkResetFences(device, 1, &frame.in_flight));
    NOOR_VK_TRY(vkResetCommandPool(device, frame.pool, 0));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    NOOR_VK_TRY(vkBeginCommandBuffer(frame.cmd, &begin));
    if (timestamps_supported) {
        vkCmdResetQueryPool(frame.cmd, timestamp_pool, frame_index * 2, 2);
        vkCmdWriteTimestamp2(frame.cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, timestamp_pool, frame_index * 2);
    }

    const VkImage image = images[image_index];

    // Color: the acquire semaphore is waited at COLOR_ATTACHMENT_OUTPUT; this barrier chains on it.
    image_barrier(frame.cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    // Depth: shared across frames in flight, so wait for the previous frame's depth writes.
    image_barrier(frame.cmd, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = image_views[image_index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::memcpy(color.clearValue.color.float32, desc.clear_color.data(), sizeof(float) * 4);

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depth_view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {0.0f, 0};  // reverse-Z: 0 is infinitely far (ADR-022)

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(frame.cmd, &rendering);
    record_draws(frame.cmd, frame_desc);
    vkCmdEndRendering(frame.cmd);

    if (capture_requested && swapchain_supports_capture && !capture.pending) {
        if (auto status = record_capture(frame.cmd, image); !status) {
            return std::unexpected(status.error());
        }
        capture_requested = false;
    } else {
        image_barrier(frame.cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }

    if (timestamps_supported) {
        vkCmdWriteTimestamp2(frame.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamp_pool, frame_index * 2 + 1);
        timestamps_written[frame_index] = true;
    }
    NOOR_VK_TRY(vkEndCommandBuffer(frame.cmd));

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = frame.image_available;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = render_finished[image_index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = frame.cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    NOOR_VK_TRY(vkQueueSubmit2(queue, 1, &submit, frame.in_flight));
    stats.cpu_record_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record_start).count();

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished[image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &image_index;
    const VkResult presented = vkQueuePresentKHR(queue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        swapchain_dirty = true;
    } else if (presented != VK_SUCCESS) {
        return vk_failure(presented, "vkQueuePresentKHR");
    }

    frame_index = (frame_index + 1) % kFramesInFlight;
    ++frames_presented;
    return FrameResult::Presented;
}

// ------------------------------------------------------------------ teardown

void Renderer::Impl::destroy() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        for (FrameSync& frame : frames) {
            vkDestroyFence(device, frame.in_flight, nullptr);
            vkDestroySemaphore(device, frame.image_available, nullptr);
            vkDestroyCommandPool(device, frame.pool, nullptr);
            frame = {};
        }
        vkDestroyCommandPool(device, upload_pool, nullptr);
        upload_pool = VK_NULL_HANDLE;
        if (timestamp_pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, timestamp_pool, nullptr);
            timestamp_pool = VK_NULL_HANDLE;
        }
        for (std::uint32_t i = 0; i < meshes.size(); ++i) {
            destroy_mesh(MeshHandle{i + 1});
        }
        if (capture.buffer.buffer != VK_NULL_HANDLE) {
            destroy_buffer(capture.buffer);
        }
        destroy_mesh_pipeline();
        destroy_swapchain_resources();
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        if (allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(allocator);
            allocator = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        target->destroy_vulkan_surface(instance, surface);
        surface = VK_NULL_HANDLE;
        if (messenger != VK_NULL_HANDLE) {
            auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy_messenger) {
                destroy_messenger(instance, messenger, nullptr);
            }
            messenger = VK_NULL_HANDLE;
        }
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

// ------------------------------------------------------------------ public API

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Renderer::~Renderer() = default;

Result<std::unique_ptr<Renderer>> Renderer::create(const platform::PresentationTarget& target, const RendererDesc& desc) {
    auto impl = std::make_unique<Impl>();
    impl->target = &target;
    impl->desc = desc;

    for (auto step : {&Impl::create_instance, &Impl::pick_gpu, &Impl::create_device, &Impl::create_allocator,
                      &Impl::create_frames, &Impl::rebuild_swapchain}) {
        if (auto status = ((*impl).*step)(); !status) {
            return std::unexpected(status.error());  // Impl destructor releases what was created
        }
    }
    return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

Result<MeshHandle> Renderer::create_mesh(const MeshData& data) { return impl_->create_mesh(data); }
void Renderer::destroy_mesh(MeshHandle mesh) { impl_->destroy_mesh(mesh); }
Result<FrameResult> Renderer::render_frame(const FrameDesc& frame) { return impl_->render_frame(frame); }
void Renderer::request_capture() noexcept {
    if (!impl_->swapchain_supports_capture) {
        NOOR_LOG_WARN("Render", "Frame capture requested, but this swapchain cannot be read back");
    }
    impl_->capture_requested = true;
}
Result<std::optional<CapturedFrame>> Renderer::take_capture() { return impl_->take_capture(); }
void Renderer::notify_resized() noexcept { impl_->resize_requested = true; }
void Renderer::set_clear_color(const std::array<float, 4>& c) noexcept { impl_->desc.clear_color = c; }
const GpuInfo& Renderer::gpu_info() const noexcept { return impl_->info; }
const FrameStats& Renderer::last_frame_stats() const noexcept { return impl_->stats; }
bool Renderer::validation_enabled() const noexcept { return impl_->validation; }
std::uint64_t Renderer::validation_error_count() const noexcept { return impl_->validation_errors.load(); }
std::uint64_t Renderer::validation_warning_count() const noexcept { return impl_->validation_warnings.load(); }
std::uint64_t Renderer::frames_presented() const noexcept { return impl_->frames_presented; }
std::uint64_t Renderer::swapchain_rebuilds() const noexcept { return impl_->swapchain_rebuilds; }

} // namespace noor::render
