// noor_diag: NoorEngine diagnostics tool.
//
// Opens a window, initializes Vulkan, reports the GPU, and clears the screen in
// Noor purple. Exits non-zero on any failure or Vulkan validation error, which makes
// it the GPU smoke test. Stays as a permanent developer utility.
//
//   noor_diag [--frames N] [--no-validation] [--log <file>]

#include "noor/foundation/log.h"
#include "noor/platform/platform.h"
#include "noor/platform/window.h"
#include "noor/render/renderer.h"

#include <array>
#include <charconv>
#include <format>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::uint64_t max_frames = 0;  // 0 = run until the window is closed
    bool validation = true;
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
        } else if (arg == "--log" && i + 1 < argc) {
            out.log_path = argv[++i];
        } else {
            return false;
        }
    }
    return true;
}

float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Noor purple, sRGB #6B3FD9.
std::array<float, 4> noor_purple_linear() {
    return {srgb_to_linear(0x6B / 255.0f), srgb_to_linear(0x3F / 255.0f), srgb_to_linear(0xD9 / 255.0f), 1.0f};
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

        auto renderer = noor::render::Renderer::create(
            **window, {.enable_validation = options.validation, .clear_color = noor_purple_linear()});
        if (!renderer) {
            NOOR_LOG_FATAL("Diag", "{}", renderer.error().describe());
            window->reset();
            noor::platform::shutdown();
            return 1;
        }
        const auto& gpu = (*renderer)->gpu_info();
        NOOR_LOG_INFO("Diag", "GPU: {} | type {} | Vulkan {} | vendor 0x{:04X}", gpu.name, gpu.device_type,
                      gpu.api_version, gpu.vendor_id);

        const double start = noor::platform::time_seconds();
        double last_report = start;
        std::uint64_t frames_at_last_report = 0;

        for (;;) {
            const auto events = noor::platform::pump_events();
            if (events.quit_requested) {
                NOOR_LOG_INFO("Diag", "Window closed by user");
                break;
            }
            if (events.window_resized || events.window_restored) {
                (*renderer)->notify_resized();
            }

            auto frame = (*renderer)->render_frame();
            if (!frame) {
                NOOR_LOG_ERROR("Diag", "Frame failed: {}", frame.error().describe());
                exit_code = 1;
                break;
            }
            if (*frame == noor::render::FrameResult::Skipped && (*window)->is_minimized()) {
                noor::platform::wait_for_events(100);
                continue;
            }

            const std::uint64_t presented = (*renderer)->frames_presented();
            const double now = noor::platform::time_seconds();
            if (now - last_report >= 1.0) {
                const double fps = static_cast<double>(presented - frames_at_last_report) / (now - last_report);
                (*window)->set_title(std::format("NoorEngine Diagnostics | {} | {:.0f} FPS (vsync)", gpu.name, fps));
                last_report = now;
                frames_at_last_report = presented;
            }
            if (options.max_frames > 0 && presented >= options.max_frames) {
                break;
            }
        }

        const double elapsed = noor::platform::time_seconds() - start;
        const std::uint64_t presented = (*renderer)->frames_presented();
        NOOR_LOG_INFO("Diag", "Presented {} frames in {:.2f}s (avg {:.2f} ms/frame, vsync-limited), {} swapchain build(s)",
                      presented, elapsed, presented ? elapsed * 1000.0 / static_cast<double>(presented) : 0.0,
                      (*renderer)->swapchain_rebuilds());

        const bool validated = (*renderer)->validation_enabled();
        const std::uint64_t vk_warnings = (*renderer)->validation_warning_count();
        renderer->reset();  // destroy GPU objects before the window
        window->reset();

        // Validation errors are logged at Error level, so counting log errors after teardown
        // also catches problems reported while destroying Vulkan objects.
        const std::uint64_t log_errors = noor::log::count(noor::LogLevel::Error) + noor::log::count(noor::LogLevel::Fatal);
        NOOR_LOG_INFO("Diag", "Validation: {} | warnings {} | errors logged {}", validated ? "on" : "OFF", vk_warnings,
                      log_errors);
        if (log_errors > 0) {
            exit_code = exit_code ? exit_code : 2;
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
        NOOR_LOG_ERROR("Diag", "Usage: noor_diag [--frames N] [--no-validation] [--log <file>]");
        return 64;
    }

    auto file_sink = std::make_shared<noor::FileLogSink>(options.log_path);
    if (file_sink->is_open()) {
        noor::log::add_sink(file_sink);
    } else {
        NOOR_LOG_WARN("Diag", "Could not open log file '{}'", options.log_path);
    }

    NOOR_LOG_INFO("Diag", "NoorEngine diagnostics starting (frames: {}, validation requested: {})",
                  options.max_frames ? std::to_string(options.max_frames) : "until closed",
                  options.validation ? "yes" : "no");
    const int code = run(options);
    noor::log::flush();
    return code;
}
