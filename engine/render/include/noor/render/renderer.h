#pragma once
// NoorEngine renderer (Milestone 1): Vulkan 1.3 device, swapchain, depth buffer, GPU meshes,
// one lit mesh pipeline, a camera, and frame capture for automated tests.
// Vulkan types are hidden behind a pimpl (ADR-002). Conventions: ADR-022.

#include "noor/foundation/error.h"
#include "noor/foundation/math.h"
#include "noor/platform/presentation_target.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

// ---- Meshes -----------------------------------------------------------------------------

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec3 color;  // linear RGB
};

struct MeshData {
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;  // triangle list, counter-clockwise front faces
    std::string_view debug_name;             // shown in RenderDoc/Nsight; optional
};

// Opaque handle to a mesh living in GPU memory. 0 is never a valid handle.
struct MeshHandle {
    std::uint32_t id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

// ---- Frames -----------------------------------------------------------------------------

struct Camera {
    math::Mat4 view = math::Mat4::identity();  // world -> view (see math::look_at)
    float fov_y = math::radians(60.0f);
    float near_plane = 0.1f;                   // reverse-Z infinite far plane
};

struct DrawItem {
    MeshHandle mesh;
    math::Mat4 world_from_object = math::Mat4::identity();
    // rgb = linear color, a = how much of it replaces the mesh's own colors (0 = none).
    math::Vec4 tint{0, 0, 0, 0};
    // Overlay items are drawn after everything else, in front of it (editor gizmos). They
    // still depth-test against each other.
    bool overlay = false;
};

struct FrameDesc {
    Camera camera;
    std::span<const DrawItem> draws;
};

struct FrameStats {
    std::uint32_t draw_calls = 0;
    std::uint64_t triangles = 0;
    // CPU time spent recording and submitting the frame (excludes waiting for the GPU/vsync).
    double cpu_record_ms = 0.0;
    // GPU time of the most recent frame whose results are available (lags 1–2 frames).
    // Negative when timestamps are unsupported or not available yet.
    double gpu_ms = -1.0;
};

// A copy of a presented frame, read back from the GPU. Pixels are tightly packed
// 4 bytes each, in `format` byte order ("BGRA8_SRGB" or "RGBA8_SRGB").
struct CapturedFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string format;
    std::vector<std::uint8_t> pixels;

    // Returns {r, g, b, a} in sRGB bytes regardless of the stored byte order.
    [[nodiscard]] std::array<std::uint8_t, 4> rgba_at(std::uint32_t x, std::uint32_t y) const;
};

class Renderer {
public:
    [[nodiscard]] static Result<std::unique_ptr<Renderer>> create(const platform::PresentationTarget& target,
                                                                  const RendererDesc& desc);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Uploads a mesh to GPU memory. Blocks until the upload finishes (fine for Milestone 1;
    // asynchronous streaming comes with the asset system).
    [[nodiscard]] Result<MeshHandle> create_mesh(const MeshData& data);
    void destroy_mesh(MeshHandle mesh);

    // Clears to the clear color and draws `frame.draws` from `frame.camera`.
    [[nodiscard]] Result<FrameResult> render_frame(const FrameDesc& frame);
    // Clears only.
    [[nodiscard]] Result<FrameResult> render_frame() { return render_frame(FrameDesc{}); }

    // Asks the renderer to copy the next presented frame to CPU memory.
    void request_capture() noexcept;
    // Returns the captured frame once it is available (waits for the GPU if needed),
    // or nothing if no capture was requested/completed yet.
    [[nodiscard]] Result<std::optional<CapturedFrame>> take_capture();

    // Call when the window's pixel size changed; the swapchain is rebuilt before the next frame.
    void notify_resized() noexcept;

    void set_clear_color(const std::array<float, 4>& linear_rgba) noexcept;

    [[nodiscard]] const GpuInfo& gpu_info() const noexcept;
    [[nodiscard]] const FrameStats& last_frame_stats() const noexcept;
    [[nodiscard]] bool validation_enabled() const noexcept;
    [[nodiscard]] std::uint64_t validation_error_count() const noexcept;
    [[nodiscard]] std::uint64_t validation_warning_count() const noexcept;
    [[nodiscard]] std::uint64_t frames_presented() const noexcept;
    [[nodiscard]] std::uint64_t swapchain_rebuilds() const noexcept;
    // Size of the images currently being presented (zero before the first swapchain).
    [[nodiscard]] platform::PixelSize swapchain_size() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::render
