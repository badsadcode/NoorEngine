// Game UI (gameplay foundations part 6, ADR-041): RmlUi's geometry, drawn last in the screen pass, over the HUD. One
// pipeline (no depth, premultiplied blending), textures from the bindless table (set 1), and host-visible vertex and
// index buffers per frame in flight, refilled every frame (a menu is a few thousand vertices). Each draw has its own
// texture, translation and clip rectangle: push constants and a scissor.
#include "vulkan_impl.h"

#include "noor/shaders/ui_ps.h"
#include "noor/shaders/ui_vs.h"

#include <cstring>

namespace noor::render {

namespace {

struct UiPush {
    float scale[2];
    float translation[2];
    std::uint32_t texture;
    std::uint32_t unused[3];
};
static_assert(sizeof(UiPush) == 32);

} // namespace

Status Renderer::Impl::ensure_ui(VkFormat color_format) {
    if (ui.pipeline != VK_NULL_HANDLE && ui.color_format == color_format) return {};
    if (auto st = ensure_texture_table(); !st) return st;
    if (ui.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, ui.pipeline, nullptr);
        ui.pipeline = VK_NULL_HANDLE;
    }
    if (ui.layout == VK_NULL_HANDLE) {
        // Set 0 is the frame's (not used here, but the texture table is set 1 in every shader).
        const VkDescriptorSetLayout set_layouts[2] = {frame_set_layout, texture_set_layout};
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UiPush)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        NOOR_VK_TRY(vkCreatePipelineLayout(device, &layout_info, nullptr, &ui.layout));
    }

    auto make_module = [&](const std::uint8_t* code, std::size_t size, VkShaderModule& out) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = reinterpret_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device, &info, nullptr, &out);
    };
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    NOOR_VK_TRY(make_module(k_ui_vs, sizeof(k_ui_vs), vs));
    if (VkResult r = make_module(k_ui_ps, sizeof(k_ui_ps), ps); r != VK_SUCCESS) {
        vkDestroyShaderModule(device, vs, nullptr);
        return vk_failure(r, "vkCreateShaderModule (UI pixel shader)");
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "VSMain", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "PSMain", nullptr};

    const VkVertexInputBindingDescription vertex_binding{0, sizeof(UiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[3] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, position)},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UiVertex, color)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &vertex_binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions = attributes;
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
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // premultiplied
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
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
    rendering.depthAttachmentFormat = kDepthFormat;  // drawn in the screen pass, like the HUD
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
    pipeline_info.layout = ui.layout;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &ui.pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) return vk_failure(result, "vkCreateGraphicsPipelines (UI)");
    ui.color_format = color_format;
    set_name(VK_OBJECT_TYPE_PIPELINE, ui.pipeline, "UI pipeline (game UI documents)");
    return {};
}

Status Renderer::Impl::record_ui(VkCommandBuffer cmd, const UiFrame& frame) {
    if (frame.draws.empty() || frame.vertices.empty() || frame.indices.empty()) return {};
    // Made the first time a frame has UI: a game without any pays nothing (Principle 0).
    if (auto st = ensure_ui(surface_format.format); !st) return st;
    // Grows a frame slot's buffer (its previous contents are done: the slot's fence was waited).
    auto fill = [&](GpuBuffer& buffer, std::size_t& capacity, const void* data, std::size_t bytes, VkBufferUsageFlags usage,
                    const char* what) -> Status {
        if (bytes > capacity) {
            if (buffer.buffer != VK_NULL_HANDLE) destroy_buffer(buffer);
            capacity = std::max<std::size_t>(bytes * 2, 64 * 1024);
            auto b = create_buffer(capacity, usage, true, false);
            if (!b) {
                capacity = 0;
                return std::unexpected(b.error());
            }
            buffer = *b;
            set_name(VK_OBJECT_TYPE_BUFFER, buffer.buffer, std::format("UI {} (frame slot {})", what, frame_index));
        }
        std::memcpy(buffer.mapped, data, bytes);
        vmaFlushAllocation(allocator, buffer.allocation, 0, bytes);
        return {};
    };
    if (auto st = fill(ui.vertices[frame_index], ui.vertex_capacity[frame_index], frame.vertices.data(), frame.vertices.size_bytes(),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "vertices");
        !st)
        return st;
    if (auto st = fill(ui.indices[frame_index], ui.index_capacity[frame_index], frame.indices.data(), frame.indices.size_bytes(),
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "indices");
        !st)
        return st;

    begin_label(cmd, "Game UI", {0.49f, 0.30f, 1.0f, 1.0f});  // purple
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.pipeline);
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.layout, 1, 1, &texture_set, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &ui.vertices[frame_index].buffer, &offset);
    vkCmdBindIndexBuffer(cmd, ui.indices[frame_index].buffer, 0, VK_INDEX_TYPE_UINT32);
    const auto width = static_cast<std::int32_t>(extent.width), height = static_cast<std::int32_t>(extent.height);
    const auto vertex_count = static_cast<std::int64_t>(frame.vertices.size());
    for (const UiDraw& d : frame.draws) {
        if (d.index_count == 0 || static_cast<std::size_t>(d.first_index) + d.index_count > frame.indices.size()) continue;
        if (d.vertex_offset < 0 || d.vertex_offset >= vertex_count) continue;
        // The clip rectangle, kept on the screen (Vulkan wants a non-negative offset).
        std::int32_t x0 = 0, y0 = 0, x1 = width, y1 = height;
        if (d.clip) {
            x0 = std::clamp(d.clip_rect[0], 0, width);
            y0 = std::clamp(d.clip_rect[1], 0, height);
            x1 = std::clamp(d.clip_rect[0] + std::max(0, d.clip_rect[2]), 0, width);
            y1 = std::clamp(d.clip_rect[1] + std::max(0, d.clip_rect[3]), 0, height);
        }
        if (x1 <= x0 || y1 <= y0) continue;  // clipped away
        const VkRect2D scissor{{x0, y0}, {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        const std::uint32_t slot = d.texture.id < textures.size() && textures[d.texture.id].alive ? d.texture.id : kTextureWhite;
        const UiPush push{{2.0f / static_cast<float>(extent.width), 2.0f / static_cast<float>(extent.height)},
                          {d.translation.x, d.translation.y},
                          slot,
                          {0, 0, 0}};
        vkCmdPushConstants(cmd, ui.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, d.index_count, 1, d.first_index, d.vertex_offset, 0);
        stats.draw_calls += 1;
    }
    end_label(cmd);
    return {};
}

void Renderer::Impl::destroy_ui() {
    if (device == VK_NULL_HANDLE) return;
    for (auto& b : ui.vertices)
        if (b.buffer != VK_NULL_HANDLE) destroy_buffer(b);
    for (auto& b : ui.indices)
        if (b.buffer != VK_NULL_HANDLE) destroy_buffer(b);
    if (ui.pipeline) vkDestroyPipeline(device, ui.pipeline, nullptr);
    if (ui.layout) vkDestroyPipelineLayout(device, ui.layout, nullptr);
    ui = {};
}

} // namespace noor::render
