// GPU memory (VMA), the depth buffer, meshes and the mesh pipeline.

#include "vulkan_impl.h"

#include "noor/shaders/mesh_ps.h"
#include "noor/shaders/mesh_vs.h"

#include <cstring>

namespace noor::render {

// ------------------------------------------------------------------ memory

Status Renderer::Impl::create_allocator() {
    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.physicalDevice = gpu;
    allocator_info.device = device;
    allocator_info.instance = instance;
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
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

// ------------------------------------------------------------------ mesh pipeline

Status Renderer::Impl::ensure_mesh_pipeline(VkFormat color_format) {
    if (mesh_pipeline != VK_NULL_HANDLE && pipeline_color_format == color_format) {
        return {};
    }
    destroy_mesh_pipeline();

    const auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& out) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);  // arrays are alignas(4)
        return vkCreateShaderModule(device, &info, nullptr, &out);
    };
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule ps = VK_NULL_HANDLE;
    NOOR_VK_TRY(make_module(k_mesh_vs, sizeof(k_mesh_vs), vs));
    if (VkResult r = make_module(k_mesh_ps, sizeof(k_mesh_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return fail(ErrorCode::GraphicsError, std::format("pixel shader module failed ({})", string_VkResult(r)));
    }

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", nullptr};

    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // The projection flips Y (ADR-022); with that flip, world-space counter-clockwise
    // triangles stay counter-clockwise in Vulkan's framebuffer orientation.
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;  // reverse-Z

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    if (result == VK_SUCCESS) {
        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &mesh_pipeline);
    }

    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) {
        destroy_mesh_pipeline();
        return fail(ErrorCode::GraphicsError, std::format("Mesh pipeline creation failed ({})", string_VkResult(result)));
    }
    pipeline_color_format = color_format;
    NOOR_LOG_INFO("Render", "Mesh pipeline built for {}", string_VkFormat(color_format));
    return {};
}

void Renderer::Impl::destroy_mesh_pipeline() {
    if (mesh_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, mesh_pipeline, nullptr);
        mesh_pipeline = VK_NULL_HANDLE;
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
    mesh.alive = true;

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
    mesh = {};
}

void Renderer::Impl::record_draws(VkCommandBuffer cmd, const FrameDesc& frame) {
    stats.draw_calls = 0;  // timings are set by the frame loop, not here
    stats.triangles = 0;
    if (frame.draws.empty()) {
        return;
    }
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline);

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const math::Mat4 clip_from_world =
        math::perspective_reverse_z(frame.camera.fov_y, aspect, frame.camera.near_plane) * frame.camera.view;

    for (const DrawItem& item : frame.draws) {
        if (!item.mesh.valid() || item.mesh.id > meshes.size() || !meshes[item.mesh.id - 1].alive) {
            continue;
        }
        const GpuMesh& mesh = meshes[item.mesh.id - 1];
        const MeshPushConstants push{clip_from_world * item.world_from_object, item.world_from_object};
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
        ++stats.draw_calls;
        stats.triangles += mesh.index_count / 3;
    }
}

} // namespace noor::render
