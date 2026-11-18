// The frame's HDR side (ADR-012): the 16-bit float image the scene is lit into, the per-frame
// lighting uniforms (sun, sky, exposure, camera), and the tonemap pass that turns the HDR image
// into display colours on the screen.
#include "vulkan_impl.h"

#include "noor/shaders/tonemap_ps.h"
#include "noor/shaders/tonemap_vs.h"

#include <algorithm>
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

Status Renderer::Impl::create_aerial_default() {
    VkImageCreateInfo volume_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    volume_info.imageType = VK_IMAGE_TYPE_3D;
    volume_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    volume_info.extent = {1, 1, 1};
    volume_info.mipLevels = 1;
    volume_info.arrayLayers = 1;
    volume_info.samples = VK_SAMPLE_COUNT_1_BIT;
    volume_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    volume_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    NOOR_VK_TRY(vmaCreateImage(allocator, &volume_info, &alloc, &aerial_default.image, &aerial_default.allocation, nullptr));
    set_name(VK_OBJECT_TYPE_IMAGE, aerial_default.image, "Aerial perspective (none)");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = aerial_default.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view.format = volume_info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NOOR_VK_TRY(vkCreateImageView(device, &view, nullptr, &aerial_default.view));
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    NOOR_VK_TRY(vkCreateSampler(device, &sampler, nullptr, &aerial_default.sampler));
    // Clear it to "no air": inscatter 0, transmittance 1.
    VkCommandBufferAllocateInfo cmd_alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc.commandPool = upload_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    NOOR_VK_TRY(vkAllocateCommandBuffers(device, &cmd_alloc, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    image_barrier(cmd, aerial_default.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue clear{{0.0f, 0.0f, 0.0f, 1.0f}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, aerial_default.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    image_barrier(cmd, aerial_default.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vkEndCommandBuffer(cmd);
    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    VkResult r = vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE);
    if (r == VK_SUCCESS) r = vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, upload_pool, 1, &cmd);
    if (r != VK_SUCCESS) return vk_failure(r, "aerial perspective default");
    return {};
}

void Renderer::Impl::write_cloud_light_descriptor(std::uint32_t frame_slot, VkBuffer buffer) {
    VkDescriptorBufferInfo light_info{buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame_sets[frame_slot];
    write.dstBinding = 5;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &light_info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void Renderer::Impl::write_aerial_descriptor(std::uint32_t frame_slot, VkImageView view, VkImageLayout layout) {
    VkDescriptorImageInfo image{aerial_default.sampler, view, layout};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame_sets[frame_slot];
    write.dstBinding = 4;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

Status Renderer::Impl::ensure_frame_uniforms() {
    if (frame_set_layout != VK_NULL_HANDLE) return {};
    const VkDescriptorSetLayoutBinding bindings[6] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // sun shadow map
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // environment cube
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},          // environment irradiance (SH9)
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // aerial perspective volume
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},          // the clouds' light
    };
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 6;
    layout_info.pBindings = bindings;
    NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &frame_set_layout));
    const VkDescriptorPoolSize pool_sizes[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
                                                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight * 3},
                                                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight * 2}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = kFramesInFlight;
    pool_info.poolSizeCount = 3;
    pool_info.pPoolSizes = pool_sizes;
    NOOR_VK_TRY(vkCreateDescriptorPool(device, &pool_info, nullptr, &frame_pool));
    std::array<VkDescriptorSetLayout, kFramesInFlight> layouts;
    layouts.fill(frame_set_layout);
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = frame_pool;
    set_info.descriptorSetCount = kFramesInFlight;
    set_info.pSetLayouts = layouts.data();
    NOOR_VK_TRY(vkAllocateDescriptorSets(device, &set_info, frame_sets.data()));
    auto irradiance = create_buffer(9 * sizeof(math::Vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, false);
    if (!irradiance) return std::unexpected(irradiance.error());
    irradiance_buffer = *irradiance;
    set_name(VK_OBJECT_TYPE_BUFFER, irradiance_buffer.buffer, "Environment irradiance (SH9)");
    if (auto s = create_aerial_default(); !s) return s;
    auto cloud_light = create_buffer(4 * sizeof(math::Vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, false);
    if (!cloud_light) return std::unexpected(cloud_light.error());
    cloud_light_default = *cloud_light;
    std::memset(cloud_light_default.mapped, 0, 4 * sizeof(math::Vec4));
    vmaFlushAllocation(allocator, cloud_light_default.allocation, 0, VK_WHOLE_SIZE);
    set_name(VK_OBJECT_TYPE_BUFFER, cloud_light_default.buffer, "Cloud light (none)");
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkDescriptorBufferInfo sh_info{irradiance_buffer.buffer, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet sh_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        sh_write.dstSet = frame_sets[i];
        sh_write.dstBinding = 3;
        sh_write.descriptorCount = 1;
        sh_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sh_write.pBufferInfo = &sh_info;
        vkUpdateDescriptorSets(device, 1, &sh_write, 0, nullptr);
        write_aerial_descriptor(i, aerial_default.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        write_cloud_light_descriptor(i, cloud_light_default.buffer);
    }
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
    const float aspect_ratio = static_cast<float>(extent.width) / static_cast<float>(std::max(1u, extent.height));
    const bool shadows_on = frame.lighting.sun_shadows && frame.lighting.sun_illuminance > 0.0f && shadow_size > 0;
    if (shadows_on) {
        ShadowCascadeParams params;
        params.shadow_distance = std::max(1.0f, frame.lighting.shadow_distance);
        params.resolution = shadow_size;
        cascades = compute_cascades(frame.camera, aspect_ratio, frame.lighting.sun_direction, params);
    }
    const Lighting& l = frame.lighting;
    const float exposure = l.exposure();
    const math::Vec3 eye = math::inverse(frame.camera.view).transform_point({0, 0, 0});
    const math::Vec3 to_sun = math::normalize(l.sun_direction * -1.0f);
    const math::Vec3 sun = l.sun_color * (l.sun_illuminance * exposure);
    const math::Vec3 sky = l.sky_color * (l.sky_luminance * exposure);
    const math::Vec3 ground = l.ground_color * (l.sky_luminance * exposure);
    const math::Mat4 clip_from_world =
        math::perspective_reverse_z(frame.camera.fov_y, aspect_ratio, frame.camera.near_plane) * frame.camera.view;
    FrameUniforms u{};
    u.clip_from_world = clip_from_world;
    u.camera_position = {eye.x, eye.y, eye.z, 1};
    u.to_sun = {to_sun.x, to_sun.y, to_sun.z, 0};
    u.sun_radiance = {sun.x, sun.y, sun.z, 0};
    u.sky_radiance = {sky.x, sky.y, sky.z, 0};
    u.ground_radiance = {ground.x, ground.y, ground.z, 0};
    for (std::uint32_t i = 0; i < kShadowCascades; ++i) u.cascades[i] = cascades[i].clip_from_world;
    u.cascade_splits = {cascades[0].split_far, cascades[1].split_far, cascades[2].split_far, cascades[3].split_far};
    u.cascade_texel = {cascades[0].texel_world, cascades[1].texel_world, cascades[2].texel_world, cascades[3].texel_world};
    u.shadow_params = {shadows_on ? 1.0f : 0.0f, static_cast<float>(shadow_size), frame.lighting.shadow_distance, 0};
    const math::Vec3 fwd = math::normalize(math::transform_vector(math::inverse(frame.camera.view), {0, 0, -1}));
    u.camera_forward = {fwd.x, fwd.y, fwd.z, 0};

    // Local lights: the ones nearest the camera (that reach it at all first), up to kMaxLocalLights.
    std::vector<const LocalLight*> lights;
    for (const auto& light : frame.lighting.lights)
        if (light.lumens > 0.0f && light.range > 0.0f) lights.push_back(&light);
    std::ranges::sort(lights, [&](const LocalLight* a, const LocalLight* b) {
        return math::length(a->position - eye) - a->range < math::length(b->position - eye) - b->range;
    });
    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(lights.size(), kMaxLocalLights));
    u.light_count[0] = count;
    for (std::uint32_t i = 0; i < count; ++i) {
        const LocalLight& light = *lights[i];
        const math::Vec3 d = math::length(light.direction) > 1e-6f ? math::normalize(light.direction) : math::Vec3{0, -1, 0};
        const float outer = std::clamp(light.outer_angle, 0.01f, math::kPi * 0.5f);
        const float inner = std::clamp(light.inner_angle, 0.0f, outer - 0.001f);
        // Lumens to candela: a point light spreads them over the whole sphere (4π sr); a spot, like
        // a lamp with a reflector, puts them into its cone (2π(1 - cos outer) sr): narrower = brighter.
        const float solid_angle = light.type == LocalLight::Type::Spot ? 2.0f * math::kPi * (1.0f - std::cos(outer)) : 4.0f * math::kPi;
        const float candela = light.lumens / solid_angle;
        const math::Vec3 c = light.color * (candela * exposure);
        u.lights[i] = {{light.position.x, light.position.y, light.position.z, light.range},
                       {c.x, c.y, c.z, 0},
                       {d.x, d.y, d.z, light.type == LocalLight::Type::Spot ? 1.0f : 0.0f},
                       {std::cos(outer), std::cos(inner), 0, 0}};
    }
    u.world_from_clip = math::inverse(clip_from_world);
    u.environment = {frame.lighting.environment_intensity * exposure, std::cos(frame.lighting.environment_rotation),
                     std::sin(frame.lighting.environment_rotation), static_cast<float>(environment.mip_count - 1)};
    u.background[0] = live_sky.active ? live_sky.copies[frame_index].sky_slot
                                      : environment.background.valid() ? environment.background.id : kTextureWhite;
    u.background[1] = frame.lighting.sun_disc ? 1u : 0u;
    const FogParams& fog = frame.lighting.fog;
    u.fog = {std::max(0.0f, fog.density), std::max(0.0f, fog.falloff), fog.height, std::clamp(fog.anisotropy, -0.95f, 0.95f)};
    u.fog_albedo = {fog.albedo.x, fog.albedo.y, fog.albedo.z, 0.0f};
    const bool aerial = live_sky.active && live_sky.desc.aerial_distance_scale > 0.0f;
    u.aerial = {aerial ? 1.0f : 0.0f, kAerialFarKm, std::max(0.0f, live_sky.desc.aerial_distance_scale), 0.0f};
    // Clouds (only with the live sky, which lights them): their drift after this much game time.
    CloudsGpu clouds{};
    if (live_sky.active) {
        for (std::size_t i = 0; i < kCloudLayers; ++i) {
            const CloudLayer& layer = frame.lighting.clouds[i];
            if (!layer.enabled || layer.coverage <= 0.0f) continue;
            const math::Vec2 drift = cloud_offset(layer, frame.lighting.time);
            clouds.a[i] = {std::clamp(layer.coverage, 0.0f, 1.0f), std::max(0.0f, layer.density), std::max(0.05f, layer.altitude_km),
                           std::max(0.05f, layer.scale_km)};
            clouds.b[i] = {drift.x, drift.y, 0.0f, 0.0f};
            clouds.flags[i] = layer.seed;
            clouds.flags[2 + i] = 1u;
        }
        clouds.b[0].z = live_sky.desc.planet_radius_km;
        clouds.b[0].w = std::max(0.0f, (live_sky.desc.altitude_m + eye.y) * 0.001f);
    }
    std::memcpy(u.cloud_a, clouds.a, sizeof(clouds.a));
    std::memcpy(u.cloud_b, clouds.b, sizeof(clouds.b));
    std::memcpy(u.cloud_flags, clouds.flags, sizeof(clouds.flags));
    frame_clouds = clouds;
    frame_world_from_clip = u.world_from_clip;
    frame_eye = eye;
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
    if (device != VK_NULL_HANDLE) {
        if (aerial_default.sampler) vkDestroySampler(device, aerial_default.sampler, nullptr);
        if (aerial_default.view) vkDestroyImageView(device, aerial_default.view, nullptr);
        if (aerial_default.image) vmaDestroyImage(allocator, aerial_default.image, aerial_default.allocation);
        aerial_default = {};
        destroy_buffer(cloud_light_default);
    }
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
