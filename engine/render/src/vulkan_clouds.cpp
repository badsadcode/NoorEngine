// Volumetric cloud layers on the GPU (Milestone 8A, part 7b, ADR-034): the noise tile, baked once, and the march, a
// compute pass at half resolution each frame there's a volume layer. The sky pass reads the result (frame set
// binding 6) and lays it over the sky. shaders/clouds_volume.hlsl has both passes; clouds.h is the reference.
//
// The half-resolution targets live as long as the swapchain (one per frame in flight, like everything a frame
// writes while the previous one may still be reading); they stay in VK_IMAGE_LAYOUT_GENERAL, written by compute
// and sampled by the sky pass. Without a volume layer nothing is dispatched and the sky pass never reads them.

#include "vulkan_impl.h"

#include "noor/shaders/clouds_volume_march.h"
#include "noor/shaders/clouds_volume_noise.h"

#include <format>

namespace noor::render {
namespace {

constexpr std::uint32_t kNoiseSize = 128;  // clouds_volume.hlsl kNoiseSize: the tile, 32 texels per first-octave cell

void global_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                    VkAccessFlags2 dst_access) {
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

Status Renderer::Impl::create_cloud_volume_targets() {
    auto& cv = cloud_volume;
    if (cv.sampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        NOOR_VK_TRY(vkCreateSampler(device, &sampler_info, nullptr, &cv.sampler));
        set_name(VK_OBJECT_TYPE_SAMPLER, cv.sampler, "Cloud volume (upsampling)");
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        NOOR_VK_TRY(vkCreateSampler(device, &sampler_info, nullptr, &cv.noise_sampler));
        set_name(VK_OBJECT_TYPE_SAMPLER, cv.noise_sampler, "Cloud volume noise (tiles)");
    }
    cv.size = {std::max(1u, (extent.width + 1) / 2), std::max(1u, (extent.height + 1) / 2)};
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        auto& t = cv.targets[i];
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        image_info.extent = {cv.size.width, cv.size.height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc, &t.image, &t.allocation, nullptr));
        set_name(VK_OBJECT_TYPE_IMAGE, t.image, std::format("Cloud volume (half resolution, slot {})", i));
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = t.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = image_info.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        NOOR_VK_TRY(vkCreateImageView(device, &view, nullptr, &t.view));
        t.ready = false;  // cleared to "no cloud" by the first frame that uses this slot
        VkDescriptorImageInfo image{cv.sampler, t.view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame_sets[i];
        write.dstBinding = 6;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    return {};
}

void Renderer::Impl::destroy_cloud_volume_targets() {
    if (device == VK_NULL_HANDLE) return;
    for (auto& t : cloud_volume.targets) {
        if (t.view) vkDestroyImageView(device, t.view, nullptr);
        if (t.image) vmaDestroyImage(allocator, t.image, t.allocation);
        t = {};
    }
}

Status Renderer::Impl::ensure_cloud_volume() {
    auto& cv = cloud_volume;
    if (cv.march != VK_NULL_HANDLE) return {};
    // The tile: 128^3 RGBA8 (r lumps, g erosion; rgba8 is the storage format every GPU can write).
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_3D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {kNoiseSize, kNoiseSize, kNoiseSize};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc, &cv.noise, &cv.noise_allocation, nullptr));
    set_name(VK_OBJECT_TYPE_IMAGE, cv.noise, "Cloud volume noise (128^3 tile)");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = cv.noise;
    view.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view.format = image_info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NOOR_VK_TRY(vkCreateImageView(device, &view, nullptr, &cv.noise_view));
    cv.noise_ready = false;

    // Layouts: the bake writes the tile (binding 4); the march reads the frame, the clouds' light and the tile and
    // writes a target (bindings 0-3). The same numbers as clouds_volume.hlsl.
    const VkDescriptorSetLayoutBinding noise_binding{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1;
    layout_info.pBindings = &noise_binding;
    NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &cv.noise_layout));
    const VkDescriptorSetLayoutBinding march_bindings[4] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    layout_info.bindingCount = 4;
    layout_info.pBindings = march_bindings;
    NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &cv.march_layout));
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &cv.noise_layout;
    NOOR_VK_TRY(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &cv.noise_pipeline_layout));
    pipeline_layout_info.pSetLayouts = &cv.march_layout;
    NOOR_VK_TRY(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &cv.march_pipeline_layout));

    const auto make_pipeline = [&](const std::uint8_t* code, std::size_t size, const char* entry, VkPipelineLayout layout,
                                   VkPipeline& out) -> Status {
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = size;
        module_info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        VkShaderModule module = VK_NULL_HANDLE;
        NOOR_VK_TRY(vkCreateShaderModule(device, &module_info, nullptr, &module));
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, entry, nullptr};
        info.layout = layout;
        const VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &out);
        vkDestroyShaderModule(device, module, nullptr);
        if (r != VK_SUCCESS) return vk_failure(r, "vkCreateComputePipelines (cloud volume)");
        set_name(VK_OBJECT_TYPE_PIPELINE, out, std::format("Cloud volume: {}", entry));
        return {};
    };
    if (auto r = make_pipeline(k_clouds_volume_noise, sizeof(k_clouds_volume_noise), "CSNoise", cv.noise_pipeline_layout, cv.bake); !r) return r;
    if (auto r = make_pipeline(k_clouds_volume_march, sizeof(k_clouds_volume_march), "CSMarch", cv.march_pipeline_layout, cv.march); !r) return r;

    const VkDescriptorPoolSize sizes[4] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight},
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kFramesInFlight + 1}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = kFramesInFlight + 1;
    pool_info.poolSizeCount = 4;
    pool_info.pPoolSizes = sizes;
    NOOR_VK_TRY(vkCreateDescriptorPool(device, &pool_info, nullptr, &cv.pool));
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = cv.pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &cv.noise_layout;
    NOOR_VK_TRY(vkAllocateDescriptorSets(device, &set_info, &cv.noise_set));
    std::array<VkDescriptorSetLayout, kFramesInFlight> layouts;
    layouts.fill(cv.march_layout);
    set_info.descriptorSetCount = kFramesInFlight;
    set_info.pSetLayouts = layouts.data();
    NOOR_VK_TRY(vkAllocateDescriptorSets(device, &set_info, cv.march_sets.data()));
    VkDescriptorImageInfo noise_image{VK_NULL_HANDLE, cv.noise_view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = cv.noise_set;
    write.dstBinding = 4;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &noise_image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return {};
}

Status Renderer::Impl::record_cloud_volume(VkCommandBuffer cmd, const FrameDesc& frame) {
    auto& cv = cloud_volume;
    auto& out = cv.targets[frame_index];
    if (out.image == VK_NULL_HANDLE) return {};
    if (!out.ready) {  // first use of this slot: "no cloud" (black, everything let through)
        image_barrier(cmd, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0,
                      VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkClearColorValue none{{0.0f, 0.0f, 0.0f, 1.0f}};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, out.image, VK_IMAGE_LAYOUT_GENERAL, &none, 1, &range);
        image_barrier(cmd, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        out.ready = true;
    }
    // Volumes are lit by the live sky's cloud light: without the live sky there are no clouds at all.
    bool volume = false;
    if (live_sky.active)
        for (const CloudLayer& layer : frame.lighting.clouds) volume = volume || (layer.enabled && layer.volume && layer.coverage > 0.0f);
    cv.active = volume;
    if (!volume) return {};
    if (auto s = ensure_cloud_volume(); !s) return s;
    begin_label(cmd, "Cloud volume (half resolution)", {0.95f, 0.95f, 1.0f, 1.0f});
    if (!cv.noise_ready) {
        image_barrier(cmd, cv.noise, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cv.bake);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cv.noise_pipeline_layout, 0, 1, &cv.noise_set, 0, nullptr);
        vkCmdDispatch(cmd, kNoiseSize / 4, kNoiseSize / 4, kNoiseSize / 4);
        cv.noise_ready = true;  // the barrier below makes it readable by the march
    }
    // This frame's descriptors: its uniforms, its copy's cloud light, the tile, its target.
    const VkDescriptorSet set = cv.march_sets[frame_index];
    const VkBuffer light = frame_cloud_light[frame_index] != VK_NULL_HANDLE ? frame_cloud_light[frame_index] : cloud_light_default.buffer;
    VkDescriptorBufferInfo ubo_info{frame_ubos[frame_index].buffer, 0, sizeof(FrameUniforms)};
    VkDescriptorBufferInfo light_info{light, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo noise_info{cv.noise_sampler, cv.noise_view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo target_info{VK_NULL_HANDLE, out.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[4]{};
    for (std::uint32_t i = 0; i < 4; ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &ubo_info;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &light_info;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &noise_info;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].pImageInfo = &target_info;
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    // The live sky's compute (the cloud light, the tile) before the march; the sky pass that last read this target
    // (two frames ago, behind this slot's fence) before the march overwrites it.
    global_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cv.march);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cv.march_pipeline_layout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(cmd, (cv.size.width + 7) / 8, (cv.size.height + 7) / 8, 1);
    image_barrier(cmd, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    end_label(cmd);
    return {};
}

void Renderer::Impl::destroy_cloud_volume() {
    if (device == VK_NULL_HANDLE) return;
    destroy_cloud_volume_targets();
    auto& cv = cloud_volume;
    if (cv.march) vkDestroyPipeline(device, cv.march, nullptr);
    if (cv.bake) vkDestroyPipeline(device, cv.bake, nullptr);
    if (cv.march_pipeline_layout) vkDestroyPipelineLayout(device, cv.march_pipeline_layout, nullptr);
    if (cv.noise_pipeline_layout) vkDestroyPipelineLayout(device, cv.noise_pipeline_layout, nullptr);
    if (cv.pool) vkDestroyDescriptorPool(device, cv.pool, nullptr);
    if (cv.march_layout) vkDestroyDescriptorSetLayout(device, cv.march_layout, nullptr);
    if (cv.noise_layout) vkDestroyDescriptorSetLayout(device, cv.noise_layout, nullptr);
    if (cv.noise_view) vkDestroyImageView(device, cv.noise_view, nullptr);
    if (cv.noise) vmaDestroyImage(allocator, cv.noise, cv.noise_allocation);
    if (cv.sampler) vkDestroySampler(device, cv.sampler, nullptr);
    if (cv.noise_sampler) vkDestroySampler(device, cv.noise_sampler, nullptr);
    cv = {};
}

} // namespace noor::render
