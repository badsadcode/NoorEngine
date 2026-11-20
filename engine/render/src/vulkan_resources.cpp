// GPU memory (VMA), the depth buffer, meshes and the mesh pipeline.

#include "vulkan_impl.h"

#include "noor/shaders/mesh_ps.h"
#include "noor/shaders/mesh_vs.h"
#include "noor/shaders/particle_ps.h"
#include "noor/shaders/particle_vs.h"
#include "noor/shaders/weather_ps.h"
#include "noor/shaders/weather_vs.h"

#include <cstring>

namespace noor::render {

// ------------------------------------------------------------------ memory

Status Renderer::Impl::create_allocator() {
    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.physicalDevice = gpu;
    allocator_info.device = device;
    allocator_info.instance = instance;
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;  // skinned meshes (8C)
    NOOR_VK_TRY(vmaCreateAllocator(&allocator_info, &allocator));
    return {};
}

Result<GpuBuffer> Renderer::Impl::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible,
                                                bool readback) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    if (host_visible) {
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                           (readback ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                     : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    }

    GpuBuffer out;
    VmaAllocationInfo result{};
    NOOR_VK_TRY(vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &out.buffer, &out.allocation, &result));
    out.mapped = result.pMappedData;
    return out;
}

void Renderer::Impl::destroy_buffer(GpuBuffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

// Copies `data` into a new device-local buffer through a staging buffer, and waits for it.
Status Renderer::Impl::upload_to_device(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer& out) {
    auto staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false);
    if (!staging) {
        return std::unexpected(staging.error());
    }
    std::memcpy(staging->mapped, data, size);
    vmaFlushAllocation(allocator, staging->allocation, 0, size);

    auto device_buffer = create_buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, false);
    if (!device_buffer) {
        destroy_buffer(*staging);
        return std::unexpected(device_buffer.error());
    }

    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = upload_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    const auto cleanup = [&] {
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (cmd) vkFreeCommandBuffers(device, upload_pool, 1, &cmd);
        destroy_buffer(*staging);
    };
    const auto fail_with = [&](VkResult r, const char* what) -> Status {
        cleanup();
        destroy_buffer(*device_buffer);
        return fail(ErrorCode::GraphicsError, std::format("{} failed ({})", what, string_VkResult(r)));
    };

    if (VkResult r = vkAllocateCommandBuffers(device, &alloc, &cmd); r != VK_SUCCESS) return fail_with(r, "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    const VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging->buffer, device_buffer->buffer, 1, &region);

    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = device_buffer->buffer;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (VkResult r = vkCreateFence(device, &fence_info, nullptr, &fence); r != VK_SUCCESS) return fail_with(r, "vkCreateFence");
    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    if (VkResult r = vkQueueSubmit2(queue, 1, &submit, fence); r != VK_SUCCESS) return fail_with(r, "vkQueueSubmit2");
    if (VkResult r = vkWaitForFences(device, 1, &fence, VK_TRUE, kNoTimeout); r != VK_SUCCESS) return fail_with(r, "vkWaitForFences");

    cleanup();
    out = *device_buffer;
    return {};
}

// ------------------------------------------------------------------ depth buffer

Status Renderer::Impl::create_depth_buffer() {
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = kDepthFormat;
    image_info.extent = {extent.width, extent.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc_info, &depth_image, &depth_allocation, nullptr));

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = depth_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = kDepthFormat;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    NOOR_VK_TRY(vkCreateImageView(device, &view_info, nullptr, &depth_view));
    set_name(VK_OBJECT_TYPE_IMAGE, depth_image, "Depth buffer (D32, reverse-Z)");
    set_name(VK_OBJECT_TYPE_IMAGE_VIEW, depth_view, "Depth buffer view");
    return {};
}

void Renderer::Impl::destroy_depth_buffer() {
    if (depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depth_view, nullptr);
        depth_view = VK_NULL_HANDLE;
    }
    if (depth_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depth_image, depth_allocation);
        depth_image = VK_NULL_HANDLE;
        depth_allocation = VK_NULL_HANDLE;
    }
}

// ------------------------------------------------------------------ MSAA images

Status Renderer::Impl::create_msaa_targets() {
    destroy_msaa_targets();
    if (scene_samples <= 1) return {};
    const auto make = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage& image,
                          VmaAllocation& allocation, VkImageView& view, const char* name) -> Status {
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = format;
        image_info.extent = {extent.width, extent.height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = static_cast<VkSampleCountFlagBits>(scene_samples);
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = usage | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;  // never read after the pass
        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc_info, &image, &allocation, nullptr));
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange = {aspect, 0, 1, 0, 1};
        NOOR_VK_TRY(vkCreateImageView(device, &view_info, nullptr, &view));
        set_name(VK_OBJECT_TYPE_IMAGE, image, std::format("{} ({}x MSAA)", name, scene_samples));
        return {};
    };
    if (auto s = make(kHdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, msaa_color_image,
                      msaa_color_allocation, msaa_color_view, "HDR scene, multisampled");
        !s)
        return s;
    return make(kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, msaa_depth_image,
                msaa_depth_allocation, msaa_depth_view, "Depth, multisampled");
}

void Renderer::Impl::destroy_msaa_targets() {
    if (device == VK_NULL_HANDLE) return;
    if (msaa_color_view) vkDestroyImageView(device, msaa_color_view, nullptr);
    if (msaa_color_image) vmaDestroyImage(allocator, msaa_color_image, msaa_color_allocation);
    if (msaa_depth_view) vkDestroyImageView(device, msaa_depth_view, nullptr);
    if (msaa_depth_image) vmaDestroyImage(allocator, msaa_depth_image, msaa_depth_allocation);
    msaa_color_view = msaa_depth_view = VK_NULL_HANDLE;
    msaa_color_image = msaa_depth_image = VK_NULL_HANDLE;
    msaa_color_allocation = msaa_depth_allocation = VK_NULL_HANDLE;
}

void Renderer::Impl::destroy_scene_pipelines() {
    if (device == VK_NULL_HANDLE) return;
    for (VkPipeline* p : {&mesh_pipeline, &double_sided_pipeline, &blend_pipeline, &blend_double_sided_pipeline, &sky_pipeline, &particle_pipeline, &weather_pipeline}) {
        if (*p != VK_NULL_HANDLE) vkDestroyPipeline(device, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
}

// ------------------------------------------------------------------ mesh pipeline

Status Renderer::Impl::ensure_mesh_pipeline(VkFormat color_format) {
    if (mesh_pipeline != VK_NULL_HANDLE && gizmo_pipeline != VK_NULL_HANDLE && pipeline_color_format == color_format) {
        return {};
    }
    if (auto s = ensure_frame_uniforms(); !s) return s;
    if (auto s = ensure_texture_table(); !s) return s;
    if (pipeline_layout == VK_NULL_HANDLE) {
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants)};
        const VkDescriptorSetLayout set_layouts[2] = {frame_set_layout, texture_set_layout};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        NOOR_VK_TRY(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout));
        set_name(VK_OBJECT_TYPE_PIPELINE_LAYOUT, pipeline_layout, "Mesh pipeline layout");
    }
    if (mesh_pipeline == VK_NULL_HANDLE) {  // the HDR format never changes
        if (VkResult r = create_mesh_pipeline(kHdrFormat, false, mesh_pipeline); r != VK_SUCCESS) {
            return vk_failure(r, "vkCreateGraphicsPipelines (scene)");
        }
        set_name(VK_OBJECT_TYPE_PIPELINE, mesh_pipeline, "Scene pipeline (lit, HDR)");
        if (VkResult r = create_mesh_pipeline(kHdrFormat, false, double_sided_pipeline, false); r != VK_SUCCESS) {
            return vk_failure(r, "vkCreateGraphicsPipelines (scene, double-sided)");
        }
        set_name(VK_OBJECT_TYPE_PIPELINE, double_sided_pipeline, "Scene pipeline (double-sided)");
        if (VkResult r = create_mesh_pipeline(kHdrFormat, false, blend_pipeline, true, true); r != VK_SUCCESS) {
            return vk_failure(r, "vkCreateGraphicsPipelines (scene, see-through)");
        }
        set_name(VK_OBJECT_TYPE_PIPELINE, blend_pipeline, "Scene pipeline (see-through)");
        if (VkResult r = create_mesh_pipeline(kHdrFormat, false, blend_double_sided_pipeline, false, true); r != VK_SUCCESS) {
            return vk_failure(r, "vkCreateGraphicsPipelines (scene, see-through double-sided)");
        }
        set_name(VK_OBJECT_TYPE_PIPELINE, blend_double_sided_pipeline, "Scene pipeline (see-through, double-sided)");
        if (VkResult r = create_particle_pipeline(particle_pipeline); r != VK_SUCCESS) {
            return vk_failure(r, "vkCreateGraphicsPipelines (particles)");
        }
        set_name(VK_OBJECT_TYPE_PIPELINE, particle_pipeline, "Particle pipeline (see-through)");
        if (VkResult r = create_particle_pipeline(weather_pipeline, true); r != VK_SUCCESS) {
            return vk_failure(r, "vkCreateGraphicsPipelines (weather)");
        }
        set_name(VK_OBJECT_TYPE_PIPELINE, weather_pipeline, "Weather pipeline (rain, snow, dust)");
    }
    if (gizmo_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, gizmo_pipeline, nullptr);
        gizmo_pipeline = VK_NULL_HANDLE;
    }
    if (VkResult r = create_mesh_pipeline(color_format, true, gizmo_pipeline); r != VK_SUCCESS) {
        return vk_failure(r, "vkCreateGraphicsPipelines (gizmos)");
    }
    set_name(VK_OBJECT_TYPE_PIPELINE, gizmo_pipeline, "Gizmo pipeline (display colours)");
    pipeline_color_format = color_format;
    NOOR_LOG_INFO("Render", "Mesh pipelines built (scene {}, gizmos {})", string_VkFormat(kHdrFormat), string_VkFormat(color_format));
    return {};
}

// One pipeline from mesh.hlsl: `display` false = lit scene into the HDR image, true = gizmos in
// display colours (the shader's kDisplay specialization constant).
VkResult Renderer::Impl::create_mesh_pipeline(VkFormat color_format, bool display, VkPipeline& out, bool cull_back, bool blended) {
    const auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& module) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);  // arrays are alignas(4)
        return vkCreateShaderModule(device, &info, nullptr, &module);
    };
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule ps = VK_NULL_HANDLE;
    if (VkResult r = make_module(k_mesh_vs, sizeof(k_mesh_vs), vs); r != VK_SUCCESS) return r;
    if (VkResult r = make_module(k_mesh_ps, sizeof(k_mesh_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return r;
    }

    const VkBool32 display_value = display ? VK_TRUE : VK_FALSE;
    const VkSpecializationMapEntry entry{0, 0, sizeof(VkBool32)};
    const VkSpecializationInfo specialization{1, &entry, sizeof(display_value), &display_value};
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", &specialization};

    // Binding 0: the mesh's vertices. Binding 1: one GpuInstance per instance (ADR-032).
    const VkVertexInputBindingDescription bindings[2] = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
                                                         {1, sizeof(GpuInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
    const VkVertexInputAttributeDescription attributes[9] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuInstance, row0)},
        {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuInstance, row1)},
        {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuInstance, row2)},
        {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuInstance, tint)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 2;
    vertex_input.pVertexBindingDescriptions = bindings;
    vertex_input.vertexAttributeDescriptionCount = 9;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // The projection flips Y (ADR-022); with that flip, world-space counter-clockwise
    // triangles stay counter-clockwise in the framebuffer orientation Vulkan uses.
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = cull_back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = display ? VK_SAMPLE_COUNT_1_BIT : static_cast<VkSampleCountFlagBits>(std::max(1u, scene_samples));

    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = blended ? VK_FALSE : VK_TRUE;  // see-through: behind ones must still show
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;  // reverse-Z

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (blended) {
        // Premultiplied alpha (mesh.hlsl already multiplied what the surface lets through): reflections
        // keep their strength on glass, the rest shows through by (1 - alpha).
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format;
    rendering.depthAttachmentFormat = kDepthFormat;

    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext = &rendering;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState = &viewport;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = pipeline_layout;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &out);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    return result;
}

// The particle pipeline (Milestone 8B): particle.hlsl, one ParticleVertex per instance and six vertices (two triangles)
// made from the vertex index. Into the multisampled HDR scene like see-through meshes: depth-tested, not written;
// premultiplied blending (alpha particles write their alpha, additive ones 0); rgb only, so the image's alpha keeps
// marking the background for the tonemapper.
// `weather`: weather.hlsl instead (rain, snow, dust: no vertex data, everything from the instance index).
VkResult Renderer::Impl::create_particle_pipeline(VkPipeline& out, bool weather) {
    const auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& module) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device, &info, nullptr, &module);
    };
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    if (VkResult r = weather ? make_module(k_weather_vs, sizeof(k_weather_vs), vs) : make_module(k_particle_vs, sizeof(k_particle_vs), vs);
        r != VK_SUCCESS)
        return r;
    if (VkResult r = weather ? make_module(k_weather_ps, sizeof(k_weather_ps), ps) : make_module(k_particle_ps, sizeof(k_particle_ps), ps);
        r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return r;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", nullptr};
    const VkVertexInputBindingDescription binding{0, sizeof(ParticleVertex), VK_VERTEX_INPUT_RATE_INSTANCE};
    const VkVertexInputAttributeDescription attributes[3] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleVertex, position)},   // position, size
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleVertex, color)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleVertex, velocity)},   // velocity, frame
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = weather ? 0 : 1;
    vertex_input.pVertexBindingDescriptions = weather ? nullptr : &binding;
    vertex_input.vertexAttributeDescriptionCount = weather ? 0 : 3;
    vertex_input.pVertexAttributeDescriptions = weather ? nullptr : attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(std::max(1u, scene_samples));
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;  // reverse-Z
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    const VkFormat color_format = kHdrFormat;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format;
    rendering.depthAttachmentFormat = kDepthFormat;
    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext = &rendering;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState = &viewport;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = pipeline_layout;  // the mesh layout: same sets, same push constant shape
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &out);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    return result;
}

void Renderer::Impl::destroy_mesh_pipeline() {
    if (mesh_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, mesh_pipeline, nullptr);
        mesh_pipeline = VK_NULL_HANDLE;
    }
    if (gizmo_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, gizmo_pipeline, nullptr);
        gizmo_pipeline = VK_NULL_HANDLE;
    }
    for (VkPipeline* p : {&double_sided_pipeline, &blend_pipeline, &blend_double_sided_pipeline, &particle_pipeline, &weather_pipeline}) {
        if (*p != VK_NULL_HANDLE) vkDestroyPipeline(device, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
}

// ------------------------------------------------------------------ meshes

Result<MeshHandle> Renderer::Impl::create_mesh(const MeshData& data) {
    if (data.vertices.empty() || data.indices.empty() || data.indices.size() % 3 != 0) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("Mesh needs vertices and a triangle list (got {} vertices, {} indices)",
                                data.vertices.size(), data.indices.size()));
    }
    // Meshes may come from imported files later: never trust indices.
    for (const std::uint32_t index : data.indices) {
        if (index >= data.vertices.size()) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("Mesh index {} out of range ({} vertices)", index, data.vertices.size()));
        }
    }

    GpuMesh mesh;
    if (auto s = upload_to_device(data.vertices.data(), data.vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  mesh.vertices); !s) {
        return std::unexpected(s.error());
    }
    if (auto s = upload_to_device(data.indices.data(), data.indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  mesh.indices); !s) {
        destroy_buffer(mesh.vertices);
        return std::unexpected(s.error());
    }
    mesh.index_count = static_cast<std::uint32_t>(data.indices.size());
    mesh.bytes = data.vertices.size_bytes() + data.indices.size_bytes();
    // A skinned mesh (8C): its skin data in a buffer of its own, read by the vertex shader through its address.
    if (!data.skin.empty()) {
        if (data.skin.size() != data.vertices.size()) {
            destroy_buffer(mesh.vertices);
            destroy_buffer(mesh.indices);
            return fail(ErrorCode::InvalidArgument, std::format("Skin data for {} vertices, the mesh has {}", data.skin.size(), data.vertices.size()));
        }
        if (auto s = upload_to_device(data.skin.data(), data.skin.size_bytes(),
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, mesh.skin);
            !s) {
            destroy_buffer(mesh.vertices);
            destroy_buffer(mesh.indices);
            return std::unexpected(s.error());
        }
        VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        address.buffer = mesh.skin.buffer;
        mesh.skin_address = vkGetBufferDeviceAddress(device, &address);
        mesh.bytes += data.skin.size_bytes();
    }
    {
        // Bounding sphere: the box's centre, and the farthest vertex from it.
        math::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const Vertex& v : data.vertices) {
            lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
            hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
        }
        mesh.center = data.vertices.empty() ? math::Vec3{} : (lo + hi) * 0.5f;
        for (const Vertex& v : data.vertices) mesh.radius = std::max(mesh.radius, math::length(v.position - mesh.center));
    }
    mesh.alive = true;
    const std::string label = data.debug_name.empty() ? std::string("Mesh") : std::string(data.debug_name);
    set_name(VK_OBJECT_TYPE_BUFFER, mesh.vertices.buffer, label + " vertices");
    if (mesh.skin.buffer != VK_NULL_HANDLE) set_name(VK_OBJECT_TYPE_BUFFER, mesh.skin.buffer, label + " skin");
    set_name(VK_OBJECT_TYPE_BUFFER, mesh.indices.buffer, label + " indices");

    for (std::uint32_t i = 0; i < meshes.size(); ++i) {
        if (!meshes[i].alive) {
            meshes[i] = mesh;
            return MeshHandle{i + 1};
        }
    }
    meshes.push_back(mesh);
    return MeshHandle{static_cast<std::uint32_t>(meshes.size())};
}

void Renderer::Impl::destroy_mesh(MeshHandle handle) {
    if (!handle.valid() || handle.id > meshes.size() || !meshes[handle.id - 1].alive) {
        return;
    }
    vkDeviceWaitIdle(device);  // Milestone 1: simple and safe; deferred deletion comes later
    GpuMesh& mesh = meshes[handle.id - 1];
    destroy_buffer(mesh.vertices);
    destroy_buffer(mesh.indices);
    destroy_buffer(mesh.skin);
    mesh = {};
}

std::uint32_t Renderer::Impl::material_slot(const DrawItem& item) const noexcept { return material_slot(item.material); }

std::uint32_t Renderer::Impl::material_slot(MaterialHandle material) const noexcept {
    return material.valid() && material.id < materials.size() && material_alive[material.id] ? material.id : 0u;
}

void Renderer::Impl::record_draws(VkCommandBuffer cmd, const FrameDesc& frame, DrawPass pass) {
    (void)frame;  // planned in plan_frame
    const auto& batches = pass == DrawPass::Opaque ? plan.opaque : pass == DrawPass::Transparent ? plan.transparent : plan.gizmos;
    if (pass == DrawPass::Transparent ? plan.see_through.empty() && plan.weather.empty() : batches.empty()) return;  // particles alone still draw
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    const VkDescriptorSet sets[2] = {frame_sets[frame_index], texture_set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2, sets, 0, nullptr);
    if (pass != DrawPass::Transparent) {
        record_batches(cmd, batches, false);
        return;
    }
    // See-through meshes and particles together, back to front (plan_frame merged them by distance).
    begin_label(cmd, "See-through (sorted)", {0.6f, 0.85f, 1.0f, 1.0f});
    const VkDeviceSize zero = 0;
    for (const SeeThrough& item : plan.see_through) {
        if (!item.particles) {
            record_batches(cmd, std::span<const Batch>(&plan.transparent[item.index], 1), false);
            continue;
        }
        const ParticleBatch& p = plan.particles[item.index];
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particle_pipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &particle_buffers[frame_index].buffer, &zero);
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(p.push), &p.push);
        vkCmdDraw(cmd, 6, p.count, 0, p.first);
        ++stats.draw_calls;
        stats.triangles += 2ull * p.count;
    }
    // Rain, snow and dust last, over everything (in front of see-through things too: a drop between the camera and a
    // window is drawn over it; one behind the window as well, untinted: accepted, ADR-035).
    for (const auto& w : plan.weather) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, weather_pipeline);
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(w.push), &w.push);
        vkCmdDraw(cmd, 6, w.count, 0, 0);
        ++stats.draw_calls;
        stats.triangles += 2ull * w.count;
    }
    end_label(cmd);
}

} // namespace noor::render
