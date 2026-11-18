// The live sky (Milestone 8A, part 2, ADR-033): the atmosphere's tables, the sky image and the
// environment's lighting (prefiltered cube, irradiance SH9) built by compute shaders
// (shaders/atmosphere.hlsl), so the sun can move every frame.
//
// Every frame (Milestone 8A, part 3): the camera's aerial perspective, a 32^3 volume of what the air adds
// and lets through along each pixel's view, which the scene's shader applies to every surface.
//
// Clouds (part 4) live in the sky image this copy lights the scene with (so overcast lights it grey); the sky
// pass draws them sharp itself, every frame, over a cloud-free background image. Drifting clouds rebuild the
// copy's lighting in steps (each 1% of their feature size), not every frame.
//
// What is rebuilt when:
//   - settings change (planet, air, haze, ground): the transmittance and multiple-scattering tables
//     (shared), then every copy below;
//   - the sun (or the observer's height) moves: this frame's copy of the sky image and its pyramid, the
//     cube's seven mips and the irradiance. In the frame that sees the change, before the shadow pass
//     (cost measured in docs/rendering/MEASUREMENTS.md: spreading it over frames wasn't needed).
//
// One copy of what the scene reads per frame in flight: frame N writes copy N % 2 while frame N - 1 may
// still draw with the other. The CPU's wait for the frame slot's fence already guarantees the copy is no
// longer read, so the compute work never waits for an earlier frame (a shared copy needed a barrier on
// earlier fragment work, which sits behind the swapchain's acquire: every frame waited a whole vsync).
//
// All of it stays in VK_IMAGE_LAYOUT_GENERAL (written by compute, sampled by the scene), so the only
// synchronisation is global memory barriers.
#include "vulkan_impl.h"

#include "noor/shaders/atmosphere_aerial.h"
#include "noor/shaders/atmosphere_cloudlight.h"
#include "noor/shaders/atmosphere_cube.h"
#include "noor/shaders/atmosphere_downsample.h"
#include "noor/shaders/atmosphere_irradiance.h"
#include "noor/shaders/atmosphere_multiple.h"
#include "noor/shaders/atmosphere_sky.h"
#include "noor/shaders/atmosphere_transmittance.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace noor::render {

namespace {

// Sizes shared with the CPU reference (asset::Atmosphere, asset::make_environment(sky, 64)).
constexpr std::uint32_t kTW = 256, kTH = 64, kMS = 32;
constexpr std::uint32_t kSkyW = 512, kSkyH = 256;
constexpr std::uint32_t kSkyLevels = 7;   // 512x256 down to 8x4, like make_environment's pyramid
constexpr std::uint32_t kCube = 64;
constexpr std::uint32_t kCubeMips = 7;    // 64 down to 1
constexpr std::uint32_t kIrradianceLevel = 1;  // make_environment projects SH from level 1 when wider than 256

using Push = LiveSkyPush;

Push push_of(const AtmosphereDesc& d) {
    Push p{};
    p.rayleigh = {d.rayleigh_scattering.x, d.rayleigh_scattering.y, d.rayleigh_scattering.z, d.rayleigh_height_km};
    p.mie = {d.mie_scattering, d.mie_absorption, d.mie_height_km, d.mie_g};
    p.ozone = {d.ozone_absorption.x, d.ozone_absorption.y, d.ozone_absorption.z, d.ozone_centre_km};
    p.planet = {d.planet_radius_km, d.planet_radius_km + d.atmosphere_height_km, d.ozone_half_width_km, std::clamp(d.turbidity, 0.0f, 50.0f)};
    p.ground = {d.ground_albedo.x, d.ground_albedo.y, d.ground_albedo.z, std::clamp(d.altitude_m * 0.001f, 0.0f, d.atmosphere_height_km)};
    const math::Vec3 sun = math::length(d.to_sun) > 1e-6f ? math::normalize(d.to_sun) : math::Vec3{0, 1, 0};
    p.sun = {sun.x, sun.y, sun.z, 0};
    return p;
}

bool same(math::Vec3 a, math::Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// The settings the tables depend on (everything but the sun and the observer's height).
bool same_tables(const AtmosphereDesc& a, const AtmosphereDesc& b) {
    return a.planet_radius_km == b.planet_radius_km && a.atmosphere_height_km == b.atmosphere_height_km &&
           same(a.rayleigh_scattering, b.rayleigh_scattering) && a.rayleigh_height_km == b.rayleigh_height_km &&
           a.mie_scattering == b.mie_scattering && a.mie_absorption == b.mie_absorption && a.mie_height_km == b.mie_height_km &&
           a.mie_g == b.mie_g && same(a.ozone_absorption, b.ozone_absorption) && a.ozone_centre_km == b.ozone_centre_km &&
           a.ozone_half_width_km == b.ozone_half_width_km && same(a.ground_albedo, b.ground_albedo) && a.turbidity == b.turbidity;
}

// The clouds a copy's lighting was built with still stand for these: the same layers, drifted by less than
// 1% of their feature size (the sky pass draws the clouds sharp every frame itself; the reflections and the
// light from all around follow them in steps, not every frame).
bool close_clouds(const CloudsGpu& built, const CloudsGpu& now) {
    if (std::memcmp(built.a, now.a, sizeof(now.a)) != 0 || std::memcmp(built.flags, now.flags, sizeof(now.flags)) != 0) return false;
    for (std::size_t i = 0; i < 2; ++i) {
        const float step = 0.01f * now.a[i].w;
        if (std::abs(built.b[i].x - now.b[i].x) > step || std::abs(built.b[i].y - now.b[i].y) > step) return false;
    }
    return built.b[0].z == now.b[0].z && std::abs(built.b[0].w - now.b[0].w) < 0.05f;  // planet; the camera's height (50 m)
}

bool same_sky(const AtmosphereDesc& a, const AtmosphereDesc& b) {
    return same_tables(a, b) && same(a.to_sun, b.to_sun) && a.altitude_m == b.altitude_m;
}

void memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
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

// Compute writes (or reads), then the next compute step reads or writes.
void compute_to_compute(VkCommandBuffer cmd) {
    memory_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
}

std::uint32_t groups(std::uint32_t n) { return (n + 7) / 8; }

// atmosphere.hlsl's AerialCamera (binding 8).
struct AerialCameraGpu {
    math::Mat4 world_from_clip;
    math::Vec4 eye;
    math::Vec4 params;  // x far end (km), y distance scale, z the camera's height above the surface (km)
};

} // namespace

Status Renderer::Impl::create_live_image(LiveSky::Image& out, VkFormat format, std::uint32_t width, std::uint32_t height,
                                         std::uint32_t mips, bool cube, const char* name) {
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = mips;
    image_info.arrayLayers = cube ? 6 : 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    NOOR_VK_TRY(vmaCreateImage(allocator, &image_info, &alloc, &out.image, &out.allocation, nullptr));
    set_name(VK_OBJECT_TYPE_IMAGE, out.image, name);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = out.image;
    view.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, image_info.arrayLayers};
    NOOR_VK_TRY(vkCreateImageView(device, &view, nullptr, &out.view));
    for (std::uint32_t m = 0; m < mips; ++m) {  // one view per level for writing
        view.viewType = cube ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, image_info.arrayLayers};
        VkImageView level = VK_NULL_HANDLE;
        NOOR_VK_TRY(vkCreateImageView(device, &view, nullptr, &level));
        out.levels.push_back(level);
    }
    out.mips = mips;
    out.layers = image_info.arrayLayers;
    return {};
}

Status Renderer::Impl::ensure_live_sky() {
    LiveSky& s = live_sky;
    if (s.layout != VK_NULL_HANDLE) return {};

    // Images (the shared tables, then each copy's sky and cube), all moved to GENERAL once.
    if (auto r = create_live_image(s.transmittance, VK_FORMAT_R32G32B32A32_SFLOAT, kTW, kTH, 1, false, "Atmosphere transmittance table"); !r) return r;
    if (auto r = create_live_image(s.multiple, VK_FORMAT_R32G32B32A32_SFLOAT, kMS, kMS, 1, false, "Atmosphere multiple-scattering table"); !r)
        return r;
    std::vector<const LiveSky::Image*> live_images = {&s.transmittance, &s.multiple};
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        LiveSky::Copy& c = s.copies[i];
        if (auto r = create_live_image(c.sky, VK_FORMAT_R16G16B16A16_SFLOAT, kSkyW, kSkyH, kSkyLevels, false, "Live sky (equirect pyramid)"); !r)
            return r;
        if (auto r = create_live_image(c.cube, VK_FORMAT_R16G16B16A16_SFLOAT, kCube, kCube, kCubeMips, true, "Live sky environment cube"); !r)
            return r;
        auto buffer = create_buffer(9 * sizeof(math::Vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, false);
        if (!buffer) return std::unexpected(buffer.error());
        c.irradiance = *buffer;
        {  // the aerial perspective volume (3D, 32^3)
            VkImageCreateInfo volume_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            volume_info.imageType = VK_IMAGE_TYPE_3D;
            volume_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            volume_info.extent = {kAerialSize, kAerialSize, kAerialSize};
            volume_info.mipLevels = 1;
            volume_info.arrayLayers = 1;
            volume_info.samples = VK_SAMPLE_COUNT_1_BIT;
            volume_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            volume_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            VmaAllocationCreateInfo alloc{};
            alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            NOOR_VK_TRY(vmaCreateImage(allocator, &volume_info, &alloc, &c.aerial.image, &c.aerial.allocation, nullptr));
            set_name(VK_OBJECT_TYPE_IMAGE, c.aerial.image, std::format("Aerial perspective volume (copy {})", i));
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = c.aerial.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_3D;
            view.format = volume_info.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            NOOR_VK_TRY(vkCreateImageView(device, &view, nullptr, &c.aerial.view));
            auto camera = create_buffer(sizeof(AerialCameraGpu), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, false);
            if (!camera) return std::unexpected(camera.error());
            c.aerial_camera = *camera;
        }
        live_images.push_back(&c.aerial);
        if (auto r = create_live_image(c.background, VK_FORMAT_R16G16B16A16_SFLOAT, kSkyW, kSkyH, 1, false, "Live sky background (no clouds)"); !r)
            return r;
        live_images.push_back(&c.background);
        auto clouds = create_buffer(sizeof(CloudsGpu), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, false);
        if (!clouds) return std::unexpected(clouds.error());
        c.clouds = *clouds;
        auto light = create_buffer(4 * sizeof(math::Vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, false);
        if (!light) return std::unexpected(light.error());
        c.cloud_light = *light;
        set_name(VK_OBJECT_TYPE_BUFFER, c.cloud_light.buffer, std::format("Cloud light (copy {})", i));
        set_name(VK_OBJECT_TYPE_BUFFER, c.irradiance.buffer, std::format("Live sky irradiance (SH9, copy {})", i));
        live_images.push_back(&c.sky);
        live_images.push_back(&c.cube);
    }
    {
        VkCommandBufferAllocateInfo cmd_alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_alloc.commandPool = upload_pool;
        cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        NOOR_VK_TRY(vkAllocateCommandBuffers(device, &cmd_alloc, &cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        std::vector<VkImageMemoryBarrier2> barriers;
        for (const LiveSky::Image* image : live_images) {
            VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image->image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image->mips, 0, image->layers};
            barriers.push_back(b);
        }
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dep.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(cmd, &dep);
        vkEndCommandBuffer(cmd);
        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_info.commandBuffer = cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        VkResult r = vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE);
        if (r == VK_SUCCESS) r = vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, upload_pool, 1, &cmd);
        if (r != VK_SUCCESS) return vk_failure(r, "live sky image setup");
    }

    // Samplers: the tables are read with linear filtering at texel centres; the sky like the CPU's
    // pyramid (wraps round, clamps at the poles, blends between levels).
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    NOOR_VK_TRY(vkCreateSampler(device, &sampler, nullptr, &s.table_sampler));
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = static_cast<float>(kSkyLevels - 1);
    NOOR_VK_TRY(vkCreateSampler(device, &sampler, nullptr, &s.sky_sampler));

    // One set layout for every pass; each set fills the bindings its entry point uses.
    const VkDescriptorSetLayoutBinding bindings[13] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},           // out_table
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},           // out_image
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},           // out_cube
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},  // transmittance
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},  // multiple scattering
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},  // sky pyramid
        {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},           // in_level
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},          // sh_out
        {8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},          // aerial_camera
        {9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},           // out_volume
        {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},         // clouds
        {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},         // cloud_light
        {12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},          // out_background
    };
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 13;
    layout_info.pBindings = bindings;
    NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &s.set_layout));
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &s.set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push;
    NOOR_VK_TRY(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &s.layout));

    const auto make_pipeline = [&](const std::uint8_t* code, std::size_t size, const char* entry, VkPipeline& out) -> Status {
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = size;
        module_info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        VkShaderModule module = VK_NULL_HANDLE;
        NOOR_VK_TRY(vkCreateShaderModule(device, &module_info, nullptr, &module));
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, entry, nullptr};
        info.layout = s.layout;
        const VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &out);
        vkDestroyShaderModule(device, module, nullptr);
        if (r != VK_SUCCESS) return vk_failure(r, "vkCreateComputePipelines (live sky)");
        set_name(VK_OBJECT_TYPE_PIPELINE, out, std::format("Live sky: {}", entry));
        return {};
    };
    if (auto r = make_pipeline(k_atmosphere_transmittance, sizeof(k_atmosphere_transmittance), "CSTransmittance", s.transmittance_pipeline); !r) return r;
    if (auto r = make_pipeline(k_atmosphere_multiple, sizeof(k_atmosphere_multiple), "CSMultiple", s.multiple_pipeline); !r) return r;
    if (auto r = make_pipeline(k_atmosphere_sky, sizeof(k_atmosphere_sky), "CSSky", s.sky_pipeline); !r) return r;
    if (auto r = make_pipeline(k_atmosphere_downsample, sizeof(k_atmosphere_downsample), "CSDownsample", s.downsample_pipeline); !r) return r;
    if (auto r = make_pipeline(k_atmosphere_cube, sizeof(k_atmosphere_cube), "CSCube", s.cube_pipeline); !r) return r;
    if (auto r = make_pipeline(k_atmosphere_irradiance, sizeof(k_atmosphere_irradiance), "CSIrradiance", s.irradiance_pipeline); !r) return r;
    if (auto r = make_pipeline(k_atmosphere_aerial, sizeof(k_atmosphere_aerial), "CSAerial", s.aerial_pipeline); !r) return r;
    if (auto r = make_pipeline(k_atmosphere_cloudlight, sizeof(k_atmosphere_cloudlight), "CSCloudLight", s.cloud_light_pipeline); !r) return r;

    // Descriptor sets: the two tables, and per copy the sky, a downsample per level, a cube per mip, irradiance.
    const std::uint32_t per_copy = 1 + (kSkyLevels - 1) + kCubeMips + 1 + 1 + 1;  // ... the aerial volume, the cloud light
    const std::uint32_t set_count = 2 + per_copy * kFramesInFlight;
    const VkDescriptorPoolSize sizes[4] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, set_count * 2},
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, set_count * 2},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight * 3},
                                           {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight * 3}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = set_count;
    pool_info.poolSizeCount = 4;
    pool_info.pPoolSizes = sizes;
    NOOR_VK_TRY(vkCreateDescriptorPool(device, &pool_info, nullptr, &s.pool));
    const auto allocate = [&](VkDescriptorSet& set) -> Status {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = s.pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &s.set_layout;
        NOOR_VK_TRY(vkAllocateDescriptorSets(device, &info, &set));
        return {};
    };
    const auto storage = [&](VkDescriptorSet set, std::uint32_t binding, VkImageView view) {
        VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    };
    const auto sampled = [&](VkDescriptorSet set, std::uint32_t binding, VkImageView view, VkSampler with) {
        VkDescriptorImageInfo image{with, view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    };
    const auto buffer_write = [&](VkDescriptorSet set, std::uint32_t binding, VkBuffer buffer, VkDescriptorType type) {
        VkDescriptorBufferInfo info{buffer, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    };
    if (auto r = allocate(s.transmittance_set); !r) return r;
    storage(s.transmittance_set, 0, s.transmittance.view);
    if (auto r = allocate(s.multiple_set); !r) return r;
    storage(s.multiple_set, 0, s.multiple.view);
    sampled(s.multiple_set, 3, s.transmittance.view, s.table_sampler);
    for (LiveSky::Copy& c : s.copies) {
        if (auto r = allocate(c.sky_set); !r) return r;
        storage(c.sky_set, 1, c.sky.levels[0]);
        sampled(c.sky_set, 3, s.transmittance.view, s.table_sampler);
        sampled(c.sky_set, 4, s.multiple.view, s.table_sampler);
        buffer_write(c.sky_set, 10, c.clouds.buffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        buffer_write(c.sky_set, 11, c.cloud_light.buffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        storage(c.sky_set, 12, c.background.view);
        if (auto r = allocate(c.cloud_light_set); !r) return r;
        sampled(c.cloud_light_set, 3, s.transmittance.view, s.table_sampler);
        sampled(c.cloud_light_set, 4, s.multiple.view, s.table_sampler);
        buffer_write(c.cloud_light_set, 10, c.clouds.buffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        buffer_write(c.cloud_light_set, 11, c.cloud_light.buffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        c.downsample_sets.resize(kSkyLevels);
        for (std::uint32_t level = 1; level < kSkyLevels; ++level) {
            if (auto r = allocate(c.downsample_sets[level]); !r) return r;
            storage(c.downsample_sets[level], 1, c.sky.levels[level]);
            storage(c.downsample_sets[level], 6, c.sky.levels[level - 1]);
        }
        c.cube_sets.resize(kCubeMips);
        for (std::uint32_t m = 0; m < kCubeMips; ++m) {
            if (auto r = allocate(c.cube_sets[m]); !r) return r;
            storage(c.cube_sets[m], 2, c.cube.levels[m]);
            sampled(c.cube_sets[m], 5, c.sky.view, s.sky_sampler);
        }
        if (auto r = allocate(c.irradiance_set); !r) return r;
        sampled(c.irradiance_set, 5, c.sky.view, s.sky_sampler);
        VkDescriptorBufferInfo buffer{c.irradiance.buffer, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = c.irradiance_set;
        write.dstBinding = 7;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        if (auto r = allocate(c.aerial_set); !r) return r;
        sampled(c.aerial_set, 3, s.transmittance.view, s.table_sampler);
        sampled(c.aerial_set, 4, s.multiple.view, s.table_sampler);
        storage(c.aerial_set, 9, c.aerial.view);
        VkDescriptorBufferInfo camera{c.aerial_camera.buffer, 0, sizeof(AerialCameraGpu)};
        VkWriteDescriptorSet camera_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        camera_write.dstSet = c.aerial_set;
        camera_write.dstBinding = 8;
        camera_write.descriptorCount = 1;
        camera_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        camera_write.pBufferInfo = &camera;
        vkUpdateDescriptorSets(device, 1, &camera_write, 0, nullptr);

        // The sky image as a texture in the bindless table (the sky pass draws it as the background).
        std::uint32_t slot = kAnySlot;
        for (std::uint32_t i = kFirstUserTexture; i < textures.size(); ++i)
            if (!textures[i].alive) {
                slot = i;
                break;
            }
        if (slot == kAnySlot) {
            if (textures.size() >= kMaxTextures) return fail(ErrorCode::OutOfMemory, "texture table full: no slot for the live sky");
            slot = static_cast<std::uint32_t>(textures.size());
            textures.emplace_back();
        }
        GpuTexture entry;  // the image is owned here, not by the texture table (destroy_live_sky frees the slot)
        entry.alive = true;
        textures[slot] = entry;
        c.sky_slot = slot;
        VkDescriptorImageInfo image{VK_NULL_HANDLE, c.background.view, VK_IMAGE_LAYOUT_GENERAL};  // the clear sky
        VkWriteDescriptorSet slot_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        slot_write.dstSet = texture_set;
        slot_write.dstBinding = 0;
        slot_write.dstArrayElement = slot;
        slot_write.descriptorCount = 1;
        slot_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        slot_write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &slot_write, 0, nullptr);
    }
    return {};
}

Status Renderer::Impl::set_atmosphere(const AtmosphereDesc& d) {
    if (auto s = ensure_live_sky(); !s) return s;
    LiveSky& s = live_sky;
    if (!s.active) {
        // Each frame slot's set reads its own copy (frames in flight use the old environment: wait once).
        vkDeviceWaitIdle(device);
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
            const LiveSky::Copy& c = s.copies[i];
            VkDescriptorImageInfo image{environment.sampler, c.cube.view, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo buffer{c.irradiance.buffer, 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet writes[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
            writes[0].dstSet = frame_sets[i];
            writes[0].dstBinding = 2;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &image;
            writes[1].dstSet = frame_sets[i];
            writes[1].dstBinding = 3;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &buffer;
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
            write_aerial_descriptor(i, c.aerial.view, VK_IMAGE_LAYOUT_GENERAL);
            write_cloud_light_descriptor(i, c.cloud_light.buffer);
        }
        environment.mip_count = kCubeMips;
        environment.background = TextureHandle{s.copies[0].sky_slot};  // valid for record_sky; each frame draws its own copy
        for (LiveSky::Copy& c : s.copies) c.built = false;
        s.active = true;
    }
    s.desc = d;
    s.has_desc = true;
    return {};
}

void Renderer::Impl::record_live_sky(VkCommandBuffer cmd) {
    LiveSky& s = live_sky;
    if (!s.active || !s.has_desc) return;
    LiveSky::Copy& c = s.copies[frame_index];
    const bool tables = !s.tables_built || !same_tables(s.desc, s.tables_for);
    const bool sky = tables || !c.built || !same_sky(c.built_for, s.desc) || !close_clouds(c.built_clouds, frame_clouds);
    const bool aerial = s.desc.aerial_distance_scale > 0.0f;
    if (!sky && !aerial) return;

    begin_label(cmd, "Live sky (atmosphere, environment lighting, aerial perspective)", {1.0f, 0.62f, 0.33f, 1.0f});
    Push push = push_of(s.desc);
    const auto bind = [&](VkPipeline pipeline, VkDescriptorSet set) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.layout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, s.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
    };
    // Earlier frames' compute read the tables and wrote the other copy; this copy's last reader (the
    // frame two before) has finished: the CPU waited for its fence.
    memory_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    if (tables) {
        bind(s.transmittance_pipeline, s.transmittance_set);
        vkCmdDispatch(cmd, groups(kTW), groups(kTH), 1);
        compute_to_compute(cmd);
        bind(s.multiple_pipeline, s.multiple_set);
        vkCmdDispatch(cmd, groups(kMS), groups(kMS), 1);
        compute_to_compute(cmd);
        s.tables_built = true;
        s.tables_for = s.desc;
        for (LiveSky::Copy& other : s.copies) other.built = false;  // all show the old air now
        ++s.table_builds;
    }
    if (sky) record_live_sky_lighting(cmd, push);
    if (aerial) {
        // This frame's camera (update_frame_uniforms kept it); the copy's last reader has finished.
        AerialCameraGpu camera{};
        camera.world_from_clip = frame_world_from_clip;
        camera.eye = {frame_eye.x, frame_eye.y, frame_eye.z, 1.0f};
        camera.params = {kAerialFarKm, s.desc.aerial_distance_scale, std::max(0.0f, (s.desc.altitude_m + frame_eye.y) * 0.001f), 0.0f};
        std::memcpy(c.aerial_camera.mapped, &camera, sizeof(camera));
        vmaFlushAllocation(allocator, c.aerial_camera.allocation, 0, sizeof(camera));
        bind(s.aerial_pipeline, c.aerial_set);
        vkCmdDispatch(cmd, groups(kAerialSize), groups(kAerialSize), 1);
    }
    // This frame's scene pass reads them all.
    memory_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_2_SHADER_READ_BIT);
    end_label(cmd);
}

void Renderer::Impl::record_live_sky_lighting(VkCommandBuffer cmd, Push& push) {
    LiveSky& s = live_sky;
    LiveSky::Copy& c = s.copies[frame_index];
    const auto bind = [&](VkPipeline pipeline, VkDescriptorSet set) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.layout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, s.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
    };
    // The clouds this copy shows, and the light at each layer (the sky pass reads it too).
    std::memcpy(c.clouds.mapped, &frame_clouds, sizeof(CloudsGpu));
    vmaFlushAllocation(allocator, c.clouds.allocation, 0, sizeof(CloudsGpu));
    bind(s.cloud_light_pipeline, c.cloud_light_set);
    vkCmdDispatch(cmd, 1, 1, 1);
    compute_to_compute(cmd);
    bind(s.sky_pipeline, c.sky_set);
    vkCmdDispatch(cmd, groups(kSkyW), groups(kSkyH), 1);
    compute_to_compute(cmd);
    for (std::uint32_t level = 1; level < kSkyLevels; ++level) {
        bind(s.downsample_pipeline, c.downsample_sets[level]);
        vkCmdDispatch(cmd, groups(std::max(1u, kSkyW >> level)), groups(std::max(1u, kSkyH >> level)), 1);
        compute_to_compute(cmd);
    }
    // make_environment(sky, 64): mip 0 from the pyramid at the face's texel size, then GGX mips.
    const float face_texel = 4.0f * math::kPi / (6.0f * static_cast<float>(kCube * kCube));
    const float base_texel = 4.0f * math::kPi / static_cast<float>(kSkyW * kSkyH);
    push.extra = {0.0f, std::max(0.0f, 0.5f * std::log2(face_texel / base_texel)), base_texel, 0.0f};
    for (std::uint32_t m = 0; m < kCubeMips; ++m) {
        const std::uint32_t size = std::max(1u, kCube >> m);
        push.pass[0] = m;
        push.pass[1] = size;
        push.pass[2] = m == 0 ? 1u : (m < 3 ? 48u : 64u);
        push.extra.x = static_cast<float>(m) / static_cast<float>(kCubeMips - 1);
        bind(s.cube_pipeline, c.cube_sets[m]);
        vkCmdDispatch(cmd, groups(size), groups(size), 6);
    }
    push.pass[0] = kIrradianceLevel;
    bind(s.irradiance_pipeline, c.irradiance_set);
    vkCmdDispatch(cmd, 1, 1, 1);
    push = push_of(s.desc);  // the aerial pass reads the plain settings
    c.built = true;
    c.built_for = s.desc;
    c.built_clouds = frame_clouds;
    ++s.sky_builds;
}

void Renderer::Impl::destroy_live_sky() {
    if (device == VK_NULL_HANDLE) return;
    LiveSky& s = live_sky;
    for (VkPipeline p : {s.transmittance_pipeline, s.multiple_pipeline, s.sky_pipeline, s.downsample_pipeline, s.cube_pipeline, s.irradiance_pipeline,
                         s.aerial_pipeline, s.cloud_light_pipeline})
        if (p) vkDestroyPipeline(device, p, nullptr);
    if (s.layout) vkDestroyPipelineLayout(device, s.layout, nullptr);
    if (s.pool) vkDestroyDescriptorPool(device, s.pool, nullptr);
    if (s.set_layout) vkDestroyDescriptorSetLayout(device, s.set_layout, nullptr);
    if (s.table_sampler) vkDestroySampler(device, s.table_sampler, nullptr);
    if (s.sky_sampler) vkDestroySampler(device, s.sky_sampler, nullptr);
    const auto destroy_image = [&](LiveSky::Image& image) {
        for (VkImageView v : image.levels) vkDestroyImageView(device, v, nullptr);
        if (image.view) vkDestroyImageView(device, image.view, nullptr);
        if (image.image) vmaDestroyImage(allocator, image.image, image.allocation);
    };
    destroy_image(s.transmittance);
    destroy_image(s.multiple);
    for (LiveSky::Copy& c : s.copies) {
        if (c.sky_slot != 0 && c.sky_slot < textures.size()) textures[c.sky_slot] = {};
        destroy_image(c.sky);
        destroy_image(c.cube);
        destroy_image(c.aerial);
        destroy_image(c.background);
        destroy_buffer(c.clouds);
        destroy_buffer(c.cloud_light);
        destroy_buffer(c.irradiance);
        destroy_buffer(c.aerial_camera);
    }
    s = {};
}

} // namespace noor::render
