// Water (Milestone 8D, part 1, ADR-037). When water is in view, the scene pass is split in two: the opaque scene and
// the sky first; then its colour is copied and its depth resolved into images water can read (set 0, bindings 7 and
// 8); then the see-through pass draws the water surfaces (water.hlsl: absorption by thickness, refraction, a soft
// shore, the sky's reflection, the sun's glint) before glass and particles. Without water nothing of this runs.
// Part 3: before the surfaces, each body's wet ground (a flat band a little above the water, multiplying what's under
// it); each draw's further settings in a storage buffer (set 0, binding 9) and its depth map in the texture table.
#include "vulkan_impl.h"

#include "noor/shaders/water_ps.h"
#include "noor/shaders/water_vs.h"
#include "noor/shaders/water_wet_ps.h"
#include "noor/shaders/water_wet_vs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

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

// water.hlsl's WaterSettings (std430), one per draw, indexed by WaterPush::grid[3].
struct WaterGpu {
    float map[4];               // xy: world (x, z) of the depth map's sample (0, 0), z: metres between samples
    std::uint32_t map_info[4];  // x, y: samples across, z: texture slot, w: 1 = has a map
    float foam[4];              // x: foam, y: foam depth (m), z: crest foam, w: wet (darkening)
    float shore[4];             // x: wet height (m), y: reflections, z: the wet band's height above the level (m), w: unused
};
static_assert(sizeof(WaterGpu) == 64);
constexpr std::uint32_t kMaxWaterDraws = 64;
constexpr float kWetBandRadius = 400.0f;  // a sea's wet band: metres round the camera

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

// ------------------------------------------------------------------ settings and depth maps (part 3)

Status Renderer::Impl::create_water_buffers() {
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (water_buffers[i].buffer == VK_NULL_HANDLE) {
            auto buffer = create_buffer(kMaxWaterDraws * sizeof(WaterGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, false);
            if (!buffer) return std::unexpected(buffer.error());
            water_buffers[i] = *buffer;
            std::memset(water_buffers[i].mapped, 0, kMaxWaterDraws * sizeof(WaterGpu));
            vmaFlushAllocation(allocator, water_buffers[i].allocation, 0, VK_WHOLE_SIZE);
            set_name(VK_OBJECT_TYPE_BUFFER, water_buffers[i].buffer, std::format("Water draws' settings (slot {})", i));
        }
        const VkDescriptorBufferInfo buffer_info{water_buffers[i].buffer, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame_sets[i];
        write.dstBinding = 9;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    return {};
}

void Renderer::Impl::destroy_water_resources() {
    for (auto& b : water_buffers)
        if (b.buffer != VK_NULL_HANDLE) destroy_buffer(b);
    water_buffers = {};
    for (auto& m : water_maps) destroy_texture(m.texture);
    water_maps.clear();
}

void Renderer::Impl::prepare_water(const FrameDesc& frame) {
    if (frame.water.empty() || water_buffers[frame_index].buffer == VK_NULL_HANDLE) return;
    ++water_frame;
    auto* out = static_cast<WaterGpu*>(water_buffers[frame_index].mapped);
    const std::size_t count = std::min<std::size_t>(frame.water.size(), kMaxWaterDraws);
    for (std::size_t i = 0; i < count; ++i) {
        const WaterDraw& w = frame.water[i];
        WaterGpu g{};
        if (const auto& map = w.depth_map; map && map->nx >= 2 && map->nz >= 2 && map->depth.size() == std::size_t{map->nx} * map->nz) {
            auto it = std::ranges::find_if(water_maps, [&](const WaterMapTexture& t) { return t.map.get() == map.get(); });
            if (it == water_maps.end()) {
                const TextureMipData mip{map->nx, map->nz,
                                         {reinterpret_cast<const std::uint8_t*>(map->depth.data()), map->depth.size() * sizeof(float)}};
                const TextureDesc map_desc{TextureFormat::R32Float, std::span(&mip, 1), "Water depth map"};
                if (auto tex = create_texture(map_desc, kAnySlot)) {
                    water_maps.push_back({map, *tex, 0});
                    it = water_maps.end() - 1;
                } else {
                    NOOR_LOG_WARN("Render", "Water depth map not uploaded: {}", tex.error().describe());
                }
            }
            if (it != water_maps.end()) {
                it->last_used = water_frame;
                g.map[0] = map->origin.x;
                g.map[1] = map->origin.y;
                g.map[2] = map->cell;
                g.map_info[0] = map->nx;
                g.map_info[1] = map->nz;
                g.map_info[2] = it->texture.id;
                g.map_info[3] = 1;
            }
        }
        g.foam[0] = std::clamp(w.foam, 0.0f, 1.0f);
        g.foam[1] = std::max(0.01f, w.foam_depth);
        g.foam[2] = std::clamp(w.crest_foam, 0.0f, 1.0f);
        g.foam[3] = std::clamp(w.wet, 0.0f, 1.0f);
        g.shore[0] = std::max(0.0f, w.wet_height);
        g.shore[1] = std::clamp(w.reflections, 0.0f, 1.0f);
        // The band reaches over the highest the waves can lift the water (the four amplitudes: 0.97 of the main
        // wave's height) and the wet height above that.
        g.shore[2] = 0.97f * std::max(0.0f, w.wave_height) + g.shore[0] + 0.01f;
        out[i] = g;
    }
    vmaFlushAllocation(allocator, water_buffers[frame_index].allocation, 0, VK_WHOLE_SIZE);
    // Maps no longer drawn (a terrain sculpted, a body moved) go after a few frames.
    std::erase_if(water_maps, [&](WaterMapTexture& t) {
        if (water_frame - t.last_used <= kFramesInFlight + 1) return false;
        destroy_texture(t.texture);
        return true;
    });
}

// ------------------------------------------------------------------ the water pipeline

Status Renderer::Impl::ensure_water_pipeline() {
    if (water_pipeline != VK_NULL_HANDLE) return {};
    if (auto s = ensure_texture_table(); !s) return s;
    if (water_layout == VK_NULL_HANDLE) {
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterPush)};
        const VkDescriptorSetLayout set_layouts[2] = {frame_set_layout, texture_set_layout};  // depth maps: the texture table
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
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
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &water_pipeline);
    if (result == VK_SUCCESS) {
        // Wet ground: a band drawn before the water, multiplying the ground under it (no depth written).
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, ps, nullptr);
        vs = ps = VK_NULL_HANDLE;
        result = make_module(k_water_wet_vs, sizeof(k_water_wet_vs), vs);
        if (result == VK_SUCCESS) result = make_module(k_water_wet_ps, sizeof(k_water_wet_ps), ps);
        stages[0].module = vs;
        stages[1].module = ps;
        stages[0].pName = "VSWet";
        stages[1].pName = "PSWet";
        depth.depthWriteEnable = VK_FALSE;
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        if (result == VK_SUCCESS) result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &water_wet_pipeline);
    }
    if (vs) vkDestroyShaderModule(device, vs, nullptr);
    if (ps) vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) {
        destroy_water_pipeline();
        return vk_failure(result, "vkCreateGraphicsPipelines (water)");
    }
    set_name(VK_OBJECT_TYPE_PIPELINE, water_pipeline, "Water pipeline (surfaces over the copied scene)");
    set_name(VK_OBJECT_TYPE_PIPELINE, water_wet_pipeline, "Water pipeline (wet ground at the waterline)");
    return {};
}

void Renderer::Impl::destroy_water_pipeline() {
    if (water_pipeline) vkDestroyPipeline(device, water_pipeline, nullptr);
    if (water_wet_pipeline) vkDestroyPipeline(device, water_wet_pipeline, nullptr);
    water_pipeline = water_wet_pipeline = VK_NULL_HANDLE;
}

void Renderer::Impl::record_water(VkCommandBuffer cmd, const FrameDesc& frame) {
    if (frame.water.empty() || water_pipeline == VK_NULL_HANDLE) return;
    begin_label(cmd, "Water", {0.2f, 0.5f, 0.9f, 1.0f});
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    const VkDescriptorSet sets[2] = {frame_sets[frame_index], texture_set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_layout, 0, 2, sets, 0, nullptr);
    const std::size_t count = std::min<std::size_t>(frame.water.size(), kMaxWaterDraws);
    const auto push_of = [&](const WaterDraw& w, std::uint32_t index, std::uint32_t& vertices) {
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
        push.grid[3] = index;
        return push;
    };
    // Wet ground first: a flat band over each body (a sea: round the camera), multiplying the ground under it.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_wet_pipeline);
    for (std::size_t i = 0; i < count; ++i) {
        const WaterDraw& w = frame.water[i];
        if (w.wet <= 0.0f) continue;
        std::uint32_t vertices = 0;
        WaterPush push = push_of(w, static_cast<std::uint32_t>(i), vertices);
        if (w.endless) push.size[0] = push.size[1] = kWetBandRadius;
        vkCmdPushConstants(cmd, water_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        ++stats.draw_calls;
        stats.triangles += 2;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_pipeline);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t vertices = 0;
        const WaterPush push = push_of(frame.water[i], static_cast<std::uint32_t>(i), vertices);
        vkCmdPushConstants(cmd, water_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, vertices, 1, 0, 0);
        ++stats.draw_calls;
        ++stats.water_draws;
        stats.triangles += vertices / 3;
    }
    end_label(cmd);
}

} // namespace noor::render
