// Water (Milestone 8D, part 1, ADR-037). When water is in view, the scene pass is split in two: the opaque scene and
// the sky first; then its colour is copied and its depth resolved into images water can read (set 0, bindings 7 and
// 8); then the see-through pass draws the water surfaces (water.hlsl: absorption by thickness, refraction, a soft
// shore, the sky's reflection, the sun's glint) before glass and particles. Without water nothing of this runs.
// Part 3: before the surfaces, each body's wet ground (a flat band a little above the water, multiplying what's under
// it); each draw's further settings in a storage buffer (set 0, binding 9) and its depth map in the texture table.
#include "vulkan_impl.h"

#include "noor/shaders/water_ps.h"
#include "noor/shaders/water_vs.h"
#include "noor/shaders/water_fog_add_ps.h"
#include "noor/shaders/water_fog_mul_ps.h"
#include "noor/shaders/water_fog_vs.h"
#include "noor/shaders/water_wet_ps.h"
#include "noor/shaders/water_wet_vs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace noor::render {

namespace {

struct UnderwaterLook {
    math::Vec3 absorption, scatter;
    float caustics, shafts;
};
// A body's water as it's drawn: its own, or (the camera in it) the environment's underwater settings.
UnderwaterLook underwater_look(const WaterDraw& w, bool camera_inside) {
    if (camera_inside && w.underwater) return {w.underwater->absorption, w.underwater->scatter, w.underwater->caustics, w.underwater->shafts};
    return {w.absorption, w.scatter, w.caustics, w.shafts};
}

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

// water_light.hlsli's WaterSettings (std430). Entry 0 is a header (map_info: x the bodies that light the scene, the
// first kLightBodies draws; y the body the camera may be in, plus 1, or 0); draw i is entry i + 1 (WaterPush::grid[3]).
struct WaterGpu {
    float map[4];               // xy: world (x, z) of the depth map's sample (0, 0), z: metres between samples, w: wind (m/s)
    std::uint32_t map_info[4];  // x, y: samples across, z: texture slot, w: 1 = has a map
    float foam[4];              // x: foam, y: foam depth (m), z: crest foam, w: wet (darkening)
    float shore[4];             // x: wet height (m), y: reflections, z: the wet band's height above the level (m), w: rain
    float body[4];              // part 4: xyz its middle (y: the level), w: yaw
    float extent[4];            // x, y: half size (m), z: 1 = a sea, w: caustics
    float absorption[4];        // rgb: per metre, w: shafts
    float scatter[4];           // rgb, w: the main wave's height (m)
};
static_assert(sizeof(WaterGpu) == 128);
constexpr std::uint32_t kMaxWaterDraws = 64;
constexpr std::uint32_t kLightBodies = 8;  // the first draws: light under them is dimmed by the water (mesh.hlsl)
constexpr std::uint32_t kRiverAcross = 8;  // a river's ribbon: cells across (part 6)
constexpr std::uint32_t kRiverRows = 8;    // its course texture: x, y, z, half width, along, tangent x, tangent z, slope
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
        image_info.extent = {render_extent.width, render_extent.height, 1};
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
    color_region.extent = {render_extent.width, render_extent.height, 1};
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
        depth_region.extent = {render_extent.width, render_extent.height, 1};
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
            auto buffer = create_buffer((kMaxWaterDraws + 1) * sizeof(WaterGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, false);
            if (!buffer) return std::unexpected(buffer.error());
            water_buffers[i] = *buffer;
            std::memset(water_buffers[i].mapped, 0, (kMaxWaterDraws + 1) * sizeof(WaterGpu));  // header 0: no water
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

// Whether the camera may be in this body: inside its footprint and below the highest its waves reach (plus a margin
// for the near plane). The shaders decide pixel by pixel; this only says whether to look.
static bool camera_may_be_in(const WaterDraw& w, math::Vec3 eye) {
    if (w.river) return false;  // rivers aren't seen from inside (yet)
    if (eye.y > w.center.y + 0.97f * std::max(0.0f, w.wave_height) + 0.25f) return false;
    if (w.endless) return true;
    const float dx = eye.x - w.center.x, dz = eye.z - w.center.z;
    const float c = std::cos(w.yaw), s = std::sin(w.yaw);
    return std::abs(c * dx - s * dz) <= w.half_size.x && std::abs(s * dx + c * dz) <= w.half_size.y;
}

float underwater_exposure_shift(const FrameDesc& frame) {
    if (frame.water.empty()) return 0.0f;
    const math::Vec3 eye = math::inverse(frame.camera.view).transform_point({0, 0, 0});
    const std::size_t count = std::min<std::size_t>(frame.water.size(), kMaxWaterDraws);
    for (std::size_t i = 0; i < count; ++i) {
        const WaterDraw& w = frame.water[i];
        if (!camera_may_be_in(w, eye)) continue;
        const float depth = std::max(0.0f, w.center.y - eye.y);
        const UnderwaterLook look = underwater_look(w, true);
        // The sky's light left at the camera's depth (by brightness), and half of its loss made up, in stops.
        const float left = 0.2126f * std::exp(-look.absorption.x * depth) + 0.7152f * std::exp(-look.absorption.y * depth) +
                           0.0722f * std::exp(-look.absorption.z * depth);
        return 0.5f * std::log2(1.0f / std::max(left, 1e-6f));
    }
    return 0.0f;
}

void Renderer::Impl::prepare_water(const FrameDesc& frame) {
    water_camera_body = -1;
    if (water_buffers[frame_index].buffer == VK_NULL_HANDLE) return;
    auto* out = static_cast<WaterGpu*>(water_buffers[frame_index].mapped);
    if (frame.water.empty()) {
        if (out[0].map_info[0] != 0 || out[0].map_info[1] != 0) {
            out[0] = {};
            vmaFlushAllocation(allocator, water_buffers[frame_index].allocation, 0, sizeof(WaterGpu));
        }
        return;
    }
    ++water_frame;
    const std::size_t count = std::min<std::size_t>(frame.water.size(), kMaxWaterDraws);
    const math::Vec3 eye = math::inverse(frame.camera.view).transform_point({0, 0, 0});
    for (std::size_t i = 0; i < count; ++i)
        if (camera_may_be_in(frame.water[i], eye)) {
            water_camera_body = static_cast<int>(i);
            break;
        }
    out[0] = {};
    out[0].map_info[0] = static_cast<std::uint32_t>(std::min<std::size_t>(count, kLightBodies));
    out[0].map_info[1] = static_cast<std::uint32_t>(water_camera_body + 1);
    out[0].map_info[2] = std::min(frame.water_debug, 5u);  // the debug view (part 7)
    out[0].map[0] = static_cast<float>(std::fmod(frame.water[0].time, 3600.0));  // the water's clock (caustics)
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
                    water_maps.push_back({std::static_pointer_cast<const void>(map), *tex, 0});
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
        g.shore[3] = std::clamp(w.rain, 0.0f, 1.0f);
        g.map[3] = std::max(0.0f, w.wind_speed);
        // A river (part 6): its course as an R32 float texture (a column per sample, kRiverRows rows) in place of a depth
        // map (map_info.w 2), its speed in body.w; extent.z 2 says "a river" (no light dimmed under it, no wet band).
        bool river_ready = false;
        if (const auto& path = w.river; path && path->samples.size() >= 2) {
            auto it = std::ranges::find_if(water_maps, [&](const WaterMapTexture& t) { return t.map.get() == path.get(); });
            if (it == water_maps.end()) {
                const auto n = static_cast<std::uint32_t>(path->samples.size());
                std::vector<float> texels(std::size_t{n} * kRiverRows);
                for (std::uint32_t k = 0; k < n; ++k) {
                    const auto& smp = path->samples[k];
                    const float row[kRiverRows] = {smp.centre.x, smp.centre.y, smp.centre.z, smp.half_width, smp.along, smp.tangent.x, smp.tangent.y, smp.slope};
                    for (std::uint32_t r = 0; r < kRiverRows; ++r) texels[std::size_t{r} * n + k] = row[r];
                }
                const TextureMipData mip{n, kRiverRows, {reinterpret_cast<const std::uint8_t*>(texels.data()), texels.size() * sizeof(float)}};
                const TextureDesc path_desc{TextureFormat::R32Float, std::span(&mip, 1), "River course"};
                if (auto tex = create_texture(path_desc, kAnySlot)) {
                    water_maps.push_back({std::static_pointer_cast<const void>(path), *tex, 0});
                    it = water_maps.end() - 1;
                } else {
                    NOOR_LOG_WARN("Render", "River course not uploaded: {}", tex.error().describe());
                }
            }
            if (it != water_maps.end()) {
                it->last_used = water_frame;
                g.map_info[0] = static_cast<std::uint32_t>(path->samples.size());
                g.map_info[1] = kRiverRows;
                g.map_info[2] = it->texture.id;
                g.map_info[3] = 2;
                river_ready = true;
            }
        }
        const UnderwaterLook look = underwater_look(w, static_cast<int>(i) == water_camera_body);
        g.body[0] = w.center.x;
        g.body[1] = w.center.y;
        g.body[2] = w.center.z;
        g.body[3] = w.yaw;
        g.extent[0] = w.half_size.x;
        g.extent[1] = w.half_size.y;
        g.extent[2] = w.endless ? 1.0f : 0.0f;
        g.extent[3] = std::max(0.0f, look.caustics);
        g.absorption[0] = look.absorption.x;
        g.absorption[1] = look.absorption.y;
        g.absorption[2] = look.absorption.z;
        g.absorption[3] = std::clamp(look.shafts, 0.0f, 1.0f);
        g.scatter[0] = look.scatter.x;
        g.scatter[1] = look.scatter.y;
        g.scatter[2] = look.scatter.z;
        g.scatter[3] = std::max(0.0f, w.wave_height);
        if (river_ready) {
            g.body[3] = w.river->speed;
            g.extent[2] = 2.0f;
        }
        out[i + 1] = g;
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
    if (result == VK_SUCCESS) {
        // Underwater from inside (part 4): full screen, no depth; [0] multiplies (what gets through), [1] adds.
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, ps, nullptr);
        vs = ps = VK_NULL_HANDLE;
        result = make_module(k_water_fog_vs, sizeof(k_water_fog_vs), vs);
        stages[0].module = vs;
        stages[0].pName = "VSFog";
        depth.depthTestEnable = VK_FALSE;
        depth.depthWriteEnable = VK_FALSE;
        for (int k = 0; k < 2 && result == VK_SUCCESS; ++k) {
            if (ps) vkDestroyShaderModule(device, ps, nullptr);
            ps = VK_NULL_HANDLE;
            result = k == 0 ? make_module(k_water_fog_mul_ps, sizeof(k_water_fog_mul_ps), ps) : make_module(k_water_fog_add_ps, sizeof(k_water_fog_add_ps), ps);
            if (result != VK_SUCCESS) break;
            stages[1].module = ps;
            stages[1].pName = k == 0 ? "PSFogMul" : "PSFogAdd";
            blend_attachment.srcColorBlendFactor = k == 0 ? VK_BLEND_FACTOR_DST_COLOR : VK_BLEND_FACTOR_ONE;
            blend_attachment.dstColorBlendFactor = k == 0 ? VK_BLEND_FACTOR_ZERO : VK_BLEND_FACTOR_ONE;
            result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &water_fog_pipelines[k]);
        }
    }
    if (vs) vkDestroyShaderModule(device, vs, nullptr);
    if (ps) vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) {
        destroy_water_pipeline();
        return vk_failure(result, "vkCreateGraphicsPipelines (water)");
    }
    set_name(VK_OBJECT_TYPE_PIPELINE, water_pipeline, "Water pipeline (surfaces over the copied scene)");
    set_name(VK_OBJECT_TYPE_PIPELINE, water_wet_pipeline, "Water pipeline (wet ground at the waterline)");
    set_name(VK_OBJECT_TYPE_PIPELINE, water_fog_pipelines[0], "Water pipeline (underwater: what gets through)");
    set_name(VK_OBJECT_TYPE_PIPELINE, water_fog_pipelines[1], "Water pipeline (underwater: scattered light)");
    return {};
}

void Renderer::Impl::destroy_water_pipeline() {
    if (water_pipeline) vkDestroyPipeline(device, water_pipeline, nullptr);
    if (water_wet_pipeline) vkDestroyPipeline(device, water_wet_pipeline, nullptr);
    for (auto& p : water_fog_pipelines)
        if (p) vkDestroyPipeline(device, p, nullptr);
    water_pipeline = water_wet_pipeline = VK_NULL_HANDLE;
    water_fog_pipelines[0] = water_fog_pipelines[1] = VK_NULL_HANDLE;
}

void Renderer::Impl::record_water(VkCommandBuffer cmd, const FrameDesc& frame) {
    if (frame.water.empty() || water_pipeline == VK_NULL_HANDLE) return;
    begin_label(cmd, "Water", {0.2f, 0.5f, 0.9f, 1.0f});
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(render_extent.width), static_cast<float>(render_extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, render_extent};
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
        if (w.river && w.river->samples.size() >= 2) {
            // A river (part 6): a ribbon along its course, a row of cells per sample.
            push.grid[0] = static_cast<std::uint32_t>(w.river->samples.size() - 1);
            push.grid[1] = kRiverAcross;
            push.grid[2] = 2;
            vertices = push.grid[0] * kRiverAcross * 6;
        } else if (w.endless) {
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
        push.absorption[3] = w.refraction;
        push.scatter[3] = std::max(0.001f, w.shore_fade);
        push.params[0] = std::clamp(w.roughness, 0.0f, 1.0f);
        push.params[1] = static_cast<float>(std::fmod(w.time, 3600.0));
        push.params[2] = 1.0f / static_cast<float>(std::max(1u, render_extent.width));
        push.params[3] = 1.0f / static_cast<float>(std::max(1u, render_extent.height));
        push.grid[3] = index + 1;  // its settings (entry 0 is the header)
        // The camera's body seen from inside: the environment's underwater settings, if any, over the water's.
        const UnderwaterLook look = underwater_look(w, static_cast<int>(index) == water_camera_body);
        push.absorption[0] = look.absorption.x;
        push.absorption[1] = look.absorption.y;
        push.absorption[2] = look.absorption.z;
        push.scatter[0] = look.scatter.x;
        push.scatter[1] = look.scatter.y;
        push.scatter[2] = look.scatter.z;
        return push;
    };
    // Wet ground first: a flat band over each body (a sea: round the camera), multiplying the ground under it.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_wet_pipeline);
    for (std::size_t i = 0; i < count; ++i) {
        const WaterDraw& w = frame.water[i];
        if (w.wet <= 0.0f || w.river) continue;  // rivers: no wet band (their level isn't one height)
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
    // The camera may be under a surface: the water between it and what it sees (each pixel decides).
    if (water_camera_body >= 0 && static_cast<std::size_t>(water_camera_body) < count) {
        std::uint32_t vertices = 0;
        const WaterPush push = push_of(frame.water[static_cast<std::size_t>(water_camera_body)], static_cast<std::uint32_t>(water_camera_body), vertices);
        for (VkPipeline p : water_fog_pipelines) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
            vkCmdPushConstants(cmd, water_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            ++stats.draw_calls;
        }
    }
    end_label(cmd);
}

} // namespace noor::render
