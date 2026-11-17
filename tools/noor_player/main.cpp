// noor_player: runs a NoorEngine game outside the editor (Milestone 4, part 6; ADR-028).
//
//   noor_player                      the game next to the executable (game.json, written by noor_cli build)
//   noor_player <project folder>     a project as it is on disk (handy while developing)
//
// Options: [--scene <Scenes/X.noorscene>] [--title <text>] [--frames N] [--report <file.json>]
//          [--screenshot <file.bmp>] [--no-audio] [--no-input] [--validation] [--log <file>]
// --no-input: the game gets no keyboard or mouse (automated tests: a click on the window by
// whoever sits at the PC must not change the result).
//
// --frames runs N frames and exits (tests); the exit code is then 2 if anything logged an error.
// It's a windowed program (no console): the log is <exe name>.log next to it unless --log says otherwise.
// --report writes what happened as JSON: frames, play time, each named entity's world position, and
// the colour of the centre pixel of the last frame, the HUD's vertex count and its text.

#include "noor/asset/json.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/game/game.h"
#include "noor/game/hud.h"
#include "noor/game/scene_renderer.h"
#include "noor/platform/input.h"
#include "noor/platform/platform.h"
#include "noor/platform/window.h"
#include "noor/render/renderer.h"
#include "noor/runtime/input.h"
#include "noor/runtime/world.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"
#include "noor/scripting/dotnet_host.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>  // GetModuleFileNameW (WIN32_LEAN_AND_MEAN comes from the build)
#endif

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

struct Options {
    std::string project;     // empty: the game next to the executable
    std::string scene;       // empty: game.json's, else the project's default
    std::string title;
    std::uint64_t frames = 0;  // 0: until the window is closed
    std::string report;
    std::string screenshot;
    std::string log_path;
    bool audio = true;
    bool input = true;
    bool validation = false;
};

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--scene" && more) o.scene = argv[++i];
        else if (a == "--title" && more) o.title = argv[++i];
        else if (a == "--frames" && more) o.frames = std::stoull(argv[++i]);
        else if (a == "--report" && more) o.report = argv[++i];
        else if (a == "--screenshot" && more) o.screenshot = argv[++i];
        else if (a == "--log" && more) o.log_path = argv[++i];
        else if (a == "--no-audio") o.audio = false;
        else if (a == "--no-input") o.input = false;
        else if (a == "--validation") o.validation = true;
        else if (!a.starts_with("--") && o.project.empty()) o.project = a;
        else return false;
    }
    return true;
}

stdfs::path executable_dir() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return stdfs::path(buffer).parent_path();
#else
    return stdfs::current_path();
#endif
}

noor::runtime::Key runtime_key(noor::platform::Key k) {
    using P = noor::platform::Key;
    using R = noor::runtime::Key;
    const auto n = static_cast<int>(k);
    if (k >= P::A && k <= P::Z) return static_cast<R>(static_cast<int>(R::A) + (n - static_cast<int>(P::A)));
    if (k >= P::Num0 && k <= P::Num9) return static_cast<R>(static_cast<int>(R::Num0) + (n - static_cast<int>(P::Num0)));
    if (k >= P::F1 && k <= P::F12) return static_cast<R>(static_cast<int>(R::F1) + (n - static_cast<int>(P::F1)));
    switch (k) {
    case P::Space: return R::Space;
    case P::Enter: return R::Enter;
    case P::Escape: return R::Escape;
    case P::Tab: return R::Tab;
    case P::Backspace: return R::Backspace;
    case P::LeftShift: case P::RightShift: return R::Shift;
    case P::LeftCtrl: case P::RightCtrl: return R::Ctrl;
    case P::LeftAlt: case P::RightAlt: return R::Alt;
    case P::Up: return R::Up;
    case P::Down: return R::Down;
    case P::Left: return R::Left;
    case P::Right: return R::Right;
    default: return R::None;
    }
}

noor::runtime::InputState game_input(const noor::platform::InputState& in, bool captured) {
    noor::runtime::InputState out;
    for (std::size_t i = 1; i < noor::platform::InputState::kKeyCount; ++i) {
        if (in.keys_down[i]) out.set(runtime_key(static_cast<noor::platform::Key>(i)));
    }
    using B = noor::platform::MouseButton;
    out.set(noor::runtime::Key::MouseLeft, in.down(B::Left));
    out.set(noor::runtime::Key::MouseRight, in.down(B::Right));
    out.set(noor::runtime::Key::MouseMiddle, in.down(B::Middle));
    out.mouse = {in.mouse_x, in.mouse_y};
    out.mouse_delta = {in.mouse_dx, in.mouse_dy};
    out.wheel = in.wheel;
    out.mouse_captured = captured;
    return out;
}

bool save_bmp(const noor::render::CapturedFrame& frame, const std::string& path) {
    const std::uint32_t row_bytes = (frame.width * 3 + 3) & ~3u;
    std::vector<std::uint8_t> file(54 + static_cast<std::size_t>(row_bytes) * frame.height, 0);
    const auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) file[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, static_cast<std::uint32_t>(file.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, frame.width);
    put32(22, frame.height);
    file[26] = 1;
    file[28] = 24;
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

bool has_csharp_scripts(const noor::asset::Project& project) {
    std::error_code ec;
    for (stdfs::recursive_directory_iterator it(project.root() / "Scripts", ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (it->is_directory() && (name == "bin" || name == "obj")) it.disable_recursion_pending();
        else if (it->path().extension() == ".cs") return true;
    }
    return false;
}

// Where the game is and what to start.
struct Game {
    stdfs::path project;
    std::string scene;
    std::string title;
    stdfs::path csharp;  // a built game's compiled C# scripts (managed/Scripts.dll), if any
};

noor::Result<Game> locate(const Options& o) {
    Game g;
    if (!o.project.empty()) {
        g.project = o.project;
    } else {
        // A built game: game.json next to the executable says where the project is.
        const stdfs::path manifest = executable_dir() / "game.json";
        auto text = noor::fs::read_file(manifest);
        if (!text)
            return std::unexpected(noor::Error{noor::ErrorCode::NotFound,
                                               "No game here: run noor_player <project folder>, or a game made with noor_cli build"});
        auto json = noor::asset::parse_json(*text, manifest.generic_string());
        if (!json) return std::unexpected(json.error());
        g.project = executable_dir() / json->value("project", std::string("game"));
        g.scene = json->value("start_scene", std::string{});
        g.title = json->value("title", std::string{});
        if (const auto cs = json->value("csharp", std::string{}); !cs.empty()) g.csharp = executable_dir() / cs;
    }
    if (!o.scene.empty()) g.scene = o.scene;
    if (!o.title.empty()) g.title = o.title;
    return g;
}

int run(const Options& o) {
    auto game = locate(o);
    if (!game) {
        NOOR_LOG_FATAL("Player", "{}", game.error().describe());
        return 1;
    }
    auto project = noor::asset::Project::open(game->project);
    if (!project) {
        NOOR_LOG_FATAL("Player", "{}", project.error().describe());
        return 1;
    }
    noor::asset::AssetRegistry registry;
    if (auto scan = registry.scan(*project); !scan) {
        NOOR_LOG_FATAL("Player", "{}", scan.error().describe());
        return 1;
    }
    if (game->scene.empty()) {
        auto found = project->default_start_scene();
        if (!found) {
            NOOR_LOG_FATAL("Player", "'{}' has no scenes", project->info().name);
            return 1;
        }
        game->scene = *found;
    }
    auto scene_path = project->resolve(game->scene);
    auto scene = scene_path ? noor::scene::Scene::load(*scene_path) : noor::Result<noor::scene::Scene>(std::unexpected(scene_path.error()));
    if (!scene) {
        NOOR_LOG_FATAL("Player", "{}", scene.error().describe());
        return 1;
    }
    if (game->title.empty()) game->title = project->info().name;
    NOOR_LOG_INFO("Player", "{}: {}", game->title, game->scene);

    if (auto status = noor::platform::initialize(); !status) {
        NOOR_LOG_FATAL("Player", "{}", status.error().describe());
        return 1;
    }
    int exit_code = 0;
    {
        auto window = noor::platform::Window::create({.title = game->title, .width = 1280, .height = 720});
        if (!window) {
            NOOR_LOG_FATAL("Player", "{}", window.error().describe());
            noor::platform::shutdown();
            return 1;
        }
        auto renderer = noor::render::Renderer::create(**window, {.enable_validation = o.validation, .clear_color = {0.05f, 0.045f, 0.07f, 1.0f}});
        if (!renderer) {
            NOOR_LOG_FATAL("Player", "{}", renderer.error().describe());
            window->reset();
            noor::platform::shutdown();
            return 1;
        }
        {
            noor::scene::PrefabLibrary prefabs(*project, registry);
            noor::game::WorldOptions world_options;
            world_options.audio_device = o.audio;
            if (!game->csharp.empty()) {
                // The game's C# scripts: start .NET (the runtime installed on this PC) with them.
                if (auto host = noor::scripting::start_dotnet(game->csharp.parent_path(), game->csharp))
                    world_options.script_host = *host;
                else
                    NOOR_LOG_ERROR("Player", "C# scripts won't run: {}", host.error().describe());
            } else if (has_csharp_scripts(*project)) {
                NOOR_LOG_WARN("Player", "This project has C# scripts: they run in NoorEditor and in built games "
                                        "(noor_cli build), not with noor_player <project>");
            }
            auto world = noor::game::make_world(*project, registry, prefabs, *scene, world_options);
            if (!world) {
                NOOR_LOG_FATAL("Player", "{}", world.error().describe());
                exit_code = 1;
            } else {
                noor::game::SceneRenderer scene_renderer(**renderer, project->root() / ".noor" / "cache" / "textures");
                const noor::game::Hud hud;  // the HUD scripts draw (noor.ui / Ui), with the built-in font
                if (auto s = (*renderer)->set_overlay_atlas(hud.atlas_width(), hud.atlas_height(), hud.atlas()); !s)
                    NOOR_LOG_ERROR("Player", "HUD font: {}", s.error().describe());
                std::vector<noor::render::OverlayVertex> overlay;
                const auto asset_path = noor::game::registry_asset_path(*project, registry);
                bool captured = false;
                int ignore_motion = 0;  // frames of mouse movement to drop after capture changes
                bool warned_no_camera = false;
                std::optional<noor::render::CapturedFrame> last_capture;
                double last = noor::platform::time_seconds();
                while (o.frames == 0 || (*world)->frame_count() < o.frames) {
                    const auto events = noor::platform::pump_events();
                    if (events.quit_requested) break;
                    if (events.window_resized || events.window_restored) (*renderer)->notify_resized();
                    const auto& in = noor::platform::input();

                    // The mouse: scripts ask for it (noor.input.capture_mouse); Esc and switching to
                    // another window always give it back.
                    if (captured && (in.pressed(noor::platform::Key::Escape) || events.focus_lost)) {
                        noor::platform::set_relative_mouse(**window, false);
                        captured = false;
                        ignore_motion = 2;
                    }
                    if (const auto want = (*world)->take_mouse_capture_request(); want && *want != captured) {
                        noor::platform::set_relative_mouse(**window, *want);
                        captured = *want;
                        // Entering relative mode moves the hidden cursor to the window's centre, and that
                        // jump arrives as movement: without this the view lurches on the capturing click.
                        ignore_motion = 2;
                    }

                    const double now = noor::platform::time_seconds();
                    // Fixed steps in test runs (--frames) so results don't depend on how busy the machine is.
                    const double dt = o.frames ? 1.0 / 60.0 : now - last;
                    last = now;
                    auto game_in = o.input ? game_input(in, captured) : noor::runtime::InputState{};
                    if (ignore_motion > 0) {
                        --ignore_motion;
                        game_in.mouse_delta = {0, 0};
                    }
                    (*world)->frame(dt, game_in);

                    const auto view = noor::game::collect_view((*world)->scene(), asset_path, &(*world)->render_poses());
                    if (!view.camera && !warned_no_camera) {
                        NOOR_LOG_WARN("Player", "The scene has no camera; showing it from above. Add a camera component (or Create ▸ Player) to see the game's view");
                        warned_no_camera = true;
                    }
                    const auto draws = scene_renderer.draws(view);
                    const bool last_frame = o.frames && (*world)->frame_count() == o.frames;
                    if (last_frame && (!o.report.empty() || !o.screenshot.empty())) (*renderer)->request_capture();
                    overlay.clear();
                    const auto pixels = (*renderer)->swapchain_size();
                    hud.build((*world)->ui_commands(), static_cast<float>(pixels.width), static_cast<float>(pixels.height), overlay);
                    auto frame = (*renderer)->render_frame({noor::game::SceneRenderer::camera(view), view.lighting, draws, overlay});
                    if (!frame) {
                        NOOR_LOG_ERROR("Player", "Frame failed: {}", frame.error().describe());
                        exit_code = 1;
                        break;
                    }
                    if (*frame == noor::render::FrameResult::Skipped && (*window)->is_minimized()) noor::platform::wait_for_events(50);
                    if (auto cap = (*renderer)->take_capture(); cap && cap->has_value()) last_capture = std::move(**cap);
                }
                if (captured) noor::platform::set_relative_mouse(**window, false);
                NOOR_LOG_INFO("Player", "Ran {} frames ({:.1f} s of game time)", (*world)->frame_count(), (*world)->time());

                if (!o.screenshot.empty()) {
                    const bool ok = last_capture && save_bmp(*last_capture, o.screenshot);
                    if (!ok) NOOR_LOG_ERROR("Player", "Screenshot failed: {}", o.screenshot);
                }
                if (!o.report.empty()) {
                    Json report{{"frames", (*world)->frame_count()}, {"time", (*world)->time()}, {"scene", game->scene}};
                    Json entities = Json::object();
                    const auto& s = (*world)->scene();
                    for (std::size_t i = 0; i < s.entities.size(); ++i) {
                        const auto p = s.world_matrix(i).transform_point({0, 0, 0});
                        entities[s.entities[i].name] = {p.x, p.y, p.z};
                    }
                    report["entities"] = std::move(entities);
                    if (last_capture) {
                        const auto c = last_capture->rgba_at(last_capture->width / 2, last_capture->height / 2);
                        report["center_pixel"] = {c[0], c[1], c[2]};
                        // The pixel at UI position (40, 40) (the screen is 720 UI units tall), for HUD tests.
                        const auto ui = static_cast<std::uint32_t>(40.0f * static_cast<float>(last_capture->height) / noor::runtime::kUiHeight);
                        const auto p = last_capture->rgba_at(std::min(ui, last_capture->width - 1), std::min(ui, last_capture->height - 1));
                        report["ui_pixel_40_40"] = {p[0], p[1], p[2]};
                    }
                    report["hud_vertices"] = overlay.size();
                    // The last frame's HUD text, so a test can read what the game shows (a score, a timer).
                    Json hud_text = Json::array();
                    for (const auto& c : (*world)->ui_commands())
                        if (c.kind == noor::runtime::UiCommand::Kind::Text) hud_text.push_back(c.text);
                    report["hud_text"] = std::move(hud_text);
                    Json passes = Json::object();  // GPU milliseconds per pass of a recent frame
                    for (const auto& pass : (*renderer)->last_frame_stats().passes) passes[pass.name] = pass.ms;
                    report["gpu_passes"] = std::move(passes);
                    report["errors"] = noor::log::count(noor::LogLevel::Error) + noor::log::count(noor::LogLevel::Fatal);
                    if (auto w = noor::fs::write_file_atomic(o.report, report.dump(2)); !w) NOOR_LOG_ERROR("Player", "{}", w.error().describe());
                }
            }
        }  // the world (and its scripts, physics and sound) goes before the renderer and window
        renderer->reset();
        window->reset();
    }
    noor::platform::shutdown();
    if (exit_code == 0 && o.frames && noor::log::count(noor::LogLevel::Error) + noor::log::count(noor::LogLevel::Fatal) > 0) exit_code = 2;
    return exit_code;
}

} // namespace

int main(int argc, char** argv) {
    noor::log::add_sink(std::make_shared<noor::ConsoleLogSink>());
    Options options;
    if (!parse(argc, argv, options)) {
        NOOR_LOG_ERROR("Player", "Usage: noor_player [project folder] [--scene <path>] [--title <text>] [--frames N] "
                                 "[--report <file.json>] [--screenshot <file.bmp>] [--no-audio] [--no-input] [--validation] [--log <file>]");
        return 64;
    }
    // There's no console (a windowed program): the log goes to a file, by default <exe name>.log
    // next to the executable. A game folder that can't be written to just has no log.
    {
        stdfs::path log_path = options.log_path;
#if defined(_WIN32)
        if (log_path.empty()) {
            std::wstring buffer(MAX_PATH, L'\0');
            const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            log_path = stdfs::path(buffer.substr(0, n)).replace_extension(".log");
        }
#endif
        if (!log_path.empty()) {
            auto sink = std::make_shared<noor::FileLogSink>(log_path.string());
            if (sink->is_open()) noor::log::add_sink(sink);
        }
    }
    const int code = run(options);
    noor::log::flush();
    return code;
}
