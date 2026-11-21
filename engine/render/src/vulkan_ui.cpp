// Game UI (gameplay foundations part 6, ADR-041; effects 6c): RmlUi's commands, run after the screen pass.
//
// The UI is drawn in layers of its own, screen-sized, 8-bit with sRGB values premultiplied by alpha (blending in sRGB,
// as a browser and RmlUi's reference renderer do), multisampled with a shared stencil for clip masks. The screen is
// copied first (the backdrop): the finished base layer is laid over it in sRGB and back onto the screen (PSFinal: into
// linear light for the sRGB swapchain), and a backdrop filter sees it under the base layer, so it blurs the game behind
// a panel. Resolves and filters cover only the element's area, not the whole screen. Filters (opacity, blur, drop shadow, colour
// matrices, masks) work on three single-sample "post" images, ping-ponged like RmlUi_Renderer_GL3.cpp's; those, the
// backdrop and the mask are read through the bindless table. Everything is made the first time a frame has UI (a game
// without UI pays nothing) and remade when the screen changes size.
#include "vulkan_impl.h"

#include "noor/shaders/ui_blur_ps.h"
#include "noor/shaders/ui_clip_ps.h"
#include "noor/shaders/ui_color_matrix_ps.h"
#include "noor/shaders/ui_draw_ps.h"
#include "noor/shaders/ui_drop_shadow_ps.h"
#include "noor/shaders/ui_final_ps.h"
#include "noor/shaders/ui_full_plain_vs.h"
#include "noor/shaders/ui_full_vs.h"
#include "noor/shaders/ui_gradient_ps.h"
#include "noor/shaders/ui_mask_ps.h"
#include "noor/shaders/ui_opaque_ps.h"
#include "noor/shaders/ui_pass_ps.h"
#include "noor/shaders/ui_vs.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace noor::render {

namespace {

struct Push {  // ui.hlsl's PushConstants
    float c0[4], c1[4], c2[4], c3[4];
    float translation[2];
    float origin[2];
    float scale[2];
    std::uint32_t texture;
    std::uint32_t extra;
    float data[2][4];
};
static_assert(sizeof(Push) == 128, "the push-constant block every UI program shares");

struct GpuGradient {  // ui.hlsl's Gradient
    std::uint32_t kind, stops;
    float p[2], v[2], unused[2];
    float positions[16];
    float colors[16][4];
};
static_assert(sizeof(GpuGradient) == 352);

enum class Prog : std::uint8_t { Draw, Gradient, Clip, Pass, Opaque, ColorMatrix, Blur, DropShadow, Mask, Final };
enum class Target : std::uint8_t { Layer, Post, Screen };
enum class Blend : std::uint8_t { None, Premultiplied, Under };  // Under: drawn beneath what's there

struct Rect {
    std::int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    [[nodiscard]] bool empty() const { return x1 <= x0 || y1 <= y0; }
};

constexpr VkPipelineStageFlags2 kAll = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
constexpr VkAccessFlags2 kAny = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

// RmlUi's reference: a blur of a big sigma is done on an image halved `level` times, at a sigma of at most 3.
void blur_parameters(float sigma, int& level, float& level_sigma) {
    const int steps = static_cast<int>(std::floor(std::log2(std::max(1.0f, sigma * (2.0f / 3.0f)))));
    level = std::clamp(steps, 0, 10);
    level_sigma = std::clamp(sigma / static_cast<float>(1 << level), 0.0f, 3.0f);
}

void blur_weights(float sigma, float out[4]) {
    float sum = 0;
    for (int i = 0; i < 4; ++i) {
        out[i] = std::abs(sigma) < 0.1f ? float(i == 0) : std::exp(-float(i * i) / (2 * sigma * sigma)) / (std::sqrt(2 * 3.14159265f) * sigma);
        sum += (i == 0 ? 1.0f : 2.0f) * out[i];
    }
    for (int i = 0; i < 4; ++i) out[i] /= sum;
}

void identity(Push& p) {
    p.c0[0] = 1, p.c1[1] = 1, p.c2[2] = 1, p.c3[3] = 1;
}

} // namespace

struct UiImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    std::uint32_t slot = 0;  // in the texture table (images that are read)
    std::uint64_t bytes = 0;
};

struct Renderer::Impl::UiRenderer {
    Renderer::Impl& r;
    explicit UiRenderer(Renderer::Impl& impl) : r(impl) {}

    VkFormat screen_format = VK_FORMAT_UNDEFINED, layer_format = VK_FORMAT_UNDEFINED, stencil_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkExtent2D size{};
    std::vector<UiImage> layers;  // the stack's images (0: the base), made as deep as pages need
    UiImage stencil, mask, backdrop;
    std::array<UiImage, 3> post;
    std::array<int, 3> role{0, 1, 2};  // post images as primary, secondary, tertiary
    bool backdrop_ok = false;          // the screen can be copied (it can be read: screenshots use the same)

    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> sets{};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::map<std::uint32_t, VkPipeline> pipelines;
    std::array<GpuBuffer, kFramesInFlight> vertices{}, indices{}, gradients{};
    std::array<std::size_t, kFramesInFlight> vertex_capacity{}, index_capacity{}, gradient_capacity{};

    // While recording a frame.
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    int open = -1;                  // the layer with an open rendering scope
    bool stencil_fresh = true;      // the first scope of the frame clears the clip mask
    std::vector<std::uint32_t> stack;
    const UiFrame* frame = nullptr;

    // ---- Resources ----

    Result<UiImage> make_image(VkFormat format, VkSampleCountFlagBits count, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                               bool sampled, const std::string& name) {
        UiImage im;
        im.aspect = aspect;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = {size.width, size.height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = count;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (VkResult res = vmaCreateImage(r.allocator, &ci, &alloc, &im.image, &im.allocation, nullptr); res != VK_SUCCESS)
            return std::unexpected(vk_failure(res, ("vmaCreateImage (" + name + ")").c_str()).error());
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = im.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, 1, 0, 1};
        if (VkResult res = vkCreateImageView(r.device, &view, nullptr, &im.view); res != VK_SUCCESS)
            return std::unexpected(vk_failure(res, ("vkCreateImageView (" + name + ")").c_str()).error());
        r.set_name(VK_OBJECT_TYPE_IMAGE, im.image, name);
        im.bytes = static_cast<std::uint64_t>(size.width) * size.height * 4 * static_cast<std::uint64_t>(count);
        if (sampled) {
            auto slot = r.claim_texture_slot(im.view);
            if (!slot) return std::unexpected(slot.error());
            im.slot = *slot;
        }
        return im;
    }
    void free_image(UiImage& im) {
        if (im.slot) r.release_texture_slot(im.slot);
        if (im.view) vkDestroyImageView(r.device, im.view, nullptr);
        if (im.image) vmaDestroyImage(r.allocator, im.image, im.allocation);
        im = {};
    }
    void free_images() {
        for (auto& l : layers) free_image(l);
        layers.clear();
        free_image(stencil);
        free_image(mask);
        free_image(backdrop);
        for (auto& p : post) free_image(p);
    }

    Status ensure(VkFormat target_format, VkExtent2D extent) {
        if (set_layout == VK_NULL_HANDLE) {
            if (auto st = make_layouts(); !st) return st;
        }
        if (target_format != screen_format) {  // new pipelines for the new formats
            vkDeviceWaitIdle(r.device);
            for (auto& [key, p] : pipelines) vkDestroyPipeline(r.device, p, nullptr);
            pipelines.clear();
            screen_format = target_format;
            switch (target_format) {
            case VK_FORMAT_B8G8R8A8_SRGB: layer_format = VK_FORMAT_B8G8R8A8_UNORM; break;
            case VK_FORMAT_R8G8B8A8_SRGB: layer_format = VK_FORMAT_R8G8B8A8_UNORM; break;
            case VK_FORMAT_A8B8G8R8_SRGB_PACK32: layer_format = VK_FORMAT_A8B8G8R8_UNORM_PACK32; break;
            default: layer_format = target_format; break;
            }
            size = {};
        }
        if (extent.width == size.width && extent.height == size.height && !layers.empty()) return {};
        vkDeviceWaitIdle(r.device);  // the old images may still be read by frames in flight (a resize: rare)
        free_images();
        size = extent;
        backdrop_ok = r.swapchain_supports_capture;
        const VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        auto base = make_image(layer_format, samples, color, VK_IMAGE_ASPECT_COLOR_BIT, false, "UI layer 0");
        if (!base) return std::unexpected(base.error());
        layers.push_back(*base);
        const VkImageAspectFlags stencil_aspect =
            stencil_format == VK_FORMAT_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        auto s = make_image(stencil_format, samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, stencil_aspect, false, "UI clip mask");
        if (!s) return std::unexpected(s.error());
        stencil = *s;
        stencil.bytes = static_cast<std::uint64_t>(size.width) * size.height * (stencil_format == VK_FORMAT_S8_UINT ? 1 : 4) * samples;
        const VkImageUsageFlags readable = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        for (int i = 0; i < 3; ++i) {
            auto p = make_image(layer_format, VK_SAMPLE_COUNT_1_BIT, readable, VK_IMAGE_ASPECT_COLOR_BIT, true, std::format("UI post {}", i));
            if (!p) return std::unexpected(p.error());
            post[static_cast<std::size_t>(i)] = *p;
        }
        auto m = make_image(layer_format, VK_SAMPLE_COUNT_1_BIT, readable, VK_IMAGE_ASPECT_COLOR_BIT, true, "UI mask image");
        if (!m) return std::unexpected(m.error());
        mask = *m;
        if (backdrop_ok) {
            auto b = make_image(layer_format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT, true, "UI backdrop (the screen)");
            if (!b) return std::unexpected(b.error());
            backdrop = *b;
        }
        return {};
    }

    Status make_layouts() {
        // The clip mask's format: what this GPU can do (S8 alone is rare on some vendors).
        for (const VkFormat f : {VK_FORMAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(r.gpu, f, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                stencil_format = f;
                break;
            }
        }
        if (stencil_format == VK_FORMAT_UNDEFINED) return fail(ErrorCode::Unsupported, "game UI: no stencil format for clip masks");
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(r.gpu, &props);
        const VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferStencilSampleCounts;
        // 2 samples, as RmlUi's reference renderer: smooth rounded and rotated edges for 60% of 4's cost (measured,
        // docs/ui/GAME_UI.md). NOOR_UI_MSAA (1, 2, 4) overrides it, to compare.
        int want = 2;
        if (const char* e = std::getenv("NOOR_UI_MSAA")) want = std::atoi(e);
        samples = (want >= 4 && (counts & VK_SAMPLE_COUNT_4_BIT)) ? VK_SAMPLE_COUNT_4_BIT
                  : (want >= 2 && (counts & VK_SAMPLE_COUNT_2_BIT)) ? VK_SAMPLE_COUNT_2_BIT
                                                                  : VK_SAMPLE_COUNT_1_BIT;

        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_info.bindingCount = 1;
        set_info.pBindings = &binding;
        NOOR_VK_TRY(vkCreateDescriptorSetLayout(r.device, &set_info, nullptr, &set_layout));
        const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = kFramesInFlight;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        NOOR_VK_TRY(vkCreateDescriptorPool(r.device, &pool_info, nullptr, &pool));
        std::array<VkDescriptorSetLayout, kFramesInFlight> layouts;
        layouts.fill(set_layout);
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = pool;
        alloc.descriptorSetCount = kFramesInFlight;
        alloc.pSetLayouts = layouts.data();
        NOOR_VK_TRY(vkAllocateDescriptorSets(r.device, &alloc, sets.data()));

        if (auto st = r.ensure_texture_table(); !st) return st;
        const VkDescriptorSetLayout set_layouts[2] = {set_layout, r.texture_set_layout};
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        NOOR_VK_TRY(vkCreatePipelineLayout(r.device, &layout_info, nullptr, &layout));
        return {};
    }

    Result<VkPipeline> pipeline(Prog prog, Target target, Blend blend) {
        const std::uint32_t key = static_cast<std::uint32_t>(prog) | static_cast<std::uint32_t>(target) << 8 | static_cast<std::uint32_t>(blend) << 16;
        if (const auto it = pipelines.find(key); it != pipelines.end()) return it->second;

        const bool geometry = prog == Prog::Draw || prog == Prog::Gradient || prog == Prog::Clip;
        std::span<const std::uint8_t> vs, ps;
        if (geometry) vs = {k_ui_vs, sizeof(k_ui_vs)};
        else if (prog == Prog::ColorMatrix) vs = {k_ui_full_plain_vs, sizeof(k_ui_full_plain_vs)};
        else vs = {k_ui_full_vs, sizeof(k_ui_full_vs)};
        switch (prog) {
        case Prog::Draw: ps = {k_ui_draw_ps, sizeof(k_ui_draw_ps)}; break;
        case Prog::Gradient: ps = {k_ui_gradient_ps, sizeof(k_ui_gradient_ps)}; break;
        case Prog::Clip: ps = {k_ui_clip_ps, sizeof(k_ui_clip_ps)}; break;
        case Prog::Pass: ps = {k_ui_pass_ps, sizeof(k_ui_pass_ps)}; break;
        case Prog::Opaque: ps = {k_ui_opaque_ps, sizeof(k_ui_opaque_ps)}; break;
        case Prog::ColorMatrix: ps = {k_ui_color_matrix_ps, sizeof(k_ui_color_matrix_ps)}; break;
        case Prog::Blur: ps = {k_ui_blur_ps, sizeof(k_ui_blur_ps)}; break;
        case Prog::DropShadow: ps = {k_ui_drop_shadow_ps, sizeof(k_ui_drop_shadow_ps)}; break;
        case Prog::Mask: ps = {k_ui_mask_ps, sizeof(k_ui_mask_ps)}; break;
        case Prog::Final: ps = {k_ui_final_ps, sizeof(k_ui_final_ps)}; break;
        }
        auto make_module = [&](std::span<const std::uint8_t> code, VkShaderModule& out) {
            VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            ci.codeSize = code.size();
            ci.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
            return vkCreateShaderModule(r.device, &ci, nullptr, &out);
        };
        VkShaderModule vsm = VK_NULL_HANDLE, psm = VK_NULL_HANDLE;
        if (VkResult res = make_module(vs, vsm); res != VK_SUCCESS) return std::unexpected(vk_failure(res, "UI vertex shader").error());
        if (VkResult res = make_module(ps, psm); res != VK_SUCCESS) {
            vkDestroyShaderModule(r.device, vsm, nullptr);
            return std::unexpected(vk_failure(res, "UI pixel shader").error());
        }
        const char* vs_entry = geometry ? "VSMain" : prog == Prog::ColorMatrix ? "VSFullPlain" : "VSFull";
        const char* ps_entry = "PSMain";
        switch (prog) {
        case Prog::Draw: ps_entry = "PSDraw"; break;
        case Prog::Gradient: ps_entry = "PSGradient"; break;
        case Prog::Clip: ps_entry = "PSClip"; break;
        case Prog::Pass: ps_entry = "PSPass"; break;
        case Prog::Opaque: ps_entry = "PSOpaque"; break;
        case Prog::ColorMatrix: ps_entry = "PSColorMatrix"; break;
        case Prog::Blur: ps_entry = "PSBlur"; break;
        case Prog::DropShadow: ps_entry = "PSDropShadow"; break;
        case Prog::Mask: ps_entry = "PSMask"; break;
        case Prog::Final: ps_entry = "PSFinal"; break;
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vsm, vs_entry, nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, psm, ps_entry, nullptr};

        const VkVertexInputBindingDescription vertex_binding{0, sizeof(UiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[3] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, position)},
            {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UiVertex, color)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, uv)},
        };
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (geometry) {
            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &vertex_binding;
            vertex_input.vertexAttributeDescriptionCount = 3;
            vertex_input.pVertexAttributeDescriptions = attributes;
        }
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
        multisample.rasterizationSamples = target == Target::Layer ? samples : VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};  // stencil: dynamic
        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.blendEnable = blend != Blend::None ? VK_TRUE : VK_FALSE;
        const bool under = blend == Blend::Under;
        blend_attachment.srcColorBlendFactor = under ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : VK_BLEND_FACTOR_ONE;
        blend_attachment.dstColorBlendFactor = under ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = under ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = under ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.colorWriteMask = prog == Prog::Clip ? 0u
                                                             : VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                                                   VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend_state{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend_state.attachmentCount = 1;
        blend_state.pAttachments = &blend_attachment;
        std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        if (target == Target::Layer)
            dynamic_states.insert(dynamic_states.end(), {VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP,
                                                         VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                         VK_DYNAMIC_STATE_STENCIL_REFERENCE});
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
        dynamic.pDynamicStates = dynamic_states.data();
        const VkFormat color_format = target == Target::Screen ? screen_format : layer_format;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &color_format;
        if (target == Target::Layer) rendering.stencilAttachmentFormat = stencil_format;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.pNext = &rendering;
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vertex_input;
        ci.pInputAssemblyState = &assembly;
        ci.pViewportState = &viewport;
        ci.pRasterizationState = &raster;
        ci.pMultisampleState = &multisample;
        ci.pDepthStencilState = &depth;
        ci.pColorBlendState = &blend_state;
        ci.pDynamicState = &dynamic;
        ci.layout = layout;
        VkPipeline p = VK_NULL_HANDLE;
        const VkResult res = vkCreateGraphicsPipelines(r.device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
        vkDestroyShaderModule(r.device, vsm, nullptr);
        vkDestroyShaderModule(r.device, psm, nullptr);
        if (res != VK_SUCCESS) return std::unexpected(vk_failure(res, "vkCreateGraphicsPipelines (UI)").error());
        r.set_name(VK_OBJECT_TYPE_PIPELINE, p, std::format("UI pipeline {} {} {}", ps_entry, static_cast<int>(target), static_cast<int>(blend)));
        pipelines.emplace(key, p);
        return p;
    }

    // ---- Recording ----

    void barrier(UiImage& im, VkImageLayout to) {
        image_barrier(cmd, im.image, im.aspect, im.layout, to, kAll, kAny, kAll, kAny);
        im.layout = to;
    }
    void end() {
        if (open < 0) return;
        vkCmdEndRendering(cmd);
        open = -1;
    }
    VkRect2D full() const { return {{0, 0}, size}; }
    VkRect2D clamp_rect(const std::array<std::int32_t, 4>& rect) const {
        const auto w = static_cast<std::int32_t>(size.width), h = static_cast<std::int32_t>(size.height);
        const std::int32_t ox = static_cast<std::int32_t>(std::lround(frame->origin.x)), oy = static_cast<std::int32_t>(std::lround(frame->origin.y));
        const std::int32_t x0 = std::clamp(rect[0] + ox, 0, w), y0 = std::clamp(rect[1] + oy, 0, h);
        const std::int32_t x1 = std::clamp(rect[0] + ox + std::max(0, rect[2]), 0, w), y1 = std::clamp(rect[1] + oy + std::max(0, rect[3]), 0, h);
        return {{x0, y0}, {static_cast<std::uint32_t>(std::max(0, x1 - x0)), static_cast<std::uint32_t>(std::max(0, y1 - y0))}};
    }
    VkRect2D scissor_of(const UiCommand& c) const { return c.scissor ? clamp_rect(c.rect) : full(); }
    void viewport(std::uint32_t w, std::uint32_t h) {
        const VkViewport vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
    }
    void bind_sets() {
        const VkDescriptorSet s[2] = {sets[r.frame_index], r.texture_set};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, s, 0, nullptr);
    }

    // Opens a rendering scope on a layer of the stack (its contents kept unless `clear`).
    void begin_layer(std::uint32_t index, bool clear) {
        if (open == static_cast<int>(index) && !clear) return;
        end();
        UiImage& l = layers[index];
        barrier(l, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        barrier(stencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = l.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0, 0, 0, 0}};
        VkRenderingAttachmentInfo st{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        st.imageView = stencil.view;
        st.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        st.loadOp = stencil_fresh ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        st.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        st.clearValue.depthStencil = {0.0f, 0};
        stencil_fresh = false;
        VkRenderingInfo ci{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ci.renderArea = full();
        ci.layerCount = 1;
        ci.colorAttachmentCount = 1;
        ci.pColorAttachments = &color;
        ci.pStencilAttachment = &st;
        vkCmdBeginRendering(cmd, &ci);
        viewport(size.width, size.height);
        bind_sets();
        open = static_cast<int>(index);
    }
    void ensure_top() {
        if (open != static_cast<int>(stack.back())) begin_layer(stack.back(), false);
    }
    void stencil_test(bool test, std::uint8_t ref) {
        vkCmdSetStencilTestEnable(cmd, test ? VK_TRUE : VK_FALSE);
        vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL);
        vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
        vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
    }

    // A layer's samples averaged into the primary post image, inside `area`.
    void resolve(std::uint32_t layer_index, VkRect2D area) {
        end();
        if (area.extent.width == 0 || area.extent.height == 0) return;
        UiImage& l = layers[layer_index];
        UiImage& p = post[static_cast<std::size_t>(role[0])];
        if (samples == VK_SAMPLE_COUNT_1_BIT) {
            barrier(l, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            barrier(p, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy copy{};
            copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.srcOffset = copy.dstOffset = {area.offset.x, area.offset.y, 0};
            copy.extent = {area.extent.width, area.extent.height, 1};
            vkCmdCopyImage(cmd, l.image, l.layout, p.image, p.layout, 1, &copy);
            return;
        }
        barrier(l, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        barrier(p, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = l.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = p.view;
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkRenderingInfo ci{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ci.renderArea = area;  // resolved only here
        ci.layerCount = 1;
        ci.colorAttachmentCount = 1;
        ci.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &ci);
        vkCmdEndRendering(cmd);
    }
    // The base layer as a source (a backdrop filter): the screen goes under it, as it is under the finished UI.
    Status resolve_source(std::uint32_t layer_index, VkRect2D area) {
        resolve(layer_index, area);
        if (layer_index != 0 || !backdrop_ok || area.extent.width == 0 || area.extent.height == 0) return {};
        return pass(Prog::Opaque, primary(), Blend::Under, copy_push(backdrop), area, {&backdrop});
    }

    // A pass over a whole single-sample image (a post image or the mask), reading `sources`.
    Status pass(Prog prog, UiImage& target, Blend blend, const Push& push, VkRect2D scissor, std::initializer_list<UiImage*> sources,
                VkExtent2D view = {}) {
        end();
        for (UiImage* s : sources) barrier(*s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        barrier(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        auto p = pipeline(prog, Target::Post, blend);
        if (!p) return std::unexpected(p.error());
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = target.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ci{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ci.renderArea = scissor;  // only the element's area is touched
        ci.layerCount = 1;
        ci.colorAttachmentCount = 1;
        ci.pColorAttachments = &color;
        if (scissor.extent.width == 0 || scissor.extent.height == 0) return {};
        vkCmdBeginRendering(cmd, &ci);
        viewport(view.width ? view.width : size.width, view.height ? view.height : size.height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *p);
        bind_sets();
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        return {};
    }
    Push copy_push(const UiImage& from, float factor = 1.0f) const {
        Push p{};
        p.c0[2] = p.c0[3] = 1.0f;  // uv scale 1, offset 0
        p.c2[0] = factor;
        p.texture = from.slot;
        return p;
    }
    void limits(Push& p, VkRect2D rect) const {  // texture coordinates kept inside `rect`, half a texel in (no bleeding)
        p.c1[0] = (static_cast<float>(rect.offset.x) + 0.5f) / static_cast<float>(size.width);
        p.c1[1] = (static_cast<float>(rect.offset.y) + 0.5f) / static_cast<float>(size.height);
        p.c1[2] = (static_cast<float>(rect.offset.x + static_cast<std::int32_t>(rect.extent.width)) - 0.5f) / static_cast<float>(size.width);
        p.c1[3] = (static_cast<float>(rect.offset.y + static_cast<std::int32_t>(rect.extent.height)) - 0.5f) / static_cast<float>(size.height);
    }
    UiImage& primary() { return post[static_cast<std::size_t>(role[0])]; }
    UiImage& secondary() { return post[static_cast<std::size_t>(role[1])]; }
    UiImage& tertiary() { return post[static_cast<std::size_t>(role[2])]; }
    void swap_primary() { std::swap(role[0], role[1]); }

    void blit(UiImage& from, VkRect2D src, UiImage& to, VkRect2D dst) {
        end();
        barrier(from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        barrier(to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit b{};
        b.srcSubresource = b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b.srcOffsets[0] = {src.offset.x, src.offset.y, 0};
        b.srcOffsets[1] = {src.offset.x + static_cast<std::int32_t>(src.extent.width), src.offset.y + static_cast<std::int32_t>(src.extent.height), 1};
        b.dstOffsets[0] = {dst.offset.x, dst.offset.y, 0};
        b.dstOffsets[1] = {dst.offset.x + static_cast<std::int32_t>(dst.extent.width), dst.offset.y + static_cast<std::int32_t>(dst.extent.height), 1};
        vkCmdBlitImage(cmd, from.image, from.layout, to.image, to.layout, 1, &b, VK_FILTER_LINEAR);
    }

    // RmlUi's blur: halve the image `level` times, a 7-tap Gaussian down and across, then scale back up into `window`.
    // `sd` holds the image before and the blur after; `temp` is scratch.
    Status blur(float sigma, UiImage& sd, UiImage& temp, VkRect2D window) {
        if (window.extent.width == 0 || window.extent.height == 0) return {};
        int level = 0;
        blur_parameters(sigma, level, sigma);
        Rect s{window.offset.x, window.offset.y, window.offset.x + static_cast<std::int32_t>(window.extent.width),
               window.offset.y + static_cast<std::int32_t>(window.extent.height)};
        const auto to_vk = [](Rect q) { return VkRect2D{{q.x0, q.y0}, {static_cast<std::uint32_t>(q.x1 - q.x0), static_cast<std::uint32_t>(q.y1 - q.y0)}}; };
        const VkExtent2D half{std::max(1u, size.width / 2), std::max(1u, size.height / 2)};
        const float sx = (size.width % 2 == 1) ? 1.0f - 1.0f / static_cast<float>(size.width) : 1.0f;
        const float sy = (size.height % 2 == 1) ? 1.0f - 1.0f / static_cast<float>(size.height) : 1.0f;
        for (int i = 0; i < level; ++i) {
            s.x0 = (s.x0 + 1) / 2, s.y0 = (s.y0 + 1) / 2;
            s.x1 = std::max(s.x1 / 2, s.x0), s.y1 = std::max(s.y1 / 2, s.y0);
            UiImage& from = (i % 2 == 0) ? sd : temp;
            UiImage& to = (i % 2 == 0) ? temp : sd;
            Push p = copy_push(from);
            p.c0[2] = sx, p.c0[3] = sy;
            if (auto st = pass(Prog::Pass, to, Blend::None, p, to_vk(s), {&from}, half); !st) return st;
        }
        if (s.empty()) return {};
        if (level % 2 == 0)  // the downscaled image into temp
            if (auto st = pass(Prog::Pass, temp, Blend::None, copy_push(sd), to_vk(s), {&sd}); !st) return st;
        Push p = copy_push(temp);
        blur_weights(sigma, p.data[0]);
        limits(p, to_vk(s));
        p.c3[0] = 0.0f, p.c3[1] = 1.0f / static_cast<float>(size.height);
        if (auto st = pass(Prog::Blur, sd, Blend::None, p, to_vk(s), {&temp}); !st) return st;  // down
        // Across, into temp, with a transparent pixel round it (no smearing when scaled up).
        {
            end();
            barrier(sd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            barrier(temp, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            Rect e{std::max(0, s.x0 - 1), std::max(0, s.y0 - 1), std::min(static_cast<std::int32_t>(size.width), s.x1 + 1),
                   std::min(static_cast<std::int32_t>(size.height), s.y1 + 1)};
            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = temp.view;
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo ci{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ci.renderArea = full();
            ci.layerCount = 1;
            ci.colorAttachmentCount = 1;
            ci.pColorAttachments = &color;
            vkCmdBeginRendering(cmd, &ci);
            VkClearAttachment clear{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
            clear.clearValue.color = {{0, 0, 0, 0}};
            const VkClearRect rect{to_vk(e), 0, 1};
            vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
            auto pl = pipeline(Prog::Blur, Target::Post, Blend::None);
            if (!pl) return std::unexpected(pl.error());
            viewport(size.width, size.height);
            const VkRect2D sc = to_vk(s);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pl);
            bind_sets();
            Push h = copy_push(sd);
            blur_weights(sigma, h.data[0]);
            limits(h, sc);
            h.c3[0] = 1.0f / static_cast<float>(size.width), h.c3[1] = 0.0f;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(h), &h);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRendering(cmd);
        }
        // Back up to the window's size, filtered.
        blit(temp, to_vk(s), sd, window);
        return {};
    }

    Status filters(const UiCommand& c, VkRect2D window) {
        for (std::uint32_t i = 0; i < c.filter_count; ++i) {
            const std::size_t at = static_cast<std::size_t>(c.first_filter) + i;
            if (at >= frame->filters.size()) break;
            const UiFilter& f = frame->filters[at];
            switch (f.kind) {
            case UiFilter::Kind::Opacity:
                if (auto st = pass(Prog::Pass, secondary(), Blend::None, copy_push(primary(), f.value), window, {&primary()}); !st) return st;
                swap_primary();
                break;
            case UiFilter::Kind::ColorMatrix: {
                Push p{};
                std::memcpy(p.c0, &f.matrix[0], 16);
                std::memcpy(p.c1, &f.matrix[4], 16);
                std::memcpy(p.c2, &f.matrix[8], 16);
                std::memcpy(p.c3, &f.matrix[12], 16);
                p.texture = primary().slot;
                if (auto st = pass(Prog::ColorMatrix, secondary(), Blend::None, p, window, {&primary()}); !st) return st;
                swap_primary();
                break;
            }
            case UiFilter::Kind::Blur:
                if (auto st = blur(f.value, primary(), secondary(), window); !st) return st;
                break;
            case UiFilter::Kind::DropShadow: {
                Push p = copy_push(primary());
                p.c0[0] = -f.offset.x / static_cast<float>(size.width);
                p.c0[1] = -f.offset.y / static_cast<float>(size.height);
                limits(p, window);
                p.c2[0] = f.color.x, p.c2[1] = f.color.y, p.c2[2] = f.color.z, p.c2[3] = f.color.w;
                if (auto st = pass(Prog::DropShadow, secondary(), Blend::None, p, window, {&primary()}); !st) return st;
                if (f.value >= 0.5f)
                    if (auto st = blur(f.value, secondary(), tertiary(), window); !st) return st;
                if (auto st = pass(Prog::Pass, secondary(), Blend::Premultiplied, copy_push(primary()), window, {&primary()}); !st) return st;
                swap_primary();
                break;
            }
            case UiFilter::Kind::MaskImage: {
                Push p = copy_push(primary());
                p.extra = mask.slot;
                if (auto st = pass(Prog::Mask, secondary(), Blend::None, p, window, {&primary(), &mask}); !st) return st;
                swap_primary();
                break;
            }
            }
        }
        return {};
    }

    Status draw_geometry(const UiCommand& c) {
        if (c.index_count == 0 || static_cast<std::size_t>(c.first_index) + c.index_count > frame->indices.size()) return {};
        if (c.vertex_offset < 0 || static_cast<std::size_t>(c.vertex_offset) >= frame->vertices.size()) return {};
        const VkRect2D sc = scissor_of(c);
        if (sc.extent.width == 0 || sc.extent.height == 0) return {};
        const Prog prog = c.kind == UiCommand::Kind::Gradient ? Prog::Gradient : c.kind == UiCommand::Kind::ClipMask ? Prog::Clip : Prog::Draw;
        auto p = pipeline(prog, Target::Layer, Blend::Premultiplied);
        if (!p) return std::unexpected(p.error());
        ensure_top();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *p);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertices[r.frame_index].buffer, &offset);
        vkCmdBindIndexBuffer(cmd, indices[r.frame_index].buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        if (c.kind == UiCommand::Kind::ClipMask) {
            // Set: clear to 0, write 1; SetInverse: clear to 1, write 0; Intersect: add 1 where drawn. (RmlUi's GL3.)
            if (c.clip_op != UiCommand::ClipOp::Intersect) {
                VkClearAttachment clear{VK_IMAGE_ASPECT_STENCIL_BIT, 0, {}};
                clear.clearValue.depthStencil = {0.0f, c.clip_op == UiCommand::ClipOp::SetInverse ? 1u : 0u};
                const VkClearRect rect{full(), 0, 1};
                vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
            }
            vkCmdSetStencilTestEnable(cmd, VK_TRUE);
            vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP,
                              c.clip_op == UiCommand::ClipOp::Intersect ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : VK_STENCIL_OP_REPLACE,
                              VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
            vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
            vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c.clip_op == UiCommand::ClipOp::SetInverse ? 0u : 1u);
        } else {
            stencil_test(c.stencil, c.stencil_ref);
        }
        Push push{};
        if (c.transform >= 0 && static_cast<std::size_t>(c.transform) < frame->transforms.size()) {
            const auto& m = frame->transforms[static_cast<std::size_t>(c.transform)];
            std::memcpy(push.c0, &m[0], 16);
            std::memcpy(push.c1, &m[4], 16);
            std::memcpy(push.c2, &m[8], 16);
            std::memcpy(push.c3, &m[12], 16);
        } else {
            identity(push);
        }
        push.translation[0] = c.translation.x, push.translation[1] = c.translation.y;
        push.origin[0] = frame->origin.x, push.origin[1] = frame->origin.y;
        push.scale[0] = 2.0f / static_cast<float>(size.width), push.scale[1] = 2.0f / static_cast<float>(size.height);
        push.texture = c.texture.id < r.textures.size() && r.textures[c.texture.id].alive ? c.texture.id : kTextureWhite;
        push.extra = c.index;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, c.index_count, 1, c.first_index, c.vertex_offset, 0);
        r.stats.draw_calls += 1;
        return {};
    }

    // A whole-image draw into a layer (the backdrop, a composite), with the command's clip state.
    Status layer_pass(Prog prog, std::uint32_t layer_index, Blend blend, const Push& push, VkRect2D scissor, bool test, std::uint8_t ref,
                      UiImage& source) {
        end();
        barrier(source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        auto p = pipeline(prog, Target::Layer, blend);
        if (!p) return std::unexpected(p.error());
        begin_layer(layer_index, false);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *p);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        stencil_test(test, ref);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        end();
        return {};
    }

    Status upload(const void* data, std::size_t bytes, GpuBuffer& buffer, std::size_t& capacity, VkBufferUsageFlags usage, const char* what) {
        if (bytes == 0) return {};
        if (bytes > capacity) {
            if (buffer.buffer != VK_NULL_HANDLE) r.destroy_buffer(buffer);
            capacity = std::max<std::size_t>(bytes * 2, 64 * 1024);
            auto b = r.create_buffer(capacity, usage, true, false);
            if (!b) {
                capacity = 0;
                return std::unexpected(b.error());
            }
            buffer = *b;
            r.set_name(VK_OBJECT_TYPE_BUFFER, buffer.buffer, std::format("UI {} (frame slot {})", what, r.frame_index));
        }
        std::memcpy(buffer.mapped, data, bytes);
        vmaFlushAllocation(r.allocator, buffer.allocation, 0, bytes);
        return {};
    }

    Status record(VkCommandBuffer command_buffer, const UiFrame& f, VkImage target, VkImageView target_view) {
        cmd = command_buffer;
        frame = &f;
        open = -1;
        stencil_fresh = true;
        role = {0, 1, 2};
        const auto slot = r.frame_index;
        if (auto st = upload(f.vertices.data(), f.vertices.size_bytes(), vertices[slot], vertex_capacity[slot], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "vertices"); !st) return st;
        if (auto st = upload(f.indices.data(), f.indices.size_bytes(), indices[slot], index_capacity[slot], VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "indices"); !st) return st;
        std::vector<GpuGradient> grads(std::max<std::size_t>(1, f.gradients.size()));
        for (std::size_t i = 0; i < f.gradients.size(); ++i) {
            const UiGradient& g = f.gradients[i];
            GpuGradient& o = grads[i];
            o.kind = static_cast<std::uint32_t>(g.kind);
            o.stops = std::min(g.stops, UiGradient::kMaxStops);
            o.p[0] = g.p.x, o.p[1] = g.p.y, o.v[0] = g.v.x, o.v[1] = g.v.y;
            for (std::uint32_t s = 0; s < o.stops; ++s) {
                o.positions[s] = g.positions[s];
                o.colors[s][0] = g.colors[s].x, o.colors[s][1] = g.colors[s].y, o.colors[s][2] = g.colors[s].z, o.colors[s][3] = g.colors[s].w;
            }
        }
        const std::size_t had = gradient_capacity[slot];
        if (auto st = upload(grads.data(), grads.size() * sizeof(GpuGradient), gradients[slot], gradient_capacity[slot], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "gradients"); !st) return st;
        if (gradient_capacity[slot] != had) {  // a new buffer: the frame slot's set points at it
            VkDescriptorBufferInfo ci{gradients[slot].buffer, 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = sets[slot];
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &ci;
            vkUpdateDescriptorSets(r.device, 1, &write, 0, nullptr);
        }

        r.begin_label(cmd, "Game UI", {0.49f, 0.30f, 1.0f, 1.0f});  // purple
        // The screen, as the base layer's backdrop.
        if (backdrop_ok) {
            image_barrier(cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_READ_BIT);
            barrier(backdrop, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy copy{};
            copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.extent = {size.width, size.height, 1};
            vkCmdCopyImage(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, backdrop.image, backdrop.layout, 1, &copy);
            image_barrier(cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
        stack.assign(1, 0);
        begin_layer(0, true);  // transparent: the screen is laid under it at the end

        for (const UiCommand& c : f.commands) {
            switch (c.kind) {
            case UiCommand::Kind::Draw:
            case UiCommand::Kind::Gradient:
            case UiCommand::Kind::ClipMask:
                if (auto st = draw_geometry(c); !st) return st;
                break;
            case UiCommand::Kind::PushLayer: {
                end();
                const auto index = static_cast<std::uint32_t>(stack.size());
                if (index >= 8) return fail(ErrorCode::OutOfMemory, "game UI: layers nested more than 8 deep");
                while (layers.size() <= index) {
                    auto l = make_image(layer_format, samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        VK_IMAGE_ASPECT_COLOR_BIT, false, std::format("UI layer {}", layers.size()));
                    if (!l) return std::unexpected(l.error());
                    layers.push_back(*l);
                }
                stack.push_back(index);
                begin_layer(index, true);
                break;
            }
            case UiCommand::Kind::PopLayer:
                end();
                if (stack.size() > 1) stack.pop_back();
                break;
            case UiCommand::Kind::Composite: {
                if (c.index >= stack.size() || c.destination >= stack.size()) break;
                const VkRect2D window = scissor_of(c);
                if (auto st = resolve_source(c.index, window); !st) return st;
                if (auto st = filters(c, window); !st) return st;
                if (auto st = layer_pass(Prog::Pass, c.destination, c.replace ? Blend::None : Blend::Premultiplied, copy_push(primary()), window,
                                         c.stencil, c.stencil_ref, primary());
                    !st)
                    return st;
                break;
            }
            case UiCommand::Kind::SaveTexture: {
                if (c.texture.id >= r.textures.size() || !r.textures[c.texture.id].alive || r.textures[c.texture.id].external) break;
                const VkRect2D area = clamp_rect(c.rect);
                if (area.extent.width == 0 || area.extent.height == 0) break;
                resolve(stack.back(), area);
                UiImage& p = primary();
                barrier(p, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                const VkImage tex = r.textures[c.texture.id].image;
                image_barrier(cmd, tex, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              kAll, kAny, kAll, kAny);
                VkImageBlit b{};
                b.srcSubresource = b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                b.srcOffsets[0] = {area.offset.x, area.offset.y, 0};
                b.srcOffsets[1] = {area.offset.x + static_cast<std::int32_t>(area.extent.width), area.offset.y + static_cast<std::int32_t>(area.extent.height), 1};
                b.dstOffsets[1] = {std::max(1, c.rect[2]), std::max(1, c.rect[3]), 1};
                vkCmdBlitImage(cmd, p.image, p.layout, tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
                image_barrier(cmd, tex, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              kAll, kAny, kAll, kAny);
                break;
            }
            case UiCommand::Kind::SaveMask: {
                resolve(stack.back(), scissor_of(c));
                if (auto st = pass(Prog::Pass, mask, Blend::None, copy_push(primary()), scissor_of(c), {&primary()}); !st) return st;
                break;
            }
            }
        }

        // The base layer back onto the screen, over the screen's copy.
        resolve(0, full());
        UiImage& done = primary();
        barrier(done, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (backdrop_ok) barrier(backdrop, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        auto fin = pipeline(Prog::Final, Target::Screen, backdrop_ok ? Blend::None : Blend::Premultiplied);
        if (!fin) return std::unexpected(fin.error());
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = target_view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ci{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ci.renderArea = full();
        ci.layerCount = 1;
        ci.colorAttachmentCount = 1;
        ci.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &ci);
        viewport(size.width, size.height);
        const VkRect2D all = full();
        vkCmdSetScissor(cmd, 0, 1, &all);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *fin);
        bind_sets();
        Push push = copy_push(done, backdrop_ok ? 1.0f : 0.0f);
        push.extra = backdrop.slot;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        r.end_label(cmd);
        frame = nullptr;
        return {};
    }

    void destroy() {
        free_images();
        for (auto& [key, p] : pipelines) vkDestroyPipeline(r.device, p, nullptr);
        pipelines.clear();
        for (auto* list : {&vertices, &indices, &gradients})
            for (auto& b : *list)
                if (b.buffer != VK_NULL_HANDLE) r.destroy_buffer(b);
        if (layout) vkDestroyPipelineLayout(r.device, layout, nullptr);
        if (pool) vkDestroyDescriptorPool(r.device, pool, nullptr);
        if (set_layout) vkDestroyDescriptorSetLayout(r.device, set_layout, nullptr);
    }
};

Status Renderer::Impl::record_ui(VkCommandBuffer cmd, const UiFrame& frame, VkImage screen, VkImageView screen_view) {
    if (frame.commands.empty()) return {};
    // Made the first time a frame has UI: a game without any pays nothing (Principle 0).
    if (!ui) ui = new UiRenderer(*this);
    if (auto st = ui->ensure(surface_format.format, extent); !st) return st;
    return ui->record(cmd, frame, screen, screen_view);
}

void Renderer::Impl::destroy_ui() {
    if (!ui) return;
    if (device != VK_NULL_HANDLE) ui->destroy();
    delete ui;
    ui = nullptr;
}

std::uint64_t Renderer::Impl::ui_memory() const noexcept {
    if (!ui) return 0;
    std::uint64_t bytes = ui->stencil.bytes + ui->mask.bytes + ui->backdrop.bytes;
    for (const auto& l : ui->layers) bytes += l.bytes;
    for (const auto& p : ui->post) bytes += p.bytes;
    return bytes;
}

} // namespace noor::render
