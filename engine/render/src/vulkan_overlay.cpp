// The 2D overlay (HUD): one pipeline (no depth, alpha blending), one R8 atlas texture (font
// distance fields), and a host-visible vertex buffer per frame in flight, refilled every frame.
// Drawn inside the main pass, after the 3D scene (see render_frame).
#include "vulkan_impl.h"

#include "noor/shaders/overlay_ps.h"
#include "noor/shaders/overlay_vs.h"

#include <cstring>

namespace noor::render {

static_assert(sizeof(OverlayVertex) == 32, "overlay pipeline vertex layout: float2 position, float2 uv, float4 color");

Status Renderer::Impl::upload_overlay_atlas(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> r8) {
    if (width == 0 || height == 0 || r8.size() < static_cast<std::size_t>(width) * height)
        return fail(ErrorCode::InvalidArgument, std::format("overlay atlas: {} bytes for {}x{}", r8.size(), width, height));
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height;
    auto staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false);
    if (!staging) return std::unexpected(staging.error());
    std::memcpy(staging->mapped, r8.data(), size);
    vmaFlushAllocation(allocator, staging->allocation, 0, size);

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    if (VkResult r = vmaCreateImage(allocator, &image_info, &alloc_info, &image, &allocation, nullptr); r != VK_SUCCESS) {
        destroy_buffer(*staging);
        return vk_failure(r, "vmaCreateImage (overlay atlas)");
    }

    // Copy on the upload pool and wait (the atlas changes rarely: at start, or when a font is added).
    VkCommandBufferAllocateInfo cmd_alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc.commandPool = upload_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    auto cleanup = [&] {
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (cmd) vkFreeCommandBuffers(device, upload_pool, 1, &cmd);
        destroy_buffer(*staging);
    };
    VkResult r = vkAllocateCommandBuffers(device, &cmd_alloc, &cmd);
    if (r == VK_SUCCESS) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging->buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        vkEndCommandBuffer(cmd);
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        r = vkCreateFence(device, &fence_info, nullptr, &fence);
    }
    if (r == VK_SUCCESS) {
        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_info.commandBuffer = cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        r = vkQueueSubmit2(queue, 1, &submit, fence);
        if (r == VK_SUCCESS) r = vkWaitForFences(device, 1, &fence, VK_TRUE, kNoTimeout);
    }
    cleanup();
    if (r != VK_SUCCESS) {
        vmaDestroyImage(allocator, image, allocation);
        return vk_failure(r, "overlay atlas upload");
    }

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (r = vkCreateImageView(device, &view_info, nullptr, &view); r != VK_SUCCESS) {
        vmaDestroyImage(allocator, image, allocation);
        return vk_failure(r, "vkCreateImageView (overlay atlas)");
    }

    // Swap in the new atlas; the old one may still be read by frames in flight.
    if (overlay.atlas_image != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vkDestroyImageView(device, overlay.atlas_view, nullptr);
        vmaDestroyImage(allocator, overlay.atlas_image, overlay.atlas_allocation);
    }
    overlay.atlas_image = image;
    overlay.atlas_allocation = allocation;
    overlay.atlas_view = view;
    set_name(VK_OBJECT_TYPE_IMAGE, image, std::format("Overlay atlas {}x{}", width, height));
    if (overlay.descriptor_set != VK_NULL_HANDLE) {
        VkDescriptorImageInfo descriptor_image{overlay.sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = overlay.descriptor_set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descriptor_image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    return {};
}

Status Renderer::Impl::ensure_overlay(VkFormat color_format) {
    if (overlay.pipeline != VK_NULL_HANDLE && overlay.color_format == color_format) return {};

    if (overlay.descriptor_set == VK_NULL_HANDLE) {
        // Created once: the sampler, the descriptor set, and a 1x1 placeholder atlas (solid shapes only).
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        NOOR_VK_TRY(vkCreateSampler(device, &sampler_info, nullptr, &overlay.sampler));
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_layout_info.bindingCount = 1;
        set_layout_info.pBindings = &binding;
        NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &set_layout_info, nullptr, &overlay.set_layout));
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        NOOR_VK_TRY(vkCreateDescriptorPool(device, &pool_info, nullptr, &overlay.descriptor_pool));
        VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set_info.descriptorPool = overlay.descriptor_pool;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &overlay.set_layout;
        NOOR_VK_TRY(vkAllocateDescriptorSets(device, &set_info, &overlay.descriptor_set));
        if (overlay.atlas_image == VK_NULL_HANDLE) {  // set_overlay_atlas not called yet
            const std::uint8_t blank = 0;
            if (auto st = upload_overlay_atlas(1, 1, {&blank, 1}); !st) return st;
        }
        // Point the new set at the atlas (later uploads update it themselves).
        VkDescriptorImageInfo descriptor_image{overlay.sampler, overlay.atlas_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = overlay.descriptor_set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descriptor_image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    if (overlay.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, overlay.pipeline, nullptr);
        overlay.pipeline = VK_NULL_HANDLE;
    }
    if (overlay.layout == VK_NULL_HANDLE) {
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &overlay.set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        NOOR_VK_TRY(vkCreatePipelineLayout(device, &layout_info, nullptr, &overlay.layout));
    }

    auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& out) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device, &info, nullptr, &out);
    };
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    NOOR_VK_TRY(make_module(k_overlay_vs, sizeof(k_overlay_vs), vs));
    if (VkResult r = make_module(k_overlay_ps, sizeof(k_overlay_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return vk_failure(r, "vkCreateShaderModule (overlay pixel shader)");
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", nullptr};

    const VkVertexInputBindingDescription vertex_binding{0, sizeof(OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[3] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, uv)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayVertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &vertex_binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;  // 2D: winding doesn't matter
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};  // off: always on top
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    rendering.depthAttachmentFormat = kDepthFormat;  // drawn in the main pass, which has depth
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
    pipeline_info.layout = overlay.layout;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &overlay.pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) return vk_failure(result, "vkCreateGraphicsPipelines (overlay)");
    overlay.color_format = color_format;
    set_name(VK_OBJECT_TYPE_PIPELINE, overlay.pipeline, "Overlay pipeline (HUD)");
    return {};
}

Status Renderer::Impl::record_overlay(VkCommandBuffer cmd, std::span<const OverlayVertex> vertices) {
    if (vertices.empty() || overlay.pipeline == VK_NULL_HANDLE) return {};
    GpuBuffer& vb = overlay.vertices[frame_index];
    std::size_t& capacity = overlay.capacity[frame_index];
    if (vertices.size() > capacity) {  // grow (this frame slot's previous contents are done: its fence was waited)
        if (vb.buffer != VK_NULL_HANDLE) destroy_buffer(vb);
        capacity = std::max<std::size_t>(vertices.size() * 2, 6 * 256);
        auto buffer = create_buffer(capacity * sizeof(OverlayVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, false);
        if (!buffer) {
            capacity = 0;
            return std::unexpected(buffer.error());
        }
        vb = *buffer;
        set_name(VK_OBJECT_TYPE_BUFFER, vb.buffer, std::format("Overlay vertices (frame slot {})", frame_index));
    }
    std::memcpy(vb.mapped, vertices.data(), vertices.size_bytes());
    vmaFlushAllocation(allocator, vb.allocation, 0, vertices.size_bytes());

    begin_label(cmd, "Overlay (HUD)", {0.49f, 0.30f, 1.0f, 1.0f});  // purple
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay.pipeline);
    // The whole screen (the tonemap pass may have drawn a smaller picture, 2D part 6): vertices are screen pixels.
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay.layout, 0, 1, &overlay.descriptor_set, 0, nullptr);
    const float scale[2] = {2.0f / static_cast<float>(extent.width), 2.0f / static_cast<float>(extent.height)};
    vkCmdPushConstants(cmd, overlay.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(scale), scale);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &offset);
    vkCmdDraw(cmd, static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
    end_label(cmd);
    stats.draw_calls += 1;
    return {};
}

void Renderer::Impl::destroy_overlay() {
    if (device == VK_NULL_HANDLE) return;
    for (auto& vb : overlay.vertices)
        if (vb.buffer != VK_NULL_HANDLE) destroy_buffer(vb);
    if (overlay.pipeline) vkDestroyPipeline(device, overlay.pipeline, nullptr);
    if (overlay.layout) vkDestroyPipelineLayout(device, overlay.layout, nullptr);
    if (overlay.descriptor_pool) vkDestroyDescriptorPool(device, overlay.descriptor_pool, nullptr);
    if (overlay.set_layout) vkDestroyDescriptorSetLayout(device, overlay.set_layout, nullptr);
    if (overlay.sampler) vkDestroySampler(device, overlay.sampler, nullptr);
    if (overlay.atlas_view) vkDestroyImageView(device, overlay.atlas_view, nullptr);
    if (overlay.atlas_image) vmaDestroyImage(allocator, overlay.atlas_image, overlay.atlas_allocation);
    overlay = {};
}

} // namespace noor::render
