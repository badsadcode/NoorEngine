// The environment (Milestone 5, part 5): a prefiltered cubemap (frame set, binding 2) for specular
// image-based lighting, irradiance spherical harmonics in the frame uniforms, the BRDF lookup table
// (built-in texture slot 3), and the sky pass that draws the background texture behind the scene.
#include "vulkan_impl.h"

#include "noor/render/ibl.h"
#include "noor/shaders/sky_ps.h"
#include "noor/shaders/sky_vs.h"

#include <bit>
#include <cstring>

namespace noor::render {

namespace {

std::uint16_t half_of(float value) {
    const std::uint32_t f = std::bit_cast<std::uint32_t>(std::clamp(value, 0.0f, 65000.0f));
    const int exponent = static_cast<int>((f >> 23) & 0xFF) - 127 + 15;
    if (exponent <= 0) return 0;
    std::uint32_t half = (static_cast<std::uint32_t>(exponent) << 10) | ((f & 0x007FFFFFu) >> 13);
    if ((f & 0x1FFFu) >= 0x1000u) ++half;
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(half, 0x7BFFu));
}

} // namespace

Status Renderer::Impl::create_brdf_lut() {
    constexpr std::uint32_t kSize = 64;
    const std::vector<float> lut = brdf_lut(kSize, 256);
    std::vector<std::uint16_t> texels(static_cast<std::size_t>(kSize) * kSize * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kSize) * kSize; ++i) {
        texels[i * 4] = half_of(lut[i * 2]);
        texels[i * 4 + 1] = half_of(lut[i * 2 + 1]);
        texels[i * 4 + 2] = 0;
        texels[i * 4 + 3] = half_of(1.0f);
    }
    const TextureMipData mip{kSize, kSize, {reinterpret_cast<const std::uint8_t*>(texels.data()), texels.size() * 2}};
    if (auto h = create_texture({TextureFormat::Rgba16Float, {&mip, 1}, "Built-in BRDF lookup table"}, kTextureBrdfLut); !h)
        return std::unexpected(h.error());
    return {};
}

Status Renderer::Impl::upload_environment(const EnvironmentDesc& d) {
    if (d.face_size == 0 || d.mip_count == 0 || d.mip_count > 16)
        return fail(ErrorCode::InvalidArgument, "environment: no faces or a bad mip count");
    std::size_t expected = 0;
    for (std::uint32_t m = 0; m < d.mip_count; ++m) {
        const std::size_t s = std::max(1u, d.face_size >> m);
        expected += s * s * 4 * 6;
    }
    if (d.cube.size() != expected)
        return fail(ErrorCode::InvalidArgument, std::format("environment: {} texel values, expected {}", d.cube.size(), expected));

    vkDeviceWaitIdle(device);  // frames in flight read the old one
    destroy_environment_image();

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image_info.extent = {d.face_size, d.face_size, 1};
    image_info.mipLevels = d.mip_count;
    image_info.arrayLayers = 6;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc_info, &environment.image, &environment.allocation, nullptr));
    set_name(VK_OBJECT_TYPE_IMAGE, environment.image, std::format("Environment cube ({}², {} mips)", d.face_size, d.mip_count));

    const VkDeviceSize bytes = d.cube.size_bytes();
    auto staging = create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false);
    if (!staging) return std::unexpected(staging.error());
    std::memcpy(staging->mapped, d.cube.data(), bytes);
    vmaFlushAllocation(allocator, staging->allocation, 0, bytes);
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize offset = 0;
    for (std::uint32_t m = 0; m < d.mip_count; ++m) {
        const std::uint32_t s = std::max(1u, d.face_size >> m);
        for (std::uint32_t face = 0; face < 6; ++face) {
            VkBufferImageCopy r{};
            r.bufferOffset = offset;
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, face, 1};
            r.imageExtent = {s, s, 1};
            regions.push_back(r);
            offset += static_cast<VkDeviceSize>(s) * s * 8;
        }
    }

    VkCommandBufferAllocateInfo cmd_alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc.commandPool = upload_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkResult r = vkAllocateCommandBuffers(device, &cmd_alloc, &cmd);
    if (r == VK_SUCCESS) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = environment.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, d.mip_count, 0, 6};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
        vkCmdCopyBufferToImage(cmd, staging->buffer, environment.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(regions.size()), regions.data());
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(cmd, &dep);
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
    if (fence) vkDestroyFence(device, fence, nullptr);
    if (cmd) vkFreeCommandBuffers(device, upload_pool, 1, &cmd);
    destroy_buffer(*staging);
    if (r != VK_SUCCESS) return vk_failure(r, "environment upload");

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = environment.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, d.mip_count, 0, 6};
    NOOR_VK_TRY(vkCreateImageView(device, &view_info, nullptr, &environment.view));
    if (environment.sampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  // between roughness levels
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = VK_LOD_CLAMP_NONE;
        NOOR_VK_TRY(vkCreateSampler(device, &sampler_info, nullptr, &environment.sampler));
    }
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkDescriptorImageInfo image{environment.sampler, environment.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame_sets[i];
        write.dstBinding = 2;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    environment.mip_count = d.mip_count;
    environment.sh = d.irradiance_sh;
    environment.background = d.background;
    return {};
}

Status Renderer::Impl::ensure_default_environment() {
    if (environment.image != VK_NULL_HANDLE) return {};
    // A plain environment of radiance 0.5 everywhere: neutral light until a scene sets its own.
    const std::uint16_t grey = half_of(0.5f), one = half_of(1.0f);
    const std::vector<std::uint16_t> texels = {grey, grey, grey, one, grey, grey, grey, one, grey, grey, grey, one,
                                               grey, grey, grey, one, grey, grey, grey, one, grey, grey, grey, one};
    EnvironmentDesc d;
    d.face_size = 1;
    d.mip_count = 1;
    d.cube = texels;
    const float c0 = 0.5f * 3.14159265f * 3.5449077f;  // constant radiance L: c0 = pi * L * sqrt(4 pi)
    d.irradiance_sh[0] = {c0, c0, c0};
    return upload_environment(d);
}

void Renderer::Impl::destroy_environment_image() {
    if (environment.view) vkDestroyImageView(device, environment.view, nullptr);
    if (environment.image) vmaDestroyImage(allocator, environment.image, environment.allocation);
    environment.view = VK_NULL_HANDLE;
    environment.image = VK_NULL_HANDLE;
    environment.allocation = VK_NULL_HANDLE;
}

Status Renderer::Impl::ensure_sky_pipeline() {
    if (sky_pipeline != VK_NULL_HANDLE) return {};
    const auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& module) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device, &info, nullptr, &module);
    };
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    NOOR_VK_TRY(make_module(k_sky_vs, sizeof(k_sky_vs), vs));
    if (VkResult r = make_module(k_sky_ps, sizeof(k_sky_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return vk_failure(r, "vkCreateShaderModule (sky)");
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
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(std::max(1u, scene_samples));
    // At infinite distance (reverse-Z 0), only where nothing was drawn (the depth is still 0).
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    const VkFormat format = kHdrFormat;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
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
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &sky_pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) return vk_failure(result, "vkCreateGraphicsPipelines (sky)");
    set_name(VK_OBJECT_TYPE_PIPELINE, sky_pipeline, "Sky pipeline (environment background)");
    return {};
}

void Renderer::Impl::record_sky(VkCommandBuffer cmd, const Lighting& lighting) {
    if (!lighting.sky_background || !environment.background.valid() || environment.background.id >= textures.size() ||
        !textures[environment.background.id].alive)
        return;
    begin_label(cmd, "Sky", {0.42f, 0.6f, 1.0f, 1.0f});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline);
    const VkDescriptorSet sets[2] = {frame_sets[frame_index], texture_set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2, sets, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ++stats.draw_calls;
    end_label(cmd);
}

void Renderer::Impl::destroy_environment() {
    if (device == VK_NULL_HANDLE) return;
    destroy_environment_image();
    if (environment.sampler) vkDestroySampler(device, environment.sampler, nullptr);
    if (sky_pipeline) vkDestroyPipeline(device, sky_pipeline, nullptr);
    environment = {};
    sky_pipeline = VK_NULL_HANDLE;
}

} // namespace noor::render
