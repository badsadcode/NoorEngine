// The frame's HDR side (ADR-012): the 16-bit float image the scene is lit into, the per-frame
// lighting uniforms (sun, sky, exposure, camera), and the tonemap pass that turns the HDR image
// into display colours on the screen.
#include "vulkan_impl.h"

#include "noor/shaders/tonemap_ps.h"
#include "noor/shaders/tonemap_vs.h"

#include <cmath>
#include <cstring>

namespace noor::render {

float Lighting::exposure() const noexcept { return 1.0f / (1.2f * std::exp2(ev100)); }

// ------------------------------------------------------------------ HDR image

Status Renderer::Impl::create_hdr_target() {
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = kHdrFormat;
    image_info.extent = {extent.width, extent.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc_info, &hdr_image, &hdr_allocation, nullptr));
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = hdr_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = kHdrFormat;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NOOR_VK_TRY(vkCreateImageView(device, &view_info, nullptr, &hdr_view));
    set_name(VK_OBJECT_TYPE_IMAGE, hdr_image, "HDR scene (RGBA16F, exposed, before tonemapping)");
    set_name(VK_OBJECT_TYPE_IMAGE_VIEW, hdr_view, "HDR scene view");
    if (tonemap.set != VK_NULL_HANDLE) write_tonemap_descriptor();
    return {};
}

void Renderer::Impl::destroy_hdr_target() {
    if (hdr_view != VK_NULL_HANDLE) vkDestroyImageView(device, hdr_view, nullptr);
    if (hdr_image != VK_NULL_HANDLE) vmaDestroyImage(allocator, hdr_image, hdr_allocation);
    hdr_view = VK_NULL_HANDLE;
    hdr_image = VK_NULL_HANDLE;
    hdr_allocation = VK_NULL_HANDLE;
}

// ------------------------------------------------------------------ lighting uniforms

Status Renderer::Impl::ensure_frame_uniforms() {
    if (frame_set_layout != VK_NULL_HANDLE) return {};
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &frame_set_layout));
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = kFramesInFlight;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    NOOR_VK_TRY(vkCreateDescriptorPool(device, &pool_info, nullptr, &frame_pool));
    std::array<VkDescriptorSetLayout, kFramesInFlight> layouts;
    layouts.fill(frame_set_layout);
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = frame_pool;
    set_info.descriptorSetCount = kFramesInFlight;
    set_info.pSetLayouts = layouts.data();
    NOOR_VK_TRY(vkAllocateDescriptorSets(device, &set_info, frame_sets.data()));
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        auto buffer = create_buffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, false);
        if (!buffer) return std::unexpected(buffer.error());
        frame_ubos[i] = *buffer;
        set_name(VK_OBJECT_TYPE_BUFFER, frame_ubos[i].buffer, std::format("Frame lighting uniforms (slot {})", i));
        VkDescriptorBufferInfo buffer_info{frame_ubos[i].buffer, 0, sizeof(FrameUniforms)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame_sets[i];
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    return {};
}

void Renderer::Impl::update_frame_uniforms(const FrameDesc& frame) {
    const Lighting& l = frame.lighting;
    const float exposure = l.exposure();
    const math::Vec3 eye = math::inverse(frame.camera.view).transform_point({0, 0, 0});
    const math::Vec3 to_sun = math::normalize(l.sun_direction * -1.0f);
    const math::Vec3 sun = l.sun_color * (l.sun_illuminance * exposure);
    const math::Vec3 sky = l.sky_color * (l.sky_luminance * exposure);
    const math::Vec3 ground = l.ground_color * (l.sky_luminance * exposure);
    const FrameUniforms u{{eye.x, eye.y, eye.z, 1},
                          {to_sun.x, to_sun.y, to_sun.z, 0},
                          {sun.x, sun.y, sun.z, 0},
                          {sky.x, sky.y, sky.z, 0},
                          {ground.x, ground.y, ground.z, 0}};
    GpuBuffer& ubo = frame_ubos[frame_index];
    std::memcpy(ubo.mapped, &u, sizeof(u));
    vmaFlushAllocation(allocator, ubo.allocation, 0, sizeof(u));
}

// ------------------------------------------------------------------ tonemapping

struct TonemapPush {
    float clear_color[4];
    std::uint32_t mode[4];  // x: tonemapper, y: 1 = show the clear colour where nothing was drawn
};
static_assert(sizeof(TonemapPush) == 32);

Status Renderer::Impl::ensure_tonemap(VkFormat color_format) {
    if (tonemap.pipeline != VK_NULL_HANDLE && tonemap.color_format == color_format) return {};
    if (tonemap.set == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_NEAREST;  // same size as the screen: one texel per pixel
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        NOOR_VK_TRY(vkCreateSampler(device, &sampler_info, nullptr, &tonemap.sampler));
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set_layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_layout_info.bindingCount = 1;
        set_layout_info.pBindings = &binding;
        NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &set_layout_info, nullptr, &tonemap.set_layout));
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        NOOR_VK_TRY(vkCreateDescriptorPool(device, &pool_info, nullptr, &tonemap.pool));
        VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set_info.descriptorPool = tonemap.pool;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &tonemap.set_layout;
        NOOR_VK_TRY(vkAllocateDescriptorSets(device, &set_info, &tonemap.set));
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TonemapPush)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &tonemap.set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        NOOR_VK_TRY(vkCreatePipelineLayout(device, &layout_info, nullptr, &tonemap.layout));
        if (hdr_view != VK_NULL_HANDLE) write_tonemap_descriptor();
    }
    if (tonemap.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, tonemap.pipeline, nullptr);
        tonemap.pipeline = VK_NULL_HANDLE;
    }

    auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& out) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device, &info, nullptr, &out);
    };
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    NOOR_VK_TRY(make_module(k_tonemap_vs, sizeof(k_tonemap_vs), vs));
    if (VkResult r = make_module(k_tonemap_ps, sizeof(k_tonemap_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return vk_failure(r, "vkCreateShaderModule (tonemap pixel shader)");
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", nullptr};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};  // none
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};  // off
    VkPipelineColorBlendAttachmentState blend_attachment{};
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
    rendering.depthAttachmentFormat = kDepthFormat;  // the screen pass has depth (for the gizmos)
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
    pipeline_info.layout = tonemap.layout;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &tonemap.pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) return vk_failure(result, "vkCreateGraphicsPipelines (tonemap)");
    tonemap.color_format = color_format;
    set_name(VK_OBJECT_TYPE_PIPELINE, tonemap.pipeline, "Tonemap pipeline (HDR -> screen)");
    return {};
}

void Renderer::Impl::write_tonemap_descriptor() {
    VkDescriptorImageInfo image{tonemap.sampler, hdr_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = tonemap.set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void Renderer::Impl::record_tonemap(VkCommandBuffer cmd, const Lighting& lighting) {
    begin_label(cmd, "Tonemap", {0.95f, 0.76f, 0.31f, 1.0f});  // gold
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap.layout, 0, 1, &tonemap.set, 0, nullptr);
    TonemapPush push{};
    std::memcpy(push.clear_color, desc.clear_color.data(), sizeof(push.clear_color));
    push.mode[0] = static_cast<std::uint32_t>(lighting.tonemapper);
    push.mode[1] = lighting.sky_background ? 0u : 1u;
    vkCmdPushConstants(cmd, tonemap.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ++stats.draw_calls;
    end_label(cmd);
}

void Renderer::Impl::destroy_frame_resources() {
    if (device == VK_NULL_HANDLE) return;
    for (auto& ubo : frame_ubos)
        if (ubo.buffer != VK_NULL_HANDLE) destroy_buffer(ubo);
    if (frame_pool) vkDestroyDescriptorPool(device, frame_pool, nullptr);
    if (frame_set_layout) vkDestroyDescriptorSetLayout(device, frame_set_layout, nullptr);
    frame_pool = VK_NULL_HANDLE;
    frame_set_layout = VK_NULL_HANDLE;
    frame_sets = {};
    if (tonemap.pipeline) vkDestroyPipeline(device, tonemap.pipeline, nullptr);
    if (tonemap.layout) vkDestroyPipelineLayout(device, tonemap.layout, nullptr);
    if (tonemap.pool) vkDestroyDescriptorPool(device, tonemap.pool, nullptr);
    if (tonemap.set_layout) vkDestroyDescriptorSetLayout(device, tonemap.set_layout, nullptr);
    if (tonemap.sampler) vkDestroySampler(device, tonemap.sampler, nullptr);
    tonemap = {};
}

} // namespace noor::render
