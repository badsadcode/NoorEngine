// noor_player: runs a NoorEngine game outside the editor (Milestone 4, part 6; ADR-028).
//
//   noor_player                      the game next to the executable (game.json, written by noor_cli build)
//   noor_player <project folder>     a project as it is on disk (handy while developing)
//
// Options: [--scene <Scenes/X.noorscene>] [--title <text>] [--frames N] [--report <file.json>]
//          [--screenshot <file.bmp>] [--no-audio] [--no-input] [--validation] [--log <file>] [--msaa 1|2|4|8]
//          [--cpu-sky] [--trace-environment]
// --cpu-sky: build an atmosphere sky on the CPU (the reference implementation) instead of the GPU, to
// compare the two (tests); a moving sun then costs about 120 ms a move.
// --trace-environment: the report gets "environment_trace", one entry a frame: the camera's position, the fog density,
// the exposure and each weather zone's weight (tests of blending, Milestone 8A part 7).
// --no-input: the game gets no keyboard or mouse (automated tests: a click on the window by
// whoever sits at the PC must not change the result).
//
// --frames runs N frames and exits (tests); the exit code is then 2 if anything logged an error.
// It's a windowed program (no console): the log is <exe name>.log next to it unless --log says otherwise.
// --report writes what happened as JSON: frames, play time, each named entity's world position, and
// the colour of the centre pixel of the last frame, the HUD's vertex count and its text, the game UI's
// documents and draws, and the renderer's numbers (GPU time per pass, draws, memory, MSAA samples).

#include "noor/asset/json.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"
#include "noor/fx/particle_system.h"
#include "noor/game/animator.h"
#include "noor/game/game.h"
#include "noor/game/hud.h"
#include "noor/game/display.h"
#include "noor/game/scene_renderer.h"
#include "noor/game/water.h"
#include "noor/platform/input.h"
#include "noor/platform/platform.h"
#include "noor/platform/process.h"
#include "noor/platform/window.h"
#include "noor/render/renderer.h"
#include "noor/runtime/input.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"
#include "noor/scripting/dotnet_host.h"
#include "noor/ui/world_ui.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
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
    std::uint32_t msaa = 4;
    bool cpu_sky = false;
    bool trace_environment = false;
    std::uint32_t water_probes = 0;  // N: the last frame's depth read back at N x N pixels, compared with water_height
    std::uint32_t water_debug = 0;   // 8D part 7: none, depth, flow, waves, normals, cost (render::FrameDesc::water_debug)
    double stall_ms = 0;             // 9A part 1: a deliberate stall in every game update, to prove the yardstick check catches it
    std::uint32_t window_width = 0, window_height = 0;  // --window WxH: overrides Settings/display.json (tests)
    bool windowed = false;                              // --windowed: never fullscreen or borderless (tests)
    std::string display;                                // --display <file.json>: in place of Settings/display.json (tests)
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
        else if (a == "--msaa" && more) o.msaa = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        else if (a == "--cpu-sky") o.cpu_sky = true;
        else if (a == "--stall-ms" && more) o.stall_ms = std::stod(argv[++i]);
        else if (a == "--trace-environment") o.trace_environment = true;
        else if (a == "--windowed") o.windowed = true;
        else if (a == "--display" && more) o.display = argv[++i];
        else if (a == "--window" && more) {
            const std::string size = argv[++i];
            const auto x = size.find('x');
            if (x == std::string::npos) return false;
            o.window_width = static_cast<std::uint32_t>(std::stoul(size.substr(0, x)));
            o.window_height = static_cast<std::uint32_t>(std::stoul(size.substr(x + 1)));
        }
        else if (a == "--water-probes" && more) o.water_probes = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        else if (a == "--water-debug" && more) {
            const std::string view = argv[++i];
            const char* views[] = {"none", "depth", "flow", "waves", "normals", "cost"};
            bool found = false;
            for (std::uint32_t k = 0; k < 6; ++k)
                if (view == views[k]) {
                    o.water_debug = k;
                    found = true;
                }
            if (!found) return false;
        }
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
        // Display settings (2D part 6): the window, and how the game's picture fills it.
        auto display = noor::game::load_display_settings(project->root());
        if (!o.display.empty()) {
            auto text = noor::fs::read_file(o.display, 1u << 20);
            const Json j = text ? Json::parse(*text, nullptr, false) : Json();
            if (j.is_discarded() || !j.is_object()) {
                NOOR_LOG_FATAL("Player", "--display {}: not a JSON object", o.display);
                noor::platform::shutdown();
                return 1;
            }
            display = noor::game::display_settings(j);
        }
        if (o.window_width) display.window_width = std::max(160u, o.window_width);
        if (o.window_height) display.window_height = std::max(120u, o.window_height);
        if (o.windowed) display.window_mode = noor::game::DisplaySettings::WindowMode::Windowed;
        noor::platform::WindowDesc window_desc{.title = game->title, .width = display.window_width, .height = display.window_height};
        window_desc.mode = static_cast<noor::platform::WindowDesc::Mode>(display.window_mode);
        auto window = noor::platform::Window::create(window_desc);
        if (!window) {
            NOOR_LOG_FATAL("Player", "{}", window.error().describe());
            noor::platform::shutdown();
            return 1;
        }
        auto renderer = noor::render::Renderer::create(**window, {.enable_validation = o.validation, .msaa_samples = o.msaa, .clear_color = {0.05f, 0.045f, 0.07f, 1.0f}});
        if (!renderer) {
            NOOR_LOG_FATAL("Player", "{}", renderer.error().describe());
            window->reset();
            noor::platform::shutdown();
            return 1;
        }
        (*renderer)->set_display(display.display);
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
                noor::game::SceneRenderer scene_renderer(**renderer, project->root() / ".noor" / "cache" / "textures",
                                                         noor::game::file_resolver(noor::game::registry_asset_path(*project, registry)));
                scene_renderer.set_cooked_root(project->root());
                scene_renderer.environment().use_cpu_atmosphere(o.cpu_sky);
                double last_refresh = noor::platform::time_seconds();
                const noor::game::Hud hud;  // the HUD scripts draw (noor.ui / Ui), with the built-in font
                if (auto s = (*renderer)->set_overlay_atlas(hud.atlas_width(), hud.atlas_height(), hud.atlas()); !s)
                    NOOR_LOG_ERROR("Player", "HUD font: {}", s.error().describe());
                std::vector<noor::render::OverlayVertex> overlay;
                const auto asset_path = noor::game::registry_asset_path(*project, registry);
                // Game UI documents (part 6, ADR-041): the scene's `ui_document` components and what scripts open. RmlUi
                // starts with the first document (a game without UI pays nothing). Its clock is the world's real time:
                // transitions run while the game is paused, and test runs (--frames) are repeatable.
                noor::ui::WorldUi game_ui({project->root(), noor::ui::renderer_textures(**renderer),
                                           [w = world->get()] { return w->real_time(); }, noor::platform::set_clipboard_text,
                                           noor::platform::clipboard_text});
                game_ui.attach(**world);
                noor::render::UiFrame ui_frame;
                bool text_input = false;
                bool captured = false;
                int ignore_motion = 0;  // frames of mouse movement to drop after capture changes
                bool warned_no_camera = false;
                std::optional<noor::render::CapturedFrame> last_capture;
                noor::render::FrameDesc last_frame_desc;  // the final frame, redrawn if its picture was missed
                std::vector<noor::render::WaterDraw> last_water;
                std::vector<noor::render::DrawItem> last_draws;
                std::vector<noor::render::OverlayVertex> last_overlay;
                // Frame timings for --report (Milestone 7, part 6): wall time per frame, and where the CPU
                // spent it (the game, collecting the view, recording the frame), plus the GPU's own time.
                struct Timing {
                    double wall = 0, game = 0, view = 0, collect = 0, record = 0, gpu = -1, environment = -1, particles = -1;
                    double scene = -1, scene_copy = -1, see_through = -1;  // GPU passes (8D: what water costs)
                    double ui = -1;                                        // the game UI's pass (part 6c)
                    double cpu = 0;  // 9A part 1: the game, the view and recording together (what a frame costs the CPU, vsync aside)
                };
                double first_frame_at = -1;  // 9A part 1: seconds from the process starting to its first frame drawn
                std::vector<Timing> timings;
                Json environment_trace = Json::array();
                double frame_start = noor::platform::time_seconds();
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
                    const double t_game = noor::platform::time_seconds();
                    if (game_ui.ui() && o.input) {  // the UI's input first: what it does reaches scripts this frame
                        noor::ui::WorldUi::Input ui_in;
                        using B = noor::platform::MouseButton;
                        using P = noor::platform::Key;
                        using K = noor::ui::Key;
                        ui_in.mouse = !captured;
                        ui_in.mouse_x = in.mouse_x;
                        ui_in.mouse_y = in.mouse_y;
                        for (const B b : {B::Left, B::Right, B::Middle}) {
                            ui_in.pressed[static_cast<std::size_t>(b)] = in.pressed(b);
                            ui_in.released[static_cast<std::size_t>(b)] = in.released(b);
                        }
                        ui_in.wheel = -in.wheel;
                        const auto ui_key = [](P k) -> std::optional<K> {
                            switch (k) {
                            case P::Tab: return K::Tab;
                            case P::Enter: return K::Enter;
                            case P::Space: return K::Space;
                            case P::Escape: return K::Escape;
                            case P::Backspace: return K::Backspace;
                            case P::Delete: return K::Delete;
                            case P::Left: return K::Left;
                            case P::Right: return K::Right;
                            case P::Up: return K::Up;
                            case P::Down: return K::Down;
                            case P::Home: return K::Home;
                            case P::End: return K::End;
                            case P::PageUp: return K::PageUp;
                            case P::PageDown: return K::PageDown;
                            case P::A: return K::A;
                            case P::C: return K::C;
                            case P::V: return K::V;
                            case P::X: return K::X;
                            default: return std::nullopt;
                            }
                        };
                        for (const P k : in.key_presses)
                            if (const auto u = ui_key(k)) ui_in.keys.emplace_back(*u, true);
                        for (std::size_t i = 1; i < noor::platform::InputState::kKeyCount; ++i)
                            if (in.keys_released[i])
                                if (const auto u = ui_key(static_cast<P>(i))) ui_in.keys.emplace_back(*u, false);
                        ui_in.modifiers = {in.down(P::LeftCtrl) || in.down(P::RightCtrl), in.down(P::LeftShift) || in.down(P::RightShift),
                                           in.down(P::LeftAlt) || in.down(P::RightAlt)};
                        ui_in.text = in.text;
                        game_ui.input(ui_in);
                        if (game_ui.wants_text() != text_input) {  // a text box got or lost focus
                            text_input = game_ui.wants_text();
                            noor::platform::set_text_input(**window, text_input);
                        }
                    }
                    (*world)->frame(dt, game_in);
                    if (o.stall_ms > 0)  // spin, not sleep: a sleep's length is up to the OS's timer
                        while ((noor::platform::time_seconds() - t_game) * 1000.0 < o.stall_ms) {}
                    const double t_game_done = noor::platform::time_seconds();
                    if (now - last_refresh > 0.5) {  // materials and textures edited while the game runs
                        scene_renderer.refresh();
                        last_refresh = now;
                    }

                    auto view = noor::game::collect_view((*world)->scene(), asset_path, &(*world)->render_poses(), project->root());
                    view.lighting.time = static_cast<float>((*world)->time());  // the clouds drift with the game
                    if (o.trace_environment) {
                        Json zones = Json::array();
                        for (const auto& z : view.weather) zones.push_back(z.weight);
                        const auto eye = view.camera ? view.camera->world.transform_point({0, 0, 0}) : noor::math::Vec3{0, 0, 0};
                        environment_trace.push_back({{"frame", (*world)->frame_count()}, {"camera", {eye.x, eye.y, eye.z}},
                                                     {"fog_density", view.lighting.fog.density}, {"ev100", view.lighting.ev100}, {"rain", view.lighting.weather.rain},
                                                     {"zones", std::move(zones)}});
                    }
                    if (!view.camera && !warned_no_camera) {
                        NOOR_LOG_WARN("Player", "The scene has no camera; showing it from above. Add a camera component (or Create ▸ Player) to see the game's view");
                        warned_no_camera = true;
                    }
                    const double t_collected = noor::platform::time_seconds();
                    const auto draws = scene_renderer.draws(view);
                    const double t_view_done = noor::platform::time_seconds();
                    const bool last_frame = o.frames && (*world)->frame_count() == o.frames;
                    if (last_frame && (!o.report.empty() || !o.screenshot.empty())) (*renderer)->request_capture(o.water_probes > 0);
                    overlay.clear();
                    // The HUD fills the game's picture: laid out at the base resolution's size (so a 320 x 180 game's
                    // text is as big as designed), then scaled to the picture and moved to where it is on screen.
                    const auto& layout = (*renderer)->display_layout();
                    const auto& dd = display.display;
                    float hud_scale = 1.0f;
                    if (dd.stretch != noor::render::DisplayDesc::Stretch::Off && dd.base_width > 0 && dd.base_height > 0)
                        hud_scale = std::max(0.01f, std::min(static_cast<float>(layout.width) / static_cast<float>(dd.base_width),
                                                             static_cast<float>(layout.height) / static_cast<float>(dd.base_height)));
                    hud.build((*world)->ui_commands(), static_cast<float>(layout.width) / hud_scale, static_cast<float>(layout.height) / hud_scale, overlay);
                    for (auto& v : overlay)
                        v.position = {v.position.x * hud_scale + static_cast<float>(layout.x), v.position.y * hud_scale + static_cast<float>(layout.y)};
                    // Effect particles (Milestone 8B), seen from this frame's camera.
                    const auto frame_camera = noor::game::SceneRenderer::camera(view);
                    std::span<const noor::render::ParticleDraw> particle_draws;
                    if (const auto* ps = (*world)->system<noor::fx::ParticleSystem>())
                        particle_draws = scene_renderer.particles(*ps, **world, noor::math::inverse(frame_camera.view).transform_point({0, 0, 0}));
                    noor::game::set_water_time(view.water, (*world)->time());  // waves and ripples on the game's clock
                    // Game UI, laid out in the game's picture, over the HUD.
                    ui_frame = game_ui.draw(static_cast<std::int32_t>(layout.x), static_cast<std::int32_t>(layout.y),
                                            static_cast<std::int32_t>(layout.width), static_cast<std::int32_t>(layout.height));
                    auto frame = (*renderer)->render_frame({frame_camera, view.lighting, draws, overlay, particle_draws, view.water, o.water_debug, ui_frame});
                    if (!frame) {
                        NOOR_LOG_ERROR("Player", "Frame failed: {}", frame.error().describe());
                        exit_code = 1;
                        break;
                    }
                    if (*frame == noor::render::FrameResult::Skipped && (*window)->is_minimized()) noor::platform::wait_for_events(50);
                    if (first_frame_at < 0 && *frame != noor::render::FrameResult::Skipped) first_frame_at = noor::platform::process_stats().seconds_alive;
                    {
                        const double t_end = noor::platform::time_seconds();
                        const auto& fs = (*renderer)->last_frame_stats();
                        double environment_ms = -1;
                        for (const auto& pass : fs.passes)
                            if (std::string_view(pass.name) == "Environment") environment_ms = pass.ms;
                        const auto* particles = (*world)->system<noor::fx::ParticleSystem>();
                        timings.push_back({(t_end - frame_start) * 1000.0, (t_game_done - t_game) * 1000.0,
                                           (t_view_done - t_game_done) * 1000.0, (t_collected - t_game_done) * 1000.0, fs.cpu_record_ms, fs.gpu_ms,
                                           environment_ms, particles ? particles->stats().step_ms : -1.0});
                        for (const auto& pass : fs.passes) {
                            const std::string_view name = pass.name;
                            if (name == "Scene") timings.back().scene = pass.ms;
                            else if (name == "Scene copy") timings.back().scene_copy = pass.ms;
                            else if (name == "See-through") timings.back().see_through = pass.ms;
                            else if (name == "Game UI") timings.back().ui = pass.ms;
                        }
                        timings.back().cpu = timings.back().game + timings.back().view + std::max(0.0, timings.back().record);
                        frame_start = t_end;
                    }
                    if (auto cap = (*renderer)->take_capture(); cap && cap->has_value()) last_capture = std::move(**cap);
                    if (last_frame) {
                        last_draws = draws;
                        last_overlay = overlay;
                        last_water = view.water;
                        last_frame_desc = {noor::game::SceneRenderer::camera(view), view.lighting, {}, {}, particle_draws, last_water};
                        last_frame_desc.ui = ui_frame;  // stays valid: nothing draws the UI again
                    }
                }
                // The last frame wasn't presented (the swapchain was being rebuilt, the window was busy):
                // draw the same picture again (the game doesn't move on) until it is, so a test's
                // screenshot and report never go missing. A few tries at most.
                if (o.frames && (*world)->frame_count() == o.frames && !last_capture && (!o.report.empty() || !o.screenshot.empty())) {
                    last_frame_desc.draws = last_draws;
                    last_frame_desc.overlay = last_overlay;
                    for (int attempt = 0; attempt < 30 && !last_capture; ++attempt) {
                        (void)noor::platform::pump_events();
                        (*renderer)->request_capture(o.water_probes > 0);
                        if (auto frame = (*renderer)->render_frame(last_frame_desc); !frame) break;
                        if (auto cap = (*renderer)->take_capture(); cap && cap->has_value()) last_capture = std::move(**cap);
                    }
                    if (last_capture) NOOR_LOG_INFO("Player", "The last frame was drawn again for the screenshot (it wasn't presented the first time)");
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
                    // Water's drawn surface against gameplay's (8D part 2): the last frame's depth at a grid of pixels,
                    // turned back into world points, each beside water_height at its (x, z) (the same game time).
                    if (o.water_probes > 0 && last_capture) {
                        if (last_capture->depth.empty()) NOOR_LOG_WARN("Player", "--water-probes needs --msaa 1 (the depth is read back without MSAA)");
                        const auto& cam = last_frame_desc.camera;
                        const float aspect = static_cast<float>(last_capture->width) / static_cast<float>(std::max(1u, last_capture->height));
                        const auto world_from_clip =
                            noor::math::inverse(cam.projection(aspect) * cam.view);
                        Json probes = Json::array();
                        double worst = 0.0;
                        int on_water = 0;
                        const std::uint32_t n = o.water_probes;
                        for (std::uint32_t j = 0; j < n && !last_capture->depth.empty(); ++j)
                            for (std::uint32_t i = 0; i < n; ++i) {
                                const std::uint32_t px = (2 * i + 1) * last_capture->width / (2 * n), py = (2 * j + 1) * last_capture->height / (2 * n);
                                const float d = last_capture->depth_at(px, py);
                                if (d <= 0.0f) continue;  // the sky
                                const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(last_capture->width) * 2.0f - 1.0f;
                                const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(last_capture->height) * 2.0f - 1.0f;
                                const noor::math::Vec4 h = world_from_clip * noor::math::Vec4{u, v, d, 1.0f};
                                const noor::math::Vec3 p{h.x / h.w, h.y / h.w, h.z / h.w};
                                const auto water = (*world)->water_at(p.x, p.z);
                                const noor::math::Vec3 eye = noor::math::inverse(cam.view).transform_point({0, 0, 0});
                                // The still water's depth over the ground there (8D part 3: the body's depth map;
                                // kDeepWater without one), so a check can tell the shallows from a beach.
                                Json depth = nullptr;
                                if (const auto* ws = (*world)->system<noor::game::WaterSystem>(); ws && water)
                                    for (const auto& b : ws->bodies())
                                        if (b.entity == water->entity) depth = noor::game::depth_at(b.depth.get(), {p.x, p.z});
                                Json probe{{"pixel", {px, py}}, {"world", {p.x, p.y, p.z}}, {"distance", noor::math::length(p - eye)},
                                           {"water", water ? Json(water->height) : Json(nullptr)}, {"depth", depth},
                                           {"error", water ? Json(std::abs(p.y - water->height)) : Json(nullptr)}};
                                if (water && std::abs(p.y - water->height) < 0.5f) {  // a pixel showing the water (not a post in it)
                                    ++on_water;
                                    worst = std::max(worst, static_cast<double>(std::abs(p.y - water->height)));
                                }
                                probes.push_back(std::move(probe));
                            }
                        report["water_probes"] = {{"probes", std::move(probes)}, {"on_water", on_water}, {"max_error", worst}};
                    }
                    if (last_capture) {
                        const auto c = last_capture->rgba_at(last_capture->width / 2, last_capture->height / 2);
                        report["center_pixel"] = {c[0], c[1], c[2]};
                        // The whole picture's average colour (0..255), for tests that follow a picture changing
                        // smoothly (8D part 4: a camera walked down through the water's surface).
                        double sum[3]{};
                        for (std::uint32_t y = 0; y < last_capture->height; ++y)
                            for (std::uint32_t x = 0; x < last_capture->width; ++x) {
                                const auto q = last_capture->rgba_at(x, y);
                                for (int k = 0; k < 3; ++k) sum[k] += q[static_cast<std::size_t>(k)];
                            }
                        const double pixels = std::max(1.0, static_cast<double>(last_capture->width) * last_capture->height);
                        report["mean_rgb"] = {sum[0] / pixels, sum[1] / pixels, sum[2] / pixels};
                        // The pixel at UI position (40, 40) (the screen is 720 UI units tall), for HUD tests.
                        const auto ui = static_cast<std::uint32_t>(40.0f * static_cast<float>(last_capture->height) / noor::runtime::kUiHeight);
                        const auto p = last_capture->rgba_at(std::min(ui, last_capture->width - 1), std::min(ui, last_capture->height - 1));
                        report["ui_pixel_40_40"] = {p[0], p[1], p[2]};
                    }
                    report["hud_vertices"] = overlay.size();
                    if (game_ui.ui()) {
                        const auto us = game_ui.ui()->stats();
                        report["ui"] = {{"documents", us.documents}, {"draws", us.draws}, {"vertices", us.vertices},
                                        {"textures", us.textures}, {"texture_bytes", us.texture_bytes}};
                    }
                    {
                        const auto l = (*renderer)->display_layout();
                        const auto screen = (*renderer)->swapchain_size();
                        report["display"] = {{"screen", {screen.width, screen.height}},
                                             {"render", {l.render_width, l.render_height}},
                                             {"rect", {l.x, l.y, l.width, l.height}},
                                             {"nearest", l.nearest}};
                    }
                    // The last frame's HUD text, so a test can read what the game shows (a score, a timer).
                    Json hud_text = Json::array();
                    for (const auto& c : (*world)->ui_commands())
                        if (c.kind == noor::runtime::UiCommand::Kind::Text) hud_text.push_back(c.text);
                    report["hud_text"] = std::move(hud_text);
                    Json passes = Json::object();  // GPU milliseconds per pass of a recent frame
                    for (const auto& pass : (*renderer)->last_frame_stats().passes) passes[pass.name] = pass.ms;
                    report["gpu_passes"] = std::move(passes);
                    const auto& fs = (*renderer)->last_frame_stats();
                    report["render"] = {{"gpu_ms", fs.gpu_ms},
                                        {"draw_calls", fs.draw_calls},
                                        {"instances", fs.instances},
                                        {"culled", fs.culled},
                                        {"triangles", fs.triangles},
                                        {"shadow_draw_calls", fs.shadow_draw_calls},
                                        {"shadow_draws_culled", fs.shadow_draws_culled},
                                        {"transparent_draws", fs.transparent_draws},
                                        {"msaa_samples", fs.msaa_samples},
                                        {"texture_bytes", (*renderer)->texture_memory()},
                                        {"mesh_bytes", (*renderer)->mesh_memory()},
                                        {"render_target_bytes", (*renderer)->render_target_memory()},
                                        {"width", last_capture ? last_capture->width : 0u},
                                        {"height", last_capture ? last_capture->height : 0u}};
                    // Timings without the first second (loading, first uploads): median and 95th percentile.
                    if (timings.size() > 70) {
                        const auto stat = [&](double Timing::*field) {
                            std::vector<double> v;
                            for (std::size_t i = 60; i < timings.size(); ++i)
                                if (timings[i].*field >= 0) v.push_back(timings[i].*field);
                            if (v.empty()) return Json(nullptr);
                            std::ranges::sort(v);
                            return Json{{"median", v[v.size() / 2]}, {"p95", v[std::min(v.size() - 1, v.size() * 95 / 100)]},
                                        {"p99", v[std::min(v.size() - 1, v.size() * 99 / 100)]}, {"max", v.back()},
                                        {"min", v.front()}};
                        };
                        report["timing_ms"] = {{"frames", timings.size() - 60},
                                               {"wall", stat(&Timing::wall)},
                                               {"game", stat(&Timing::game)},
                                               {"view", stat(&Timing::view)},
                                               {"collect", stat(&Timing::collect)},
                                               {"record", stat(&Timing::record)},
                                               {"gpu", stat(&Timing::gpu)},
                                               {"environment_gpu", stat(&Timing::environment)},
                                               {"scene_gpu", stat(&Timing::scene)},
                                               {"scene_copy_gpu", stat(&Timing::scene_copy)},
                                               {"see_through_gpu", stat(&Timing::see_through)},
                                               {"ui_gpu", stat(&Timing::ui)},
                                               {"particles", stat(&Timing::particles)},
                                               {"cpu", stat(&Timing::cpu)}};
                    }
                    // Effect particles (Milestone 8B): how many, and a fingerprint of every one (equal runs, equal fingerprints).
                    if (const auto* particles = (*world)->system<noor::fx::ParticleSystem>()) {
                        const auto ps = particles->stats();
                        report["particles"] = {{"emitters", ps.emitters}, {"alive", ps.particles},
                                               {"fingerprint", std::format("{:016x}", ps.fingerprint)},
                                               {"hits", ps.hits}, {"throttled", ps.throttled}, {"splashes", ps.splashes}};  // part 7: collisions, the scene's budget
                    }
                    // Sprites (Milestone 8B, part 6): what each is playing and since when, and how many script-played
                    // animations have ended (their end events).
                    {
                        Json sprites = Json::array();
                        for (const auto& e : (*world)->scene().entities) {
                            if (!e.has("sprite") || !e.components.at("sprite").is_object()) continue;
                            const auto& c = e.components.at("sprite");
                            sprites.push_back({{"name", e.name}, {"animation", c.value("animation", std::string())},
                                               {"started", c.contains("started") ? c["started"] : Json(nullptr)}});
                        }
                        const auto* anim = (*world)->system<noor::game::SpriteAnimationSystem>();
                        report["sprites"] = {{"entities", std::move(sprites)}, {"ended", anim ? anim->ended() : 0}};
                    }
                    // Skeletal animation (Milestone 8C, part 2): what each animator plays and since when, how many
                    // one-shot clips have ended, and a fingerprint of every bone pose of the last frame.
                    {
                        Json animators = Json::array();
                        for (const auto& e : (*world)->scene().entities) {
                            if (!e.has("animator") || !e.components.at("animator").is_object()) continue;
                            const auto& c = e.components.at("animator");
                            animators.push_back({{"name", e.name}, {"animation", c.value("state", c.value("animation", std::string()))},  // a controller: its state
                                                 {"started", c.contains("started") ? c["started"] : Json(nullptr)},
                                                 // where it is (root motion moves characters, part 4), to the millimetre
                                                 {"position", [&] {
                                                      const auto w = noor::runtime::api::world_position(**world, e.id).value_or(noor::math::Vec3{});
                                                      return std::format("{:.3f} {:.3f} {:.3f}", w.x, w.y, w.z);
                                                  }()}});
                        }
                        const auto* anim = (*world)->system<noor::game::AnimatorSystem>();
                        const auto st = anim ? anim->stats() : noor::game::AnimatorSystem::Stats{};
                        report["animation"] = {{"entities", std::move(animators)}, {"posed", st.animators}, {"bones", st.nodes},
                                               {"ended", st.ended}, {"fingerprint", std::format("{:016x}", st.fingerprint)}};
                    }
                    report["ev100"] = last_frame_desc.lighting.ev100;  // what the camera exposed with (automatic exposure: metered)
                    if (o.trace_environment) report["environment_trace"] = std::move(environment_trace);
                    report["sky_builds"] = {{"tables", (*renderer)->atmosphere_table_builds()}, {"sky", (*renderer)->atmosphere_sky_builds()}};
                    // What the process costs (9A part 1, the yardsticks): memory, threads, and how long it took to show its first frame.
                    {
                        const auto ps = noor::platform::process_stats();
                        constexpr double mb = 1.0 / (1024.0 * 1024.0);
                        report["process"] = {{"working_set_mb", ps.working_set * mb}, {"peak_working_set_mb", ps.peak_working_set * mb},
                                             {"private_mb", ps.private_bytes * mb}, {"threads", ps.threads}, {"first_frame_s", first_frame_at},
                                             {"gpu_resident_mb", ((*renderer)->texture_memory() + (*renderer)->mesh_memory() + (*renderer)->render_target_memory()) * mb}};
                    }
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
                                 "[--report <file.json>] [--screenshot <file.bmp>] [--no-audio] [--no-input] [--validation] [--log <file>] [--msaa 1|2|4|8] [--cpu-sky] [--trace-environment]");
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
