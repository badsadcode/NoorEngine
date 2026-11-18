// Sun shadows (Milestone 5, part 4): one D32 2D-array image with a layer per cascade, a depth-only
// pass that draws every shadow-casting item into each layer from the sun, and a comparison sampler
// the scene shader filters with (3x3 PCF). Cascade maths: shadows.cpp.
#include "vulkan_impl.h"

#include "noor/shaders/shadow_ps.h"
#include "noor/shaders/shadow_vs.h"

namespace noor::render {

Status Renderer::Impl::ensure_shadows() {
    if (shadow_image != VK_NULL_HANDLE) return {};
    shadow_size = std::clamp(desc.shadow_map_size, 256u, 8192u);

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = kShadowFormat;
    image_info.extent = {shadow_size, shadow_size, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = kShadowCascades;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc_info, &shadow_image, &shadow_allocation, nullptr));
    set_name(VK_OBJECT_TYPE_IMAGE, shadow_image, std::format("Sun shadow cascades ({} x {}², D32)", kShadowCascades, shadow_size));

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = shadow_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view_info.format = kShadowFormat;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
    NOOR_VK_TRY(vkCreateImageView(device, &view_info, nullptr, &shadow_view));
    for (std::uint32_t i = 0; i < kShadowCascades; ++i) {
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, i, 1};
        NOOR_VK_TRY(vkCreateImageView(device, &view_info, nullptr, &shadow_layer_views[i]));
    }

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;  // with compareEnable: bilinear PCF, four depth tests blended
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // outside the map: lit
    sampler_info.compareEnable = VK_TRUE;
    sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // lit when the point is no deeper than the nearest caster
    NOOR_VK_TRY(vkCreateSampler(device, &sampler_info, nullptr, &shadow_sampler));

    // The scene shader's descriptor (frame set, binding 1), in every frame slot.
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkDescriptorImageInfo image{shadow_sampler, shadow_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame_sets[i];
        write.dstBinding = 1;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // Depth-only pipeline (the fragment shader only discards cutout pixels), no culling so thin
    // and one-sided geometry casts too, with a slope-scaled depth bias against shadow acne.
    const auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& module) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device, &info, nullptr, &module);
    };
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    NOOR_VK_TRY(make_module(k_shadow_vs, sizeof(k_shadow_vs), vs));
    if (VkResult r = make_module(k_shadow_ps, sizeof(k_shadow_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return vk_failure(r, "vkCreateShaderModule (shadow pixel shader)");
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", nullptr};
    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = VK_TRUE;
    raster.depthBiasConstantFactor = 1.0f;
    raster.depthBiasSlopeFactor = 2.0f;
    raster.depthClampEnable = VK_FALSE;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;  // shadow maps use ordinary depth: 0 near the sun, 1 far
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};  // no colour
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.depthAttachmentFormat = kShadowFormat;
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
    pipeline_info.layout = pipeline_layout;  // the mesh layout: same sets and push constants
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &shadow_pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) return vk_failure(result, "vkCreateGraphicsPipelines (shadows)");
    set_name(VK_OBJECT_TYPE_PIPELINE, shadow_pipeline, "Shadow pipeline (depth only)");
    NOOR_LOG_INFO("Render", "Sun shadows: {} cascades of {}x{}", kShadowCascades, shadow_size, shadow_size);
    return {};
}

void Renderer::Impl::record_shadows(VkCommandBuffer cmd, const FrameDesc& frame) {
    const bool on = frame.lighting.sun_shadows && frame.lighting.sun_illuminance > 0.0f;
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadow_image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    // The previous frame sampled it: wait for that before writing (contents are not kept).
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (on) {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        vkCmdPipelineBarrier2(cmd, &dep);

        begin_label(cmd, "Shadows (sun cascades)", {0.4f, 0.4f, 0.45f, 1.0f});
        const VkViewport vp{0.0f, 0.0f, static_cast<float>(shadow_size), static_cast<float>(shadow_size), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {shadow_size, shadow_size}};
        for (std::uint32_t c = 0; c < kShadowCascades; ++c) {
            VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depth.imageView = shadow_layer_views[c];
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil = {1.0f, 0};
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea = scissor;
            rendering.layerCount = 1;
            rendering.pDepthAttachment = &depth;
            vkCmdBeginRendering(cmd, &rendering);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline);
            const VkDescriptorSet sets[2] = {frame_sets[frame_index], texture_set};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2, sets, 0, nullptr);
            for (const DrawItem& item : frame.draws) {
                if (item.overlay || !item.cast_shadow) continue;
                if (!item.mesh.valid() || item.mesh.id > meshes.size() || !meshes[item.mesh.id - 1].alive) continue;
                const GpuMesh& mesh = meshes[item.mesh.id - 1];
                const std::uint32_t first = std::min(item.first_index, mesh.index_count);
                const std::uint32_t count = item.index_count == 0 ? mesh.index_count - first : std::min(item.index_count, mesh.index_count - first);
                if (count == 0) continue;
                const math::Mat4& w = item.world_from_object;
                {
                    // Skip casters that can't reach this cascade (world sphere: centre moved, radius
                    // scaled by the largest axis scale).
                    const float scale = std::max({math::length(math::Vec3{w.m[0].x, w.m[0].y, w.m[0].z}),
                                                  math::length(math::Vec3{w.m[1].x, w.m[1].y, w.m[1].z}),
                                                  math::length(math::Vec3{w.m[2].x, w.m[2].y, w.m[2].z})});
                    if (!sphere_touches_cascade(cascades[c], w.transform_point(mesh.center), mesh.radius * scale)) {
                        ++stats.shadow_draws_culled;
                        continue;
                    }
                }
                const std::uint32_t material = material_slot(item);
                const MeshPushConstants push{{w.m[0].x, w.m[1].x, w.m[2].x, w.m[3].x},
                                             {w.m[0].y, w.m[1].y, w.m[2].y, w.m[3].y},
                                             {w.m[0].z, w.m[1].z, w.m[2].z, w.m[3].z},
                                             item.tint,
                                             {material, c, 0, 0},
                                             item.splat_tiling};
                vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
                vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, count, 1, first, 0, 0);
                ++stats.draw_calls;
                ++stats.shadow_draw_calls;
            }
            vkCmdEndRendering(cmd);
        }
        end_label(cmd);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }
    // Ready for sampling (when shadows are off the shader doesn't read it, but the layout must match).
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Renderer::Impl::destroy_shadows() {
    if (device == VK_NULL_HANDLE) return;
    if (shadow_pipeline) vkDestroyPipeline(device, shadow_pipeline, nullptr);
    if (shadow_sampler) vkDestroySampler(device, shadow_sampler, nullptr);
    for (auto& v : shadow_layer_views)
        if (v) vkDestroyImageView(device, v, nullptr);
    if (shadow_view) vkDestroyImageView(device, shadow_view, nullptr);
    if (shadow_image) vmaDestroyImage(allocator, shadow_image, shadow_allocation);
    shadow_pipeline = VK_NULL_HANDLE;
    shadow_sampler = VK_NULL_HANDLE;
    shadow_layer_views = {};
    shadow_view = VK_NULL_HANDLE;
    shadow_image = VK_NULL_HANDLE;
    shadow_size = 0;
}

} // namespace noor::render
