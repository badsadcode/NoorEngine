// noor_diag: NoorEngine diagnostics tool.
//
// Opens a window, initializes Vulkan, reports the GPU and draws a lit, spinning cube on
// Noor purple. Exits non-zero on any failure or Vulkan validation error, which makes it
// the GPU smoke test. Stays as a permanent developer utility.
//
//   noor_diag [--frames N] [--no-validation] [--verify-render] [--screenshot <file.bmp>] [--log <file>]
//
// --verify-render holds the cube still, reads a frame back from the GPU and checks real
// pixels: background, the cube's front face and its top face must have the expected colors.
// That catches broken projection (Y flip), culling/winding, depth and color-space bugs.

#include "noor/foundation/log.h"
#include "noor/foundation/math.h"
#include "noor/platform/platform.h"
#include "noor/platform/window.h"
#include "noor/render/renderer.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using noor::math::Vec3;

struct Options {
    std::uint64_t max_frames = 0;  // 0 = run until the window is closed
    bool validation = true;
    bool verify_render = false;
    std::string screenshot_path;  // non-empty: save frame 90 as a 24-bit BMP
    std::string log_path = "noor_diag.log";
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            if (std::from_chars(value.data(), value.data() + value.size(), out.max_frames).ec != std::errc{}) {
                return false;
            }
        } else if (arg == "--no-validation") {
            out.validation = false;
        } else if (arg == "--verify-render") {
            out.verify_render = true;
        } else if (arg == "--log" && i + 1 < argc) {
            out.log_path = argv[++i];
        } else if (arg == "--screenshot" && i + 1 < argc) {
            out.screenshot_path = argv[++i];
        } else {
            return false;
        }
    }
    return true;
}

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

Vec3 srgb_hex(std::uint32_t rgb) {
    return {srgb_to_linear(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f),
            srgb_to_linear(static_cast<float>((rgb >> 8) & 0xFF) / 255.0f),
            srgb_to_linear(static_cast<float>(rgb & 0xFF) / 255.0f)};
}

constexpr std::uint32_t kNoorPurple = 0x6B3FD9;

// A 2 m cube with one color per face, counter-clockwise front faces (ADR-022).
struct CubeMesh {
    std::vector<noor::render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

CubeMesh make_cube() {
    struct Face {
        Vec3 normal, u, v;
        std::uint32_t color;
    };
    // u × v = normal, so (−u−v, +u−v, +u+v, −u+v) winds counter-clockwise seen from outside.
    const Face faces[] = {
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, 0x22D3EE},   // +Y top: cyan
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, 0xE5484D},   // −Y bottom: red
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 0x7C4DFF},    // +Z front: purple
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, 0xF59E0B},  // −Z back: orange
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, 0xEC4899},   // +X right: pink
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 0xFACC15},   // −X left: yellow
    };
    CubeMesh mesh;
    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const Vec3 color = srgb_hex(f.color);
        const Vec3 corners[4] = {f.normal - f.u - f.v, f.normal + f.u - f.v, f.normal + f.u + f.v,
                                 f.normal - f.u + f.v};
        for (const Vec3& p : corners) {
            mesh.vertices.push_back({p, f.normal, color});
        }
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
            mesh.indices.push_back(base + i);
        }
    }
    return mesh;
}

// Where a world-space point lands on screen, in pixels (Vulkan: y grows downward).
std::array<std::uint32_t, 2> project_to_pixel(const noor::math::Mat4& clip_from_world, Vec3 p, std::uint32_t w,
                                              std::uint32_t h) {
    const Vec3 ndc = clip_from_world.transform_point(p);
    return {static_cast<std::uint32_t>((ndc.x * 0.5f + 0.5f) * static_cast<float>(w)),
            static_cast<std::uint32_t>((ndc.y * 0.5f + 0.5f) * static_cast<float>(h))};
}

// Writes a captured frame as an uncompressed 24-bit BMP (no image library needed).
bool save_bmp(const noor::render::CapturedFrame& frame, const std::string& path) {
    const std::uint32_t row_bytes = (frame.width * 3 + 3) & ~3u;
    const std::uint32_t data_size = row_bytes * frame.height;
    std::vector<std::uint8_t> file(54 + data_size, 0);
    const auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) file[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, static_cast<std::uint32_t>(file.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, frame.width);
    put32(22, frame.height);  // positive height: rows stored bottom-up
    file[26] = 1;
    file[28] = 24;
    put32(34, data_size);
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        std::uint8_t* row = &file[54 + static_cast<std::size_t>(frame.height - 1 - y) * row_bytes];
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto c = frame.rgba_at(x, y);
            row[x * 3 + 0] = c[2];
            row[x * 3 + 1] = c[1];
            row[x * 3 + 2] = c[0];
        }
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    std::fclose(f);
    return ok;
}

// Checks the captured frame. Returns true if everything looks right.
bool verify_frame(const noor::render::CapturedFrame& frame, const noor::render::Camera& camera) {
    using noor::math::perspective_reverse_z;
    const float aspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
    const auto clip_from_world = perspective_reverse_z(camera.fov_y, aspect, camera.near_plane) * camera.view;

    struct Probe {
        const char* name;
        std::array<std::uint32_t, 2> px;
        bool (*ok)(const std::array<std::uint8_t, 4>&);
        const char* expectation;
    };
    const Probe probes[] = {
        {"background (corner)", {4, 4},
         [](const std::array<std::uint8_t, 4>& c) {
             return std::abs(c[0] - 0x6B) <= 2 && std::abs(c[1] - 0x3F) <= 2 && std::abs(c[2] - 0xD9) <= 2;
         },
         "exactly Noor purple #6B3FD9 (sRGB encode is correct)"},
        {"front face (+Z)", project_to_pixel(clip_from_world, {0, 0, 1}, frame.width, frame.height),
         [](const std::array<std::uint8_t, 4>& c) { return c[2] > c[1] + 40 && c[0] > c[1] + 20; },
         "purple-ish (blue > green, red > green)"},
        {"top face (+Y)", project_to_pixel(clip_from_world, {0, 1, 0.5f}, frame.width, frame.height),
         [](const std::array<std::uint8_t, 4>& c) { return c[1] > c[0] + 40 && c[2] > c[0] + 40; },
         "cyan-ish (green and blue > red): proves Y-flip, culling and depth"},
    };

    bool all_ok = true;
    for (const Probe& p : probes) {
        const auto c = frame.rgba_at(p.px[0], p.px[1]);
        const bool ok = p.ok(c);
        all_ok = all_ok && ok;
        NOOR_LOG_(Info, "Verify", "{}: {} at pixel ({}, {}) = #{:02X}{:02X}{:02X}, expected {}", ok ? "PASS" : "FAIL",
                  p.name, p.px[0], p.px[1], c[0], c[1], c[2], p.expectation);
        if (!ok) {
            NOOR_LOG_ERROR("Verify", "Render verification failed: {}", p.name);
        }
    }
    return all_ok;
}

int run(const Options& options) {
    if (auto status = noor::platform::initialize(); !status) {
        NOOR_LOG_FATAL("Diag", "{}", status.error().describe());
        return 1;
    }

    int exit_code = 0;
    {
        auto window = noor::platform::Window::create({.title = "NoorEngine Diagnostics", .width = 1280, .height = 720});
        if (!window) {
            NOOR_LOG_FATAL("Diag", "{}", window.error().describe());
            noor::platform::shutdown();
            return 1;
        }

        const Vec3 purple = srgb_hex(kNoorPurple);
        auto renderer = noor::render::Renderer::create(
            **window, {.enable_validation = options.validation, .clear_color = {purple.x, purple.y, purple.z, 1.0f}});
        if (!renderer) {
            NOOR_LOG_FATAL("Diag", "{}", renderer.error().describe());
            window->reset();
            noor::platform::shutdown();
            return 1;
        }
        const auto& gpu = (*renderer)->gpu_info();
        NOOR_LOG_INFO("Diag", "GPU: {} | type {} | Vulkan {} | vendor 0x{:04X}", gpu.name, gpu.device_type,
                      gpu.api_version, gpu.vendor_id);

        const CubeMesh cube_data = make_cube();
        auto cube = (*renderer)->create_mesh({cube_data.vertices, cube_data.indices});
        if (!cube) {
            NOOR_LOG_FATAL("Diag", "{}", cube.error().describe());
            exit_code = 1;
        }

        noor::render::Camera camera;
        camera.view = noor::math::look_at({0.0f, 1.8f, 5.0f}, {0, 0, 0}, {0, 1, 0});
        camera.fov_y = noor::math::radians(50.0f);

        constexpr std::uint64_t kVerifyFrame = 30;
        constexpr std::uint64_t kScreenshotFrame = 90;
        bool verified = !options.verify_render;
        bool screenshot_taken = options.screenshot_path.empty() || options.verify_render;
        const double start = noor::platform::time_seconds();
        double last_report = start;
        std::uint64_t frames_at_last_report = 0;

        while (exit_code == 0) {
            const auto events = noor::platform::pump_events();
            if (events.quit_requested) {
                NOOR_LOG_INFO("Diag", "Window closed by user");
                break;
            }
            if (events.window_resized || events.window_restored) {
                (*renderer)->notify_resized();
            }

            const double now = noor::platform::time_seconds();
            // In verify mode the cube holds still, so the pixel probes are deterministic.
            const float angle = options.verify_render ? 0.0f : static_cast<float>(now - start) * 0.8f;
            const noor::render::DrawItem draws[] = {{*cube, noor::math::rotation({0.2f, 1.0f, 0.1f}, angle)}};

            const std::uint64_t presented = (*renderer)->frames_presented();
            if ((options.verify_render && presented == kVerifyFrame) ||
                (!screenshot_taken && presented == kScreenshotFrame)) {
                (*renderer)->request_capture();
            }

            auto frame = (*renderer)->render_frame({camera, draws});
            if (!frame) {
                NOOR_LOG_ERROR("Diag", "Frame failed: {}", frame.error().describe());
                exit_code = 1;
                break;
            }
            if (*frame == noor::render::FrameResult::Skipped && (*window)->is_minimized()) {
                noor::platform::wait_for_events(100);
                continue;
            }

            if (!screenshot_taken) {
                auto captured = (*renderer)->take_capture();
                if (captured && captured->has_value()) {
                    screenshot_taken = true;
                    const bool saved = save_bmp(**captured, options.screenshot_path);
                    NOOR_LOG_(Info, "Diag", "Screenshot {} {}", saved ? "saved to" : "FAILED for", options.screenshot_path);
                }
            }
            if (!verified) {
                auto captured = (*renderer)->take_capture();
                if (!captured) {
                    NOOR_LOG_ERROR("Diag", "Capture failed: {}", captured.error().describe());
                    exit_code = 1;
                    break;
                }
                if (captured->has_value()) {
                    verified = true;
                    if (!verify_frame(**captured, camera)) {
                        exit_code = 3;
                    }
                }
            }

            if (now - last_report >= 1.0) {
                const std::uint64_t done = (*renderer)->frames_presented();
                const double fps = static_cast<double>(done - frames_at_last_report) / (now - last_report);
                const auto& stats = (*renderer)->last_frame_stats();
                (*window)->set_title(std::format("NoorEngine Diagnostics | {} | {:.0f} FPS (vsync) | {} draw, {} tris",
                                                 gpu.name, fps, stats.draw_calls, stats.triangles));
                last_report = now;
                frames_at_last_report = done;
            }
            if (options.max_frames > 0 && (*renderer)->frames_presented() >= options.max_frames) {
                break;
            }
        }

        if (options.verify_render && !verified && exit_code == 0) {
            NOOR_LOG_ERROR("Diag", "--verify-render: no frame was captured (ran fewer than {} frames?)", kVerifyFrame + 1);
            exit_code = 3;
        }

        const double elapsed = noor::platform::time_seconds() - start;
        const std::uint64_t presented = (*renderer)->frames_presented();
        NOOR_LOG_INFO("Diag", "Presented {} frames in {:.2f}s (avg {:.2f} ms/frame, vsync-limited), {} swapchain build(s)",
                      presented, elapsed, presented ? elapsed * 1000.0 / static_cast<double>(presented) : 0.0,
                      (*renderer)->swapchain_rebuilds());

        const bool validated = (*renderer)->validation_enabled();
        const std::uint64_t vk_warnings = (*renderer)->validation_warning_count();
        renderer->reset();  // destroy GPU objects (including meshes) before the window
        window->reset();

        // Validation errors are logged at Error level, so counting log errors after teardown
        // also catches problems reported while destroying Vulkan objects.
        const std::uint64_t log_errors = noor::log::count(noor::LogLevel::Error) + noor::log::count(noor::LogLevel::Fatal);
        NOOR_LOG_INFO("Diag", "Validation: {} | warnings {} | errors logged {}", validated ? "on" : "OFF", vk_warnings,
                      log_errors);
        if (log_errors > 0 && exit_code == 0) {
            exit_code = 2;
        }
    }

    noor::platform::shutdown();
    NOOR_LOG_INFO("Diag", "Exit code {}", exit_code);
    return exit_code;
}

} // namespace

int main(int argc, char** argv) {
    noor::log::add_sink(std::make_shared<noor::ConsoleLogSink>());

    Options options;
    if (!parse_options(argc, argv, options)) {
        NOOR_LOG_ERROR("Diag", "Usage: noor_diag [--frames N] [--no-validation] [--verify-render] [--log <file>]");
        return 64;
    }

    auto file_sink = std::make_shared<noor::FileLogSink>(options.log_path);
    if (file_sink->is_open()) {
        noor::log::add_sink(file_sink);
    } else {
        NOOR_LOG_WARN("Diag", "Could not open log file '{}'", options.log_path);
    }

    NOOR_LOG_INFO("Diag", "NoorEngine diagnostics starting (frames: {}, validation requested: {}, verify render: {})",
                  options.max_frames ? std::to_string(options.max_frames) : "until closed",
                  options.validation ? "yes" : "no", options.verify_render ? "yes" : "no");
    const int code = run(options);
    noor::log::flush();
    return code;
}
