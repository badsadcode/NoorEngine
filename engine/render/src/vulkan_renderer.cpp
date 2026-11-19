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

bool has_instance_extension(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    return std::ranges::any_of(exts, [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
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
    }
    // Debug utils gives validation messages *and* object names / command labels that RenderDoc
    // and Nsight show. Enable it whenever the system offers it (the validation layer provides it too).
    debug_utils = validation || has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debug_utils) {
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

    if (debug_utils) {
        pfn_set_object_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
        pfn_begin_label = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
        pfn_end_label = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
    }

    NOOR_LOG_INFO("Render", "Vulkan instance created (loader {}, validation {}, debug labels {})",
                  version_string(loader_version), validation ? "on" : "off", debug_utils ? "on" : "off");

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
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        f12.pNext = &f13;
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(candidate, &f2);
        if (!f13.synchronization2 || !f13.dynamicRendering || !f13.shaderDemoteToHelperInvocation) {
            reject("synchronization2/dynamicRendering/shaderDemoteToHelperInvocation missing");
            continue;
        }
        // The bindless texture table (ADR-012): descriptor indexing, standard on desktop GPUs.
        if (!f12.runtimeDescriptorArray || !f12.descriptorBindingPartiallyBound ||
            !f12.descriptorBindingSampledImageUpdateAfterBind || !f12.descriptorBindingUpdateUnusedWhilePending) {
            reject("descriptor indexing (bindless textures) not supported");
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
            max_anisotropy = f2.features.samplerAnisotropy ? std::min(8.0f, props.limits.maxSamplerAnisotropy) : 1.0f;
            supported_samples = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
            bc_supported = f2.features.textureCompressionBC == VK_TRUE;
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
    f13.shaderDemoteToHelperInvocation = VK_TRUE;  // `discard` in shaders (cutout materials); required in Vulkan 1.3
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;
    f12.runtimeDescriptorArray = VK_TRUE;
    f12.descriptorBindingPartiallyBound = VK_TRUE;
    f12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    f12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12;
    f2.features.samplerAnisotropy = max_anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    f2.features.textureCompressionBC = bc_supported ? VK_TRUE : VK_FALSE;  // BC7/BC5 textures (ADR-029)

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
        query_info.queryCount = kFramesInFlight * kTimestampsPerFrame;
        NOOR_VK_TRY(vkCreateQueryPool(device, &query_info, nullptr, &timestamp_pool));
        set_name(VK_OBJECT_TYPE_QUERY_POOL, timestamp_pool, "GPU frame timestamps");
    } else {
        NOOR_LOG_WARN("Render", "GPU timestamps not supported on this queue; GPU frame time unavailable");
    }

    VkCommandPoolCreateInfo upload_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    upload_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    upload_info.queueFamilyIndex = queue_family;
    NOOR_VK_TRY(vkCreateCommandPool(device, &upload_info, nullptr, &upload_pool));
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        set_name(VK_OBJECT_TYPE_COMMAND_BUFFER, frames[i].cmd, std::format("Frame slot {} commands", i));
        set_name(VK_OBJECT_TYPE_FENCE, frames[i].in_flight, std::format("Frame slot {} in-flight fence", i));
        set_name(VK_OBJECT_TYPE_SEMAPHORE, frames[i].image_available, std::format("Frame slot {} image available", i));
    }
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
    destroy_hdr_target();
    destroy_cloud_volume_targets();
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
        set_name(VK_OBJECT_TYPE_IMAGE, images[i], std::format("Swapchain image {}", i));
        set_name(VK_OBJECT_TYPE_IMAGE_VIEW, image_views[i], std::format("Swapchain image {} view", i));
    }
    {
        // Scene samples: what was asked for, lowered to what the GPU can do. A change rebuilds the
        // scene pipelines (their sample count is fixed when they are made).
        std::uint32_t samples = 1;
        for (const std::uint32_t s : {8u, 4u, 2u})
            if (s <= std::max(1u, msaa_requested) && (supported_samples & s) != 0) { samples = s; break; }
        if (samples != scene_samples) {
            if (scene_samples != 0) NOOR_LOG_INFO("Render", "Anti-aliasing: {}x MSAA", samples);
            destroy_scene_pipelines();
            scene_samples = samples;
        }
    }
    if (auto status = create_depth_buffer(); !status) {
        return status;
    }
    if (auto status = create_msaa_targets(); !status) {
        return status;
    }
    if (auto status = create_hdr_target(); !status) {
        return status;
    }
    if (auto status = ensure_mesh_pipeline(surface_format.format); !status) {
        return status;
    }
    if (auto status = ensure_tonemap(surface_format.format); !status) {
        return status;
    }
    if (auto status = ensure_shadows(); !status) {
        return status;
    }
    if (auto status = ensure_sky_pipeline(); !status) {
        return status;
    }
    if (auto status = create_cloud_volume_targets(); !status) {  // half the screen; frame set binding 6
        return status;
    }
    if (auto status = ensure_default_environment(); !status) {
        return status;
    }
    if (auto status = ensure_overlay(surface_format.format); !status) {
        return std::unexpected(status.error());
    }

    swapchain_dirty = false;
    ++swapchain_rebuilds;
    NOOR_LOG_INFO("Render", "Swapchain built: {}x{}, {} images, format {}", extent.width, extent.height, count,
                  string_VkFormat(surface_format.format));
    return {};
}

// ------------------------------------------------------------------ frame

Result<FrameResult> Renderer::Impl::render_frame(const FrameDesc& frame_desc) {
    // Nothing to draw into (minimized or hidden): skip before touching the swapchain. Some drivers
    // still let us acquire and present to a minimized window, which only wastes GPU time.
    const platform::PixelSize pixels = target->pixel_size();
    if (pixels.width == 0 || pixels.height == 0) {
        return FrameResult::Skipped;
    }
    if (resize_requested) {
        resize_requested = false;
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
        std::uint64_t ticks[kTimestampsPerFrame]{};
        if (vkGetQueryPoolResults(device, timestamp_pool, frame_index * kTimestampsPerFrame, kTimestampsPerFrame,
                                  sizeof(ticks), ticks, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ticks[kTimestampsPerFrame - 1] >= ticks[0]) {
            const auto ms = [&](std::uint64_t a, std::uint64_t b) {
                return b >= a ? static_cast<double>(b - a) * timestamp_period_ns / 1.0e6 : 0.0;
            };
            stats.gpu_ms = ms(ticks[0], ticks[kTimestampsPerFrame - 1]);
            static constexpr const char* kPassNames[] = {"Environment", "Shadows", "Scene", "Tonemap", "Gizmos", "HUD"};
            stats.passes.resize(kTimestampsPerFrame - 1);
            for (std::uint32_t i = 0; i + 1 < kTimestampsPerFrame; ++i) stats.passes[i] = {kPassNames[i], ms(ticks[i], ticks[i + 1])};
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
    const auto stamp = [&](std::uint32_t i, VkPipelineStageFlags2 stage) {
        if (timestamps_supported) vkCmdWriteTimestamp2(frame.cmd, stage, timestamp_pool, frame_index * kTimestampsPerFrame + i);
    };
    if (timestamps_supported) vkCmdResetQueryPool(frame.cmd, timestamp_pool, frame_index * kTimestampsPerFrame, kTimestampsPerFrame);
    stamp(0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    stats.draw_calls = 0;
    stats.triangles = 0;
    stats.shadow_draw_calls = 0;
    stats.shadow_draws_culled = 0;
    stats.transparent_draws = 0;
    stats.msaa_samples = scene_samples;
    update_frame_uniforms(frame_desc);
    if (auto planned = plan_frame(frame_desc); !planned) return std::unexpected(planned.error());
    const std::string frame_label = std::format("NoorEngine frame {}", frames_presented);
    begin_label(frame.cmd, frame_label.c_str(), {0.42f, 0.25f, 0.85f, 1.0f});  // Noor purple

    const VkImage image = images[image_index];

    // ---- The live sky (compute), when the sun or the atmosphere changed; volume clouds (part 7b) ----
    record_live_sky(frame.cmd);
    if (auto s = record_cloud_volume(frame.cmd, frame_desc); !s) return std::unexpected(s.error());
    stamp(1, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

    // ---- Shadow pass: the sun's cascades ----
    record_shadows(frame.cmd, frame_desc);
    stamp(2, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

    // ---- Scene pass: lit draws into the HDR image ----
    // The HDR image and the depth buffer are shared by frames in flight: wait for the previous
    // frame's tonemap read (HDR) and depth writes before this frame overwrites them.
    image_barrier(frame.cmd, hdr_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(frame.cmd, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    const bool msaa = scene_samples > 1;
    if (msaa) {
        image_barrier(frame.cmd, msaa_color_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
        image_barrier(frame.cmd, msaa_depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }

    // Where nothing is drawn: the sky (exposed like everything else) with alpha 0, so the tonemap
    // pass knows it's background (and can show the fixed clear colour instead).
    const Lighting& lighting = frame_desc.lighting;
    const math::Vec3 sky = lighting.sky_color * (lighting.sky_luminance * lighting.exposure());
    VkRenderingAttachmentInfo hdr_color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    hdr_color.imageView = hdr_view;
    hdr_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdr_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    hdr_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    hdr_color.clearValue.color = {{sky.x, sky.y, sky.z, 0.0f}};
    if (msaa) {
        // Draw into the multisampled image; the GPU averages each pixel's samples into the HDR image
        // at the end of the pass (before tonemapping: bright edges stay a little crisp, a known
        // trade-off of resolving HDR).
        hdr_color.imageView = msaa_color_view;
        hdr_color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        hdr_color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        hdr_color.resolveImageView = hdr_view;
        hdr_color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depth_view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {0.0f, 0};  // reverse-Z: 0 is infinitely far (ADR-022)
    if (msaa) depth.imageView = msaa_depth_view;

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &hdr_color;
    rendering.pDepthAttachment = &depth;
    begin_label(frame.cmd, "Scene (lit, HDR)", {0.13f, 0.83f, 0.93f, 1.0f});  // cyan
    vkCmdBeginRendering(frame.cmd, &rendering);
    record_draws(frame.cmd, frame_desc, DrawPass::Opaque);
    record_sky(frame.cmd, lighting);  // after the opaque scene: only where nothing was drawn
    record_draws(frame.cmd, frame_desc, DrawPass::Transparent);  // see-through, over everything
    vkCmdEndRendering(frame.cmd);
    if (msaa) depth.imageView = depth_view;  // the screen pass's gizmos use the plain depth buffer
    end_label(frame.cmd);
    stamp(3, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // ---- Screen pass: tonemap, then gizmos and the HUD in display colours ----
    image_barrier(frame.cmd, hdr_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // The acquire semaphore is waited at COLOR_ATTACHMENT_OUTPUT; this barrier chains on it.
    image_barrier(frame.cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    // Gizmos depth-test against each other only: the depth buffer is cleared again (after the
    // scene pass finished with it).
    image_barrier(frame.cmd, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = image_views[image_index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // the tonemap pass writes every pixel
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    rendering.pColorAttachments = &color;
    begin_label(frame.cmd, "Screen (tonemap, gizmos, HUD)", {0.49f, 0.30f, 1.0f, 1.0f});  // purple
    vkCmdBeginRendering(frame.cmd, &rendering);
    record_tonemap(frame.cmd, lighting);
    stamp(4, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    record_draws(frame.cmd, frame_desc, DrawPass::Gizmos);
    stamp(5, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (auto status = record_overlay(frame.cmd, frame_desc.overlay); !status) {
        NOOR_LOG_ERROR("Render", "Overlay skipped: {}", status.error().describe());
    }
    vkCmdEndRendering(frame.cmd);
    end_label(frame.cmd);

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

    end_label(frame.cmd);  // frame
    if (timestamps_supported) {
        stamp(kTimestampsPerFrame - 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
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
        destroy_shadows();
        destroy_instance_buffers();
        destroy_environment();
        destroy_mesh_pipeline();
        destroy_msaa_targets();
        destroy_overlay();
        destroy_cloud_volume();
        destroy_frame_resources();
        destroy_textures();
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
    impl->msaa_requested = desc.msaa_samples;

    for (auto step : {&Impl::create_instance, &Impl::pick_gpu, &Impl::create_device, &Impl::create_allocator,
                      &Impl::create_frames, &Impl::rebuild_swapchain}) {
        if (auto status = ((*impl).*step)(); !status) {
            return std::unexpected(status.error());  // Impl destructor releases what was created
        }
    }
    return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

Result<MeshHandle> Renderer::create_mesh(const MeshData& data) { return impl_->create_mesh(data); }
Result<TextureHandle> Renderer::create_texture(const TextureDesc& desc) {
    if (auto s = impl_->ensure_texture_table(); !s) return std::unexpected(s.error());
    return impl_->create_texture(desc);
}
void Renderer::destroy_texture(TextureHandle texture) { impl_->destroy_texture(texture); }
std::uint64_t Renderer::texture_memory() const noexcept { return impl_->texture_bytes; }
std::uint64_t Renderer::mesh_memory() const noexcept {
    std::uint64_t bytes = 0;
    for (const auto& m : impl_->meshes)
        if (m.alive) bytes += m.bytes;
    return bytes;
}
std::uint64_t Renderer::render_target_memory() const noexcept {
    const std::uint64_t pixels = static_cast<std::uint64_t>(impl_->extent.width) * impl_->extent.height;
    const std::uint64_t samples = impl_->scene_samples;
    std::uint64_t bytes = pixels * (8 + 4);                  // HDR (RGBA16F) + depth (D32)
    if (samples > 1) bytes += pixels * samples * (8 + 4);   // the multisampled scene images
    bytes += static_cast<std::uint64_t>(impl_->shadow_size) * impl_->shadow_size * 4 * kShadowCascades;
    return bytes;
}
void Renderer::set_msaa(std::uint32_t samples) noexcept {
    impl_->msaa_requested = samples;
    impl_->swapchain_dirty = true;  // rebuilt (with its images and pipelines) before the next frame
}
std::uint32_t Renderer::msaa_samples() const noexcept { return impl_->scene_samples; }
TextureHandle Renderer::missing_texture() const noexcept { return TextureHandle{kTextureMissing}; }
bool Renderer::supports_block_compression() const noexcept { return impl_->bc_supported; }

Status Renderer::set_environment(const EnvironmentDesc& desc) {
    if (auto s = impl_->ensure_texture_table(); !s) return s;
    // The environment cube lives in the per-frame descriptor sets: make them first (this can be
    // called before the first frame, e.g. by the editor's material preview).
    if (auto s = impl_->ensure_frame_uniforms(); !s) return s;
    return impl_->upload_environment(desc);
}

Status Renderer::set_atmosphere(const AtmosphereDesc& desc) {
    if (auto s = impl_->ensure_texture_table(); !s) return s;
    if (auto s = impl_->ensure_frame_uniforms(); !s) return s;
    if (auto s = impl_->ensure_default_environment(); !s) return s;  // its sampler reads the live cube too
    return impl_->set_atmosphere(desc);
}
std::uint64_t Renderer::atmosphere_table_builds() const noexcept { return impl_->live_sky.table_builds; }
std::uint64_t Renderer::atmosphere_sky_builds() const noexcept { return impl_->live_sky.sky_builds; }

Status Renderer::update_texture(TextureHandle texture, const TextureDesc& desc) {
    if (texture.id < kFirstUserTexture || texture.id >= impl_->textures.size() || !impl_->textures[texture.id].alive)
        return fail(ErrorCode::InvalidArgument, "update_texture: not a live texture");
    impl_->destroy_texture(texture);  // waits for the GPU; the slot shows white meanwhile
    auto h = impl_->create_texture(desc, texture.id);
    if (!h) return std::unexpected(h.error());
    // Materials point at the slot, which now holds the new image: rewrite them so the descriptor
    // indices stay right (they're the same numbers, but a material may have fallen back to white).
    for (std::uint32_t i = 0; i < impl_->materials.size(); ++i)
        if (impl_->material_alive[i]) impl_->write_material(i, impl_->materials[i]);
    return {};
}

Result<MaterialHandle> Renderer::create_material(const MaterialDesc& desc) {
    if (auto s = impl_->ensure_texture_table(); !s) return std::unexpected(s.error());
    std::uint32_t slot = 0;
    for (std::uint32_t i = 1; i < impl_->materials.size(); ++i)
        if (!impl_->material_alive[i]) {
            slot = i;
            break;
        }
    if (slot == 0) {
        if (impl_->materials.size() >= kMaxMaterials)
            return fail(ErrorCode::OutOfMemory, std::format("material table full ({} materials)", kMaxMaterials));
        slot = static_cast<std::uint32_t>(impl_->materials.size());
        impl_->materials.emplace_back();
        impl_->material_alive.push_back(false);
    }
    impl_->materials[slot] = desc;
    impl_->material_alive[slot] = true;
    impl_->write_material(slot, desc);
    return MaterialHandle{slot};
}

void Renderer::update_material(MaterialHandle material, const MaterialDesc& desc) {
    if (!material.valid() || material.id >= impl_->materials.size() || !impl_->material_alive[material.id]) return;
    impl_->materials[material.id] = desc;
    impl_->write_material(material.id, desc);
}

void Renderer::destroy_material(MaterialHandle material) {
    if (!material.valid() || material.id >= impl_->materials.size()) return;
    impl_->material_alive[material.id] = false;
    impl_->materials[material.id] = {};
}
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
Status Renderer::set_overlay_atlas(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> r8) {
    return impl_->upload_overlay_atlas(width, height, r8);
}

void Renderer::set_clear_color(const std::array<float, 4>& c) noexcept { impl_->desc.clear_color = c; }
const GpuInfo& Renderer::gpu_info() const noexcept { return impl_->info; }
const FrameStats& Renderer::last_frame_stats() const noexcept { return impl_->stats; }
bool Renderer::validation_enabled() const noexcept { return impl_->validation; }
std::uint64_t Renderer::validation_error_count() const noexcept { return impl_->validation_errors.load(); }
std::uint64_t Renderer::validation_warning_count() const noexcept { return impl_->validation_warnings.load(); }
std::uint64_t Renderer::frames_presented() const noexcept { return impl_->frames_presented; }
std::uint64_t Renderer::swapchain_rebuilds() const noexcept { return impl_->swapchain_rebuilds; }
platform::PixelSize Renderer::swapchain_size() const noexcept {
    return {impl_->extent.width, impl_->extent.height};
}

} // namespace noor::render
