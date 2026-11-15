#pragma once
// Milestone 0 renderer: Vulkan 1.3 device, swapchain and a frame loop that clears the screen.
// Vulkan types are deliberately hidden behind a pimpl (ADR-002).

#include "noor/foundation/error.h"
#include "noor/platform/presentation_target.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace noor::render {

struct RendererDesc {
    // Enable VK_LAYER_KHRONOS_validation if installed. Validation messages go to the log
    // under category "Vulkan" and are counted.
    bool enable_validation = true;
    // Linear-space RGBA clear color. The swapchain is sRGB, so the GPU encodes it on write.
    std::array<float, 4> clear_color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct GpuInfo {
    std::string name;
    std::string device_type;  // "discrete", "integrated", ...
    std::string api_version;  // "1.4.312"
    std::uint32_t vendor_id = 0;
    std::uint32_t driver_version_raw = 0;
};

enum class FrameResult : std::uint8_t {
    Presented,  // a frame was rendered and queued for display
    Skipped,    // nothing to draw into (minimized, or swapchain being recreated)
};

class Renderer {
public:
    [[nodiscard]] static Result<std::unique_ptr<Renderer>> create(const platform::PresentationTarget& target,
                                                                  const RendererDesc& desc);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] Result<FrameResult> render_frame();

    // Call when the window's pixel size changed; the swapchain is rebuilt before the next frame.
    void notify_resized() noexcept;

    void set_clear_color(const std::array<float, 4>& linear_rgba) noexcept;

    [[nodiscard]] const GpuInfo& gpu_info() const noexcept;
    [[nodiscard]] bool validation_enabled() const noexcept;
    [[nodiscard]] std::uint64_t validation_error_count() const noexcept;
    [[nodiscard]] std::uint64_t validation_warning_count() const noexcept;
    [[nodiscard]] std::uint64_t frames_presented() const noexcept;
    [[nodiscard]] std::uint64_t swapchain_rebuilds() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::render
