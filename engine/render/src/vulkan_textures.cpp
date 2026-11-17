// Textures (ADR-029) and the bindless texture table (ADR-012): one descriptor set (set 1) with an
// array of kMaxTextures sampled images and one shared sampler. A texture's handle id is its slot;
// draws pass slots in push constants and the shader indexes the array. Slots 0..2 are built in:
// white (no base colour texture), flat normal (no normal map), and a checker for missing files.
#include "vulkan_impl.h"

#include <cstring>

namespace noor::render {

namespace {

VkFormat vk_format(TextureFormat f) {
    switch (f) {
    case TextureFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Rgba16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::Bc7Srgb: return VK_FORMAT_BC7_SRGB_BLOCK;
    case TextureFormat::Bc7Unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
    case TextureFormat::Bc5Unorm: return VK_FORMAT_BC5_UNORM_BLOCK;
    }
    return VK_FORMAT_R8G8B8A8_SRGB;
}

bool block_compressed(TextureFormat f) {
    return f == TextureFormat::Bc7Srgb || f == TextureFormat::Bc7Unorm || f == TextureFormat::Bc5Unorm;
}

// Bytes of one mip: 16 per 4x4 block for BC formats (partial blocks rounded up), else per pixel.
std::size_t mip_size(TextureFormat f, std::uint32_t w, std::uint32_t h) {
    if (block_compressed(f)) return static_cast<std::size_t>((w + 3) / 4) * ((h + 3) / 4) * 16;
    return static_cast<std::size_t>(w) * h * (f == TextureFormat::Rgba16Float ? 8u : 4u);
}

} // namespace

Status Renderer::Impl::ensure_texture_table() {
    if (texture_set != VK_NULL_HANDLE) return {};

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = max_anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    sampler_info.maxAnisotropy = max_anisotropy;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    NOOR_VK_TRY(vkCreateSampler(device, &sampler_info, nullptr, &texture_sampler));
    set_name(VK_OBJECT_TYPE_SAMPLER, texture_sampler, "Texture sampler (trilinear, repeat)");

    const VkDescriptorSetLayoutBinding bindings[3] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &texture_sampler},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // materials
    };
    // Textures are added and removed while earlier frames may still be in flight: slots that
    // frame doesn't use may change (update-after-bind, unused-while-pending), and empty slots are
    // fine (partially bound).
    const VkDescriptorBindingFlags binding_flags[3] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        0,
        0,
    };
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flags_info.bindingCount = 3;
    flags_info.pBindingFlags = binding_flags;
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.pNext = &flags_info;
    layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout_info.bindingCount = 3;
    layout_info.pBindings = bindings;
    NOOR_VK_TRY(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &texture_set_layout));

    const VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures},
                                           {VK_DESCRIPTOR_TYPE_SAMPLER, 1},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 3;
    pool_info.pPoolSizes = sizes;
    NOOR_VK_TRY(vkCreateDescriptorPool(device, &pool_info, nullptr, &texture_pool));
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = texture_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &texture_set_layout;
    NOOR_VK_TRY(vkAllocateDescriptorSets(device, &set_info, &texture_set));
    set_name(VK_OBJECT_TYPE_DESCRIPTOR_SET, texture_set, "Bindless texture table");

    // The material table: persistently mapped, written from the CPU (materials change rarely).
    auto buffer = create_buffer(sizeof(GpuMaterial) * kMaxMaterials, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, false);
    if (!buffer) return std::unexpected(buffer.error());
    material_buffer = *buffer;
    set_name(VK_OBJECT_TYPE_BUFFER, material_buffer.buffer, "Material table");
    VkDescriptorBufferInfo material_info{material_buffer.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet material_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    material_write.dstSet = texture_set;
    material_write.dstBinding = 2;
    material_write.descriptorCount = 1;
    material_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    material_write.pBufferInfo = &material_info;
    vkUpdateDescriptorSets(device, 1, &material_write, 0, nullptr);

    // The built-in slots.
    textures.resize(kFirstUserTexture);
    const std::uint8_t white[4] = {255, 255, 255, 255};
    const std::uint8_t flat[4] = {128, 128, 255, 255};
    std::uint8_t checker[8 * 8 * 4];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const bool on = ((x / 2) + (y / 2)) % 2 == 0;
            std::uint8_t* p = &checker[(y * 8 + x) * 4];
            p[0] = on ? 255 : 0, p[1] = 0, p[2] = on ? 255 : 0, p[3] = 255;
        }
    struct Builtin {
        std::uint32_t slot;
        TextureFormat format;
        std::uint32_t size;
        const std::uint8_t* bytes;
        const char* name;
    };
    for (const Builtin& b : {Builtin{kTextureWhite, TextureFormat::Rgba8Srgb, 1, white, "Built-in white"},
                             Builtin{kTextureFlatNormal, TextureFormat::Rgba8Unorm, 1, flat, "Built-in flat normal"},
                             Builtin{kTextureMissing, TextureFormat::Rgba8Srgb, 8, checker, "Built-in missing (checker)"}}) {
        const TextureMipData mip{b.size, b.size, {b.bytes, static_cast<std::size_t>(b.size) * b.size * 4}};
        if (auto h = create_texture({b.format, {&mip, 1}, b.name}, b.slot); !h) return std::unexpected(h.error());
    }
    textures.resize(kFirstUserTexture);
    if (auto s = create_brdf_lut(); !s) return s;
    texture_bytes = 0;  // the built-ins don't count as "your" textures
    materials.assign(1, MaterialDesc{});  // slot 0: the default material
    material_alive.assign(1, true);
    write_material(0, materials[0]);
    return {};
}

// ---- materials ----

void Renderer::Impl::write_material(std::uint32_t slot, const MaterialDesc& d) {
    const auto tex = [&](TextureHandle t, std::uint32_t fallback) {
        return t.valid() && t.id < textures.size() && textures[t.id].alive ? t.id : fallback;
    };
    const std::uint32_t flags = (d.alpha == AlphaMode::Cutout ? 1u : 0u) | (d.alpha == AlphaMode::Blend ? 2u : 0u) | (d.double_sided ? 4u : 0u);
    const GpuMaterial m{d.base_color,
                        {d.emissive.x, d.emissive.y, d.emissive.z, d.alpha_cutoff},
                        {d.metallic, d.roughness, d.normal_scale, d.occlusion_strength},
                        {d.uv_scale.x, d.uv_scale.y, d.uv_offset.x, d.uv_offset.y},
                        {tex(d.base_color_texture, kTextureWhite), tex(d.metallic_roughness_texture, kTextureWhite),
                         tex(d.roughness_texture, kTextureWhite), tex(d.metallic_texture, kTextureWhite)},
                        {tex(d.normal_texture, kTextureFlatNormal), tex(d.occlusion_texture, kTextureWhite),
                         tex(d.emissive_texture, kTextureWhite), flags}};
    std::memcpy(static_cast<GpuMaterial*>(material_buffer.mapped) + slot, &m, sizeof(m));
    vmaFlushAllocation(allocator, material_buffer.allocation, sizeof(GpuMaterial) * slot, sizeof(GpuMaterial));
}

Result<TextureHandle> Renderer::Impl::create_texture(const TextureDesc& texture, std::uint32_t slot) {
    if (texture.mips.empty() || texture.mips[0].width == 0 || texture.mips[0].height == 0)
        return fail(ErrorCode::InvalidArgument, "texture needs at least one mip with a size");
    if (block_compressed(texture.format) && !bc_supported)
        return fail(ErrorCode::Unsupported, std::format("texture '{}': this GPU can't sample BC7/BC5 textures", texture.debug_name));
    VkDeviceSize total = 0;
    for (std::size_t i = 0; i < texture.mips.size(); ++i) {
        const auto& m = texture.mips[i];
        const std::uint32_t w = std::max(1u, texture.mips[0].width >> i), h = std::max(1u, texture.mips[0].height >> i);
        if (m.width != w || m.height != h || m.bytes.size() != mip_size(texture.format, w, h))
            return fail(ErrorCode::InvalidArgument,
                        std::format("texture '{}': mip {} is {}x{} with {} bytes, expected {}x{} with {}", texture.debug_name, i,
                                    m.width, m.height, m.bytes.size(), w, h, mip_size(texture.format, w, h)));
        total += m.bytes.size();
    }
    if (slot == kAnySlot) {  // a user texture: the first free slot
        for (std::uint32_t i = kFirstUserTexture; i < textures.size(); ++i)
            if (!textures[i].alive) {
                slot = i;
                break;
            }
        if (slot == kAnySlot) {
            if (textures.size() >= kMaxTextures)
                return fail(ErrorCode::OutOfMemory, std::format("texture table full ({} textures)", kMaxTextures));
            slot = static_cast<std::uint32_t>(textures.size());
            textures.emplace_back();
        }
    }

    auto staging = create_buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false);
    if (!staging) return std::unexpected(staging.error());
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize offset = 0;
    for (std::size_t i = 0; i < texture.mips.size(); ++i) {
        const auto& m = texture.mips[i];
        std::memcpy(static_cast<std::uint8_t*>(staging->mapped) + offset, m.bytes.data(), m.bytes.size());
        VkBufferImageCopy r{};
        r.bufferOffset = offset;
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(i), 0, 1};
        r.imageExtent = {m.width, m.height, 1};
        regions.push_back(r);
        offset += m.bytes.size();
    }
    vmaFlushAllocation(allocator, staging->allocation, 0, total);

    GpuTexture tex;
    const auto mip_count = static_cast<std::uint32_t>(texture.mips.size());
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = vk_format(texture.format);
    image_info.extent = {texture.mips[0].width, texture.mips[0].height, 1};
    image_info.mipLevels = mip_count;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (VkResult r = vmaCreateImage(allocator, &image_info, &alloc_info, &tex.image, &tex.allocation, nullptr); r != VK_SUCCESS) {
        destroy_buffer(*staging);
        return vk_failure(r, "vmaCreateImage (texture)");
    }

    // Copy on the upload pool and wait (like meshes; asynchronous streaming comes later).
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
        barrier.image = tex.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_count, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
        vkCmdCopyBufferToImage(cmd, staging->buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
    if (r != VK_SUCCESS) {
        vmaDestroyImage(allocator, tex.image, tex.allocation);
        return vk_failure(r, "texture upload");
    }

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = tex.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_info.format;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_count, 0, 1};
    if (VkResult vr = vkCreateImageView(device, &view_info, nullptr, &tex.view); vr != VK_SUCCESS) {
        vmaDestroyImage(allocator, tex.image, tex.allocation);
        return vk_failure(vr, "vkCreateImageView (texture)");
    }
    const std::string label = texture.debug_name.empty() ? std::format("Texture {}", slot) : std::string(texture.debug_name);
    set_name(VK_OBJECT_TYPE_IMAGE, tex.image, label);
    tex.bytes = total;
    tex.alive = true;
    textures[slot] = tex;
    texture_bytes += total;

    VkDescriptorImageInfo image{VK_NULL_HANDLE, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture_set;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return TextureHandle{slot};
}

void Renderer::Impl::destroy_texture(TextureHandle handle) {
    if (handle.id < kFirstUserTexture || handle.id >= textures.size() || !textures[handle.id].alive) return;
    vkDeviceWaitIdle(device);  // simple and safe, like meshes; deferred deletion comes with streaming
    GpuTexture& tex = textures[handle.id];
    // Point the slot back at white, so a stale index can never read a destroyed image.
    VkDescriptorImageInfo image{VK_NULL_HANDLE, textures[kTextureWhite].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture_set;
    write.dstBinding = 0;
    write.dstArrayElement = handle.id;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    vkDestroyImageView(device, tex.view, nullptr);
    vmaDestroyImage(allocator, tex.image, tex.allocation);
    texture_bytes -= tex.bytes;
    tex = {};
}

void Renderer::Impl::destroy_textures() {
    if (device == VK_NULL_HANDLE) return;
    for (GpuTexture& tex : textures) {
        if (!tex.alive) continue;
        vkDestroyImageView(device, tex.view, nullptr);
        vmaDestroyImage(allocator, tex.image, tex.allocation);
        tex = {};
    }
    textures.clear();
    texture_bytes = 0;
    if (material_buffer.buffer != VK_NULL_HANDLE) destroy_buffer(material_buffer);
    materials.clear();
    material_alive.clear();
    if (texture_pool) vkDestroyDescriptorPool(device, texture_pool, nullptr);
    if (texture_set_layout) vkDestroyDescriptorSetLayout(device, texture_set_layout, nullptr);
    if (texture_sampler) vkDestroySampler(device, texture_sampler, nullptr);
    texture_pool = VK_NULL_HANDLE;
    texture_set_layout = VK_NULL_HANDLE;
    texture_set = VK_NULL_HANDLE;
    texture_sampler = VK_NULL_HANDLE;
}

} // namespace noor::render
