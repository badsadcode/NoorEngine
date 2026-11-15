// Frame readback: copies a presented swapchain image into CPU memory so automated tests
// can check real rendered pixels (GPU reference tests, spec section 26.1).

#include "vulkan_impl.h"

#include <cstring>

namespace noor::render {

std::array<std::uint8_t, 4> CapturedFrame::rgba_at(std::uint32_t x, std::uint32_t y) const {
    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
    if (i + 3 >= pixels.size()) {
        return {0, 0, 0, 0};
    }
    if (format.starts_with("BGRA")) {
        return {pixels[i + 2], pixels[i + 1], pixels[i], pixels[i + 3]};
    }
    return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
}

// Called inside a frame's command buffer, after rendering: color attachment -> copy -> present.
Status Renderer::Impl::record_capture(VkCommandBuffer cmd, VkImage image) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    auto buffer = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    if (!buffer) {
        return std::unexpected(buffer.error());
    }

    set_name(VK_OBJECT_TYPE_BUFFER, buffer->buffer, "Frame capture readback");
    begin_label(cmd, "Frame capture (readback for tests)", {0.98f, 0.75f, 0.14f, 1.0f});
    image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer->buffer, 1, &region);

    // Make the copy visible to the host once the frame's fence signals.
    VkBufferMemoryBarrier2 host_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    host_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    host_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    host_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    host_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_barrier.buffer = buffer->buffer;
    host_barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &host_barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

    end_label(cmd);
    capture.buffer = *buffer;
    capture.frame_slot = frame_index;
    capture.width = extent.width;
    capture.height = extent.height;
    capture.format = surface_format.format;
    capture.pending = true;
    return {};
}

Result<std::optional<CapturedFrame>> Renderer::Impl::take_capture() {
    if (!capture.pending) {
        if (capture_requested && !swapchain_supports_capture) {
            return fail(ErrorCode::Unsupported, "Swapchain images on this GPU/surface cannot be read back");
        }
        return std::optional<CapturedFrame>{};
    }
    NOOR_VK_TRY(vkWaitForFences(device, 1, &frames[capture.frame_slot].in_flight, VK_TRUE, kNoTimeout));
    NOOR_VK_TRY(vmaInvalidateAllocation(allocator, capture.buffer.allocation, 0, VK_WHOLE_SIZE));

    CapturedFrame out;
    out.width = capture.width;
    out.height = capture.height;
    switch (capture.format) {
    case VK_FORMAT_B8G8R8A8_SRGB: out.format = "BGRA8_SRGB"; break;
    case VK_FORMAT_R8G8B8A8_SRGB: out.format = "RGBA8_SRGB"; break;
    case VK_FORMAT_B8G8R8A8_UNORM: out.format = "BGRA8_UNORM"; break;
    case VK_FORMAT_R8G8B8A8_UNORM: out.format = "RGBA8_UNORM"; break;
    default: out.format = string_VkFormat(capture.format); break;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(capture.buffer.mapped);
    out.pixels.assign(bytes, bytes + static_cast<std::size_t>(out.width) * out.height * 4);

    destroy_buffer(capture.buffer);
    capture = {};
    return std::optional<CapturedFrame>{std::move(out)};
}

} // namespace noor::render
