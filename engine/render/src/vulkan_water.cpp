// Water (Milestone 8D, part 1, ADR-037). When water is in view, the scene pass is split in two: the opaque scene and
// the sky first; then its colour is copied and its depth resolved into images water can read (set 0, bindings 7 and
// 8); then the see-through pass draws the water surfaces (water.hlsl: absorption by thickness, refraction, a soft
// shore, the sky's reflection, the sun's glint) before glass and particles. Without water nothing of this runs.
#include "vulkan_impl.h"

#include "noor/shaders/water_ps.h"
#include "noor/shaders/water_vs.h"

#include <algorithm>
#include <cmath>

namespace noor::render {

namespace {

// water.hlsl's push constants: 128 bytes (all Vulkan guarantees).
struct WaterPush {
    float center_yaw[4];   // xyz: the surface's middle (world; y is the water level), w: yaw (radians)
    float size[4];         // xy: half size along its x and z (m), z: ripple strength, w: metres per ripple
    float absorption[4];   // rgb: absorbed per metre, w: refraction
    float scatter[4];      // rgb: light scattered back from inside, w: the shore's fade depth (m)
    float params[4];       // x: roughness, y: time (s), z, w: 1 / the screen's size in pixels
    float waves[4];        // the main wave: height, length, direction (rad), choppiness
    float phases[4];       // the four waves' phases (rad)
    std::uint32_t grid[4]; // a box: x: cells per side; a sea (z = 1): x: segments round, y: rings out
};
static_assert(sizeof(WaterPush) == 128);

constexpr std::uint32_t kWaterGrid = 32;       // a calm box's cells per side
constexpr std::uint32_t kMaxWaterGrid = 512;   // a box with waves: cells small enough for its shortest wave, up to this
constexpr std::uint32_t kSeaSegments = 256;    // the sea's rings: segments round
constexpr float kSeaInner = 0.4f;              // the first ring (m), as water.hlsl
constexpr float kSeaRadius = 20000.0f;         // metres from the camera to the endless sea's edge

// How many rings reach kSeaRadius, each 1 + 2 pi / segments wider than the last.
std::uint32_t sea_rings() {
    const double growth = 1.0 + 2.0 * 3.14159265358979 / kSeaSegments;
    return 2u + static_cast<std::uint32_t>(std::ceil(std::log(kSeaRadius / kSeaInner) / std::log(growth)));
}

} // namespace

// ------------------------------------------------------------------ the scene behind the water

Status Renderer::Impl::create_scene_copies() {
    const auto make = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage& image, VmaAllocation& allocation,
                          VkImageView& view, const char* name) -> Status {
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = format;
        image_info.extent = {extent.width, extent.height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = usage;
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
        set_name(VK_OBJECT_TYPE_IMAGE, image, name);
        return {};
    };
    if (auto s = make(kHdrFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, scene_color_copy,
                      scene_color_copy_allocation, scene_color_copy_view, "The opaque scene's colour (for water)");
        !s)
        return s;
    // Depth: resolved into it at the end of a multisampled opaque pass, or copied into it from the plain depth buffer.
    if (auto s = make(kDepthFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_IMAGE_ASPECT_DEPTH_BIT, scene_depth_copy, scene_depth_copy_allocation, scene_depth_copy_view,
                      "The opaque scene's depth (for water)");
        !s)
        return s;
    if (scene_copy_sampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        NOOR_VK_TRY(vkCreateSampler(device, &sampler, nullptr, &scene_copy_sampler));
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        NOOR_VK_TRY(vkCreateSampler(device, &sampler, nullptr, &scene_depth_sampler));
    }
    write_scene_copy_descriptors();
    return {};
}

void Renderer::Impl::destroy_scene_copies() {
    if (scene_color_copy_view) vkDestroyImageView(device, scene_color_copy_view, nullptr);
    if (scene_color_copy) vmaDestroyImage(allocator, scene_color_copy, scene_color_copy_allocation);
    if (scene_depth_copy_view) vkDestroyImageView(device, scene_depth_copy_view, nullptr);
    if (scene_depth_copy) vmaDestroyImage(allocator, scene_depth_copy, scene_depth_copy_allocation);
    scene_color_copy_view = scene_depth_copy_view = VK_NULL_HANDLE;
    scene_color_copy = scene_depth_copy = VK_NULL_HANDLE;
    scene_color_copy_allocation = scene_depth_copy_allocation = VK_NULL_HANDLE;
}

void Renderer::Impl::write_scene_copy_descriptors() {
    if (frame_set_layout == VK_NULL_HANDLE || scene_color_copy_view == VK_NULL_HANDLE) return;
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        const VkDescriptorImageInfo color{scene_copy_sampler, scene_color_copy_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo depth{scene_depth_sampler, scene_depth_copy_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for (int k = 0; k < 2; ++k) {
            writes[k].dstSet = frame_sets[i];
            writes[k].dstBinding = 7 + static_cast<std::uint32_t>(k);
            writes[k].descriptorCount = 1;
            writes[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[k].pImageInfo = k == 0 ? &color : &depth;
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
}

// After the opaque pass: its resolved colour (in hdr_image) copied; its depth resolved already (multisampled) or
// copied from the plain depth buffer. Both left ready to sample; hdr_image and the depth buffer back as attachments.
void Renderer::Impl::record_scene_copy(VkCommandBuffer cmd, bool msaa) {
    begin_label(cmd, "Copy the scene for water", {0.2f, 0.6f, 0.9f, 1.0f});
    image_barrier(cmd, hdr_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT);
    image_barrier(cmd, scene_color_copy, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy color_region{};
    color_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    color_region.dstSubresource = color_region.srcSubresource;
    color_region.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(cmd, hdr_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scene_color_copy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &color_region);
    image_barrier(cmd, scene_color_copy, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    image_barrier(cmd, hdr_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    if (msaa) {
        image_barrier(cmd, scene_depth_copy, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    } else {
        image_barrier(cmd, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT);
        image_barrier(cmd, scene_depth_copy, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy depth_region{};
        depth_region.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        depth_region.dstSubresource = depth_region.srcSubresource;
        depth_region.extent = {extent.width, extent.height, 1};
        vkCmdCopyImage(cmd, depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scene_depth_copy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &depth_region);
        image_barrier(cmd, scene_depth_copy, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        image_barrier(cmd, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
    end_label(cmd);
}

// ------------------------------------------------------------------ the water pipeline

Status Renderer::Impl::ensure_water_pipeline() {
    if (water_pipeline != VK_NULL_HANDLE) return {};
    if (water_layout == VK_NULL_HANDLE) {
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterPush)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &frame_set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        NOOR_VK_TRY(vkCreatePipelineLayout(device, &layout_info, nullptr, &water_layout));
        set_name(VK_OBJECT_TYPE_PIPELINE_LAYOUT, water_layout, "Water pipeline layout");
    }
    const auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& module) {
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = size;
        module_info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device, &module_info, nullptr, &module);
    };
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    NOOR_VK_TRY(make_module(k_water_vs, sizeof(k_water_vs), vs));
    if (VkResult r = make_module(k_water_ps, sizeof(k_water_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return vk_failure(r, "vkCreateShaderModule (water)");
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", nullptr};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;  // seen from below too (part 4)
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(std::max(1u, scene_samples));
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;  // what's drawn after (glass, smoke) is hidden under the surface
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;  // reverse-Z
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    pipeline_info.layout = water_layout;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &water_pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) return vk_failure(result, "vkCreateGraphicsPipelines (water)");
    set_name(VK_OBJECT_TYPE_PIPELINE, water_pipeline, "Water pipeline (surfaces over the copied scene)");
    return {};
}

void Renderer::Impl::destroy_water_pipeline() {
    if (water_pipeline) vkDestroyPipeline(device, water_pipeline, nullptr);
    water_pipeline = VK_NULL_HANDLE;
}

void Renderer::Impl::record_water(VkCommandBuffer cmd, const FrameDesc& frame) {
    if (frame.water.empty() || water_pipeline == VK_NULL_HANDLE) return;
    begin_label(cmd, "Water", {0.2f, 0.5f, 0.9f, 1.0f});
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_layout, 0, 1, &frame_sets[frame_index], 0, nullptr);
    for (const WaterDraw& w : frame.water) {
        WaterPush push{};
        push.center_yaw[0] = w.center.x;
        push.center_yaw[1] = w.center.y;
        push.center_yaw[2] = w.center.z;
        push.center_yaw[3] = w.yaw;
        push.size[0] = w.half_size.x;
        push.size[1] = w.half_size.y;
        push.waves[0] = std::max(0.0f, w.wave_height);
        push.waves[1] = std::max(0.5f, w.wave_length);
        push.waves[2] = w.wave_direction;
        push.waves[3] = std::clamp(w.choppiness, 0.0f, 0.9f);
        for (int i = 0; i < 4; ++i) push.phases[i] = w.wave_phase[static_cast<std::size_t>(i)];
        std::uint32_t vertices = 0;
        if (w.endless) {
            // The sea: rings round the camera out to the horizon (the haze hides its edge long before), finer near.
            push.grid[0] = kSeaSegments;
            push.grid[1] = sea_rings();
            push.grid[2] = 1;
            vertices = kSeaSegments * push.grid[1] * 6;
        } else {
            // A box: with waves, cells of a sixth of its shortest wave (the smallest is 0.24 of the main one).
            std::uint32_t n = kWaterGrid;
            if (push.waves[0] > 0.0f) {
                const float cell = push.waves[1] * 0.24f / 6.0f;
                const float span = 2.0f * std::max(w.half_size.x, w.half_size.y);
                n = std::clamp(static_cast<std::uint32_t>(std::ceil(span / std::max(cell, 0.01f))), kWaterGrid, kMaxWaterGrid);
            }
            push.grid[0] = n;
            vertices = n * n * 6;
        }
        push.size[2] = w.ripple;
        push.size[3] = std::max(0.01f, w.ripple_scale);
        push.absorption[0] = w.absorption.x;
        push.absorption[1] = w.absorption.y;
        push.absorption[2] = w.absorption.z;
        push.absorption[3] = w.refraction;
        push.scatter[0] = w.scatter.x;
        push.scatter[1] = w.scatter.y;
        push.scatter[2] = w.scatter.z;
        push.scatter[3] = std::max(0.001f, w.shore_fade);
        push.params[0] = std::clamp(w.roughness, 0.0f, 1.0f);
        push.params[1] = static_cast<float>(std::fmod(w.time, 3600.0));
        push.params[2] = 1.0f / static_cast<float>(std::max(1u, extent.width));
        push.params[3] = 1.0f / static_cast<float>(std::max(1u, extent.height));
        vkCmdPushConstants(cmd, water_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, vertices, 1, 0, 0);
        ++stats.draw_calls;
        ++stats.water_draws;
        stats.triangles += vertices / 3;
    }
    end_label(cmd);
}

} // namespace noor::render
