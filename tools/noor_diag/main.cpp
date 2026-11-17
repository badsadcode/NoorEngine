// noor_diag: NoorEngine diagnostics tool.
//
// Opens a window, initializes Vulkan, reports the GPU and draws a lit, spinning cube on a
// ground plane in front of Noor purple. Exits non-zero on any failure or Vulkan validation
// error, which makes it the GPU smoke test. Stays as a permanent developer utility.
//
//   noor_diag [--frames N] [--cubes N] [--no-validation] [--verify-render] [--lifecycle-test]
//             [--screenshot <file.bmp>] [--log <file>]
//
// Controls: hold the right mouse button to look around, WASD to move, Q/E down/up,
// Shift to go faster, mouse wheel to change speed, Esc to quit.
//
// --cubes N adds a grid of N spinning cubes (one draw call each): a simple stress test.
// --verify-render shows only the hero cube from a fixed camera, reads a frame back from the
// GPU and checks real pixels: background, the cube's front face and its top face must have
// the expected colors. That catches broken projection, culling/winding, depth and color bugs.
// --lifecycle-test minimizes, restores, resizes and (with 2+ monitors) moves the window by
// itself and checks that rendering pauses, resumes and follows the window. Exit code 4 on failure.

#include "noor/foundation/log.h"
#include "noor/foundation/math.h"
#include "noor/platform/platform.h"
#include "noor/platform/window.h"
#include "noor/asset/file_watcher.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/scene/prefab.h"
#include "noor/asset/gltf_import.h"
#include "noor/render/fly_camera.h"
#include "noor/render/renderer.h"

#include <algorithm>
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
#include <unordered_map>
#include <vector>

namespace {

using noor::math::Vec3;

struct Options {
    std::uint64_t max_frames = 0;  // 0 = run until the window is closed
    std::uint32_t cubes = 0;       // extra cubes in a grid (stress test)
    bool validation = true;
    bool verify_render = false;
    bool lifecycle_test = false;
    std::string screenshot_path;  // non-empty: save frame 90 as a 24-bit BMP
    std::string glb_path;         // non-empty: show this GLB model instead of the cube scene
    std::string project_path;     // with scene_path: show a project scene (prefabs expanded)
    std::string scene_path;       // project-relative, e.g. Scenes/Main.noorscene
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
        } else if (arg == "--cubes" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            if (std::from_chars(value.data(), value.data() + value.size(), out.cubes).ec != std::errc{} ||
                out.cubes > 100'000) {
                return false;
            }
        } else if (arg == "--no-validation") {
            out.validation = false;
        } else if (arg == "--verify-render") {
            out.verify_render = true;
        } else if (arg == "--lifecycle-test") {
            out.lifecycle_test = true;
        } else if (arg == "--log" && i + 1 < argc) {
            out.log_path = argv[++i];
        } else if (arg == "--project" && i + 1 < argc) {
            out.project_path = argv[++i];
        } else if (arg == "--scene" && i + 1 < argc) {
            out.scene_path = argv[++i];
        } else if (arg == "--glb" && i + 1 < argc) {
            out.glb_path = argv[++i];
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

// A 2 m box (−1..1 on each axis) with one color per face, counter-clockwise front faces (ADR-022).
struct CubeMesh {
    std::vector<noor::render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Face order: +Y, −Y, +Z, −Z, +X, −X.
constexpr std::array<std::uint32_t, 6> kHeroColors = {0x22D3EE, 0xE5484D, 0x7C4DFF, 0xF59E0B, 0xEC4899, 0xFACC15};
constexpr std::array<std::uint32_t, 6> kGroundColors = {0x3B3354, 0x221D30, 0x2A2440, 0x2A2440, 0x2A2440, 0x2A2440};

CubeMesh make_cube(const std::array<std::uint32_t, 6>& colors = kHeroColors) {
    struct Face {
        Vec3 normal, u, v;
    };
    // u × v = normal, so (−u−v, +u−v, +u+v, −u+v) winds counter-clockwise seen from outside.
    const Face faces[] = {
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},   // +Y top: cyan
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},   // −Y bottom: red
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},    // +Z front: purple
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},  // −Z back: orange
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},   // +X right: pink
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // −X left: yellow
    };
    CubeMesh mesh;
    for (std::size_t fi = 0; fi < 6; ++fi) {
        const Face& f = faces[fi];
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const Vec3 color = srgb_hex(colors[fi]);
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

// Automated window lifecycle test (--lifecycle-test): drives the window through minimize,
// restore, resize and (with 2+ monitors) a move to another display, checking after each step
// that rendering stops/resumes correctly and the swapchain follows the window.
struct Lifecycle {
    enum class Phase { Warmup, Minimized, Restored, Resized, ResizedBack, OtherDisplay, BackHome, Done };
    bool enabled = false;
    bool finished = false;
    std::uint32_t failures = 0;
    Phase phase = Phase::Warmup;
    double phase_start = 0.0;
    std::uint64_t frames_at_phase = 0;
    std::uint64_t rebuilds_at_phase = 0;

    void check(bool ok, std::string_view what) {
        if (ok) {
            NOOR_LOG_INFO("Lifecycle", "PASS: {}", what);
        } else {
            ++failures;
            NOOR_LOG_ERROR("Lifecycle", "FAIL: {}", what);
        }
    }

    void enter(Phase next, const noor::render::Renderer& r) {
        phase = next;
        phase_start = noor::platform::time_seconds();
        frames_at_phase = r.frames_presented();
        rebuilds_at_phase = r.swapchain_rebuilds();
    }

    // Returns true when the test is complete.
    bool step(noor::platform::Window& window, noor::render::Renderer& r) {
        const double elapsed = noor::platform::time_seconds() - phase_start;
        const std::uint64_t presented = r.frames_presented();
        const auto matches_window = [&] {
            const auto win = window.pixel_size();
            const auto sc = r.swapchain_size();
            return win.width > 0 && win.width == sc.width && win.height == sc.height;
        };

        switch (phase) {
        case Phase::Warmup:
            if (presented >= 30) {
                NOOR_LOG_INFO("Lifecycle", "Minimizing");
                enter(Phase::Minimized, r);  // snapshot first: the action itself may render
                window.minimize();
            }
            break;
        case Phase::Minimized:
            if (elapsed >= 0.75) {
                check(window.is_minimized(), "window reports minimized");
                check(presented == frames_at_phase, std::format("no frames presented while minimized ({} presented)",
                                                                presented - frames_at_phase));
                NOOR_LOG_INFO("Lifecycle", "Restoring");
                enter(Phase::Restored, r);
                window.restore();
            }
            break;
        case Phase::Restored:
            if (presented >= frames_at_phase + 10 || elapsed > 3.0) {
                check(presented >= frames_at_phase + 10, "rendering resumes after restore");
                NOOR_LOG_INFO("Lifecycle", "Resizing to 800x600");
                enter(Phase::Resized, r);
                window.set_size(800, 600);
            }
            break;
        case Phase::Resized:
        case Phase::ResizedBack:
            if ((matches_window() && presented >= frames_at_phase + 10) || elapsed > 3.0) {
                const auto sc = r.swapchain_size();
                check(matches_window(), std::format("swapchain follows the window ({}x{})", sc.width, sc.height));
                check(r.swapchain_rebuilds() > rebuilds_at_phase, "swapchain was rebuilt for the new size");
                if (phase == Phase::Resized) {
                    NOOR_LOG_INFO("Lifecycle", "Resizing back to 1280x720");
                    enter(Phase::ResizedBack, r);
                    window.set_size(1280, 720);
                } else if (noor::platform::display_count() > 1) {
                    NOOR_LOG_INFO("Lifecycle", "Moving to display 2 of {}", noor::platform::display_count());
                    enter(Phase::OtherDisplay, r);
                    window.move_to_display(1);
                } else {
                    NOOR_LOG_INFO("Lifecycle", "Only one display connected: skipping the monitor move");
                    enter(Phase::Done, r);
                }
            }
            break;
        case Phase::OtherDisplay:
        case Phase::BackHome:
            if (elapsed > 1.5) {
                const std::uint32_t expected = phase == Phase::OtherDisplay ? 1u : 0u;
                check(window.display_index() == expected, std::format("window is on display {}", expected + 1));
                check(presented >= frames_at_phase + 10, "rendering continues on that display");
                if (phase == Phase::OtherDisplay) {
                    enter(Phase::BackHome, r);
                    window.move_to_display(0);
                } else {
                    enter(Phase::Done, r);
                }
            }
            break;
        case Phase::Done:
            if (elapsed > 0.5) {
                finished = true;
                NOOR_LOG_INFO("Lifecycle", "Finished with {} failure(s)", failures);
                return true;
            }
            break;
        }
        return false;
    }
};

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

// The diagnostic frame: the default sun, and the fixed purple background (not the sky), so the
// corner pixel checks exact sRGB encoding through the tonemap pass.
noor::render::FrameDesc diag_frame(const noor::render::Camera& camera, std::span<const noor::render::DrawItem> draws) {
    noor::render::FrameDesc frame;
    frame.camera = camera;
    frame.lighting.sky_background = false;
    frame.draws = draws;
    return frame;
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
        const CubeMesh ground_data = make_cube(kGroundColors);
        auto cube = (*renderer)->create_mesh({cube_data.vertices, cube_data.indices, "Cube"});
        auto ground = (*renderer)->create_mesh({ground_data.vertices, ground_data.indices, "Ground"});
        if (!cube || !ground) {
            NOOR_LOG_FATAL("Diag", "{}", (!cube ? cube.error() : ground.error()).describe());
            exit_code = 1;
        }

        // Scene: hero cube at the origin, ground slab just below it, optional grid of small cubes.
        using noor::math::rotation;
        using noor::math::scale;
        using noor::math::translation;
        const auto grid_side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(options.cubes))));
        constexpr float kSpacing = 3.0f;
        const float grid_half = static_cast<float>(grid_side) * kSpacing * 0.5f;
        const float ground_half = std::max(20.0f, grid_half + 8.0f);
        const float grid_center_z = -(grid_half + 4.0f);  // grid sits behind the hero cube
        std::vector<noor::render::DrawItem> draws;
        draws.reserve(options.cubes + 2);

        // --glb: a model (e.g. a BadBuilding building) instead of the hero cube. Collision nodes are
        // not drawn (Noor Interchange 3.5); Noor markers show as small cyan cubes.
        std::vector<noor::render::DrawItem> model_draws;
        std::vector<noor::render::MeshHandle> model_meshes;  // owned by the model; freed on reload
        Vec3 model_min{1e30f, 1e30f, 1e30f}, model_max{-1e30f, -1e30f, -1e30f};
        std::array<std::uint32_t, 6> cyan{};
        cyan.fill(0x22D3EE);
        const CubeMesh marker_data = make_cube(cyan);
        auto marker = (*renderer)->create_mesh({marker_data.vertices, marker_data.indices, "Marker"});

        // (Re)loads the model. On failure the previous model stays on screen (hot reload must
        // never leave you with nothing because a generator wrote a broken file).
        const auto grow_bounds = [&](const noor::math::Mat4& world, const noor::asset::ImportedMesh& mesh) {
            for (const auto& v : mesh.vertices) {
                const Vec3 p = world.transform_point(v.position);
                model_min = {std::min(model_min.x, p.x), std::min(model_min.y, p.y), std::min(model_min.z, p.z)};
                model_max = {std::max(model_max.x, p.x), std::max(model_max.y, p.y), std::max(model_max.z, p.z)};
            }
        };
        const auto upload = [&](const noor::asset::ImportedModel& model) {
            std::vector<noor::render::MeshHandle> handles;
            for (const auto& m : model.meshes) {
                auto h = (*renderer)->create_mesh(
                    {{reinterpret_cast<const noor::render::Vertex*>(m.vertices.data()), m.vertices.size()}, m.indices, m.name});
                handles.push_back(h ? *h : noor::render::MeshHandle{});
                model_meshes.push_back(handles.back());
            }
            return handles;
        };
        const auto reset_view = [&] {
            for (const auto h : model_meshes) (*renderer)->destroy_mesh(h);
            model_meshes.clear();
            model_draws.clear();
            model_min = {1e30f, 1e30f, 1e30f};
            model_max = {-1e30f, -1e30f, -1e30f};
        };

        // --project/--scene: the scene as it will run (prefabs expanded). Every mesh component is drawn
        // with its entity's world matrix; markers as cyan cubes; colliders are not drawn.
        std::vector<std::filesystem::path> scene_files;  // watched for hot reload
        const auto load_scene = [&]() -> bool {
            auto project = noor::asset::Project::open(options.project_path);
            if (!project) {
                NOOR_LOG_ERROR("Diag", "{}", project.error().describe());
                return false;
            }
            noor::asset::AssetRegistry registry;
            if (auto r = registry.scan(*project); !r) {
                NOOR_LOG_ERROR("Diag", "{}", r.error().describe());
                return false;
            }
            auto scene_file = project->resolve(options.scene_path);
            if (!scene_file) {
                NOOR_LOG_ERROR("Diag", "{}", scene_file.error().describe());
                return false;
            }
            auto authored = noor::scene::Scene::load(*scene_file);
            if (!authored) {
                NOOR_LOG_ERROR("Diag", "{}", authored.error().describe());
                return false;
            }
            noor::scene::PrefabLibrary prefabs(*project, registry);
            const auto expanded = noor::scene::expand_prefabs(*authored, prefabs.resolver());
            for (const auto& issue : expanded.issues) NOOR_LOG_WARN("Diag", "{}: {}", issue.entity, issue.message);

            reset_view();
            scene_files = {*scene_file};
            for (const auto& a : registry.assets()) {
                if (auto path = project->resolve(a.path)) scene_files.push_back(*path);
            }
            struct LoadedModel {
                noor::asset::ImportedModel model;
                std::vector<noor::render::MeshHandle> handles;
            };
            std::unordered_map<noor::Guid, std::optional<LoadedModel>> models;
            const auto& entities = expanded.scene.entities;
            for (std::size_t i = 0; i < entities.size(); ++i) {
                const auto& e = entities[i];
                if (e.has("marker") && marker) {
                    model_draws.push_back({*marker, expanded.scene.world_matrix(i) * scale({0.25f, 0.25f, 0.25f})});
                }
                if (!e.has("mesh")) continue;
                const auto& mesh = e.components["mesh"];
                const auto guid = noor::asset::guid_field(mesh, "asset");
                const int index = mesh.value("mesh_index", -1);
                if (!guid || index < 0) continue;
                auto [it, fresh] = models.try_emplace(*guid);
                if (fresh) {
                    const auto* record = registry.find(*guid);
                    if (record) {
                        if (auto path = project->resolve(record->path)) {
                            if (auto model = noor::asset::import_gltf(*path)) {
                                auto handles = upload(*model);
                                it->second = LoadedModel{std::move(*model), std::move(handles)};
                            }
                        }
                    }
                    if (!it->second) NOOR_LOG_WARN("Diag", "Asset {} could not be loaded", guid->to_string());
                }
                if (!it->second || index >= static_cast<int>(it->second->handles.size())) continue;
                const auto handle = it->second->handles[static_cast<std::size_t>(index)];
                if (!handle.valid()) continue;
                const auto world = expanded.scene.world_matrix(i);
                model_draws.push_back({handle, world});
                grow_bounds(world, it->second->model.meshes[static_cast<std::size_t>(index)]);
            }
            NOOR_LOG_INFO("Diag", "Scene '{}': {} authored entities, {} after expanding {} prefab instance(s), {} draws",
                          authored->name, authored->entities.size(), entities.size(), expanded.instances,
                          model_draws.size());
            return true;
        };

        const auto load_model = [&]() -> bool {
            if (!options.scene_path.empty()) return load_scene();
            auto model = noor::asset::import_gltf(options.glb_path);
            if (!model) {
                NOOR_LOG_ERROR("Diag", "{}", model.error().describe());
                return false;
            }
            reset_view();
            static_assert(sizeof(noor::asset::MeshVertex) == sizeof(noor::render::Vertex));
            upload(*model);
            for (std::size_t i = 0; i < model->nodes.size(); ++i) {
                const auto& n = model->nodes[i];
                const auto world = model->world_matrix(i);
                if (n.mesh >= 0 && !n.is_collision() && model_meshes[static_cast<std::size_t>(n.mesh)].valid()) {
                    model_draws.push_back({model_meshes[static_cast<std::size_t>(n.mesh)], world});
                    grow_bounds(world, model->meshes[static_cast<std::size_t>(n.mesh)]);
                }
                if (n.is_marker() && marker) {
                    model_draws.push_back({*marker, world * scale({0.25f, 0.25f, 0.25f})});
                }
            }
            NOOR_LOG_INFO("Diag", "Model '{}': {} nodes, {} meshes, {} render triangles, {} draws",
                          options.glb_path, model->nodes.size(), model->meshes.size(), model->render_triangles(),
                          model_draws.size());
            return true;
        };

        // Hot reload: when the GLB changes on disk (e.g. BadBuilding regenerates it), reload it.
        noor::asset::FileWatcher watcher;
        std::uint64_t model_reloads = 0;
        double last_watch_poll = 0.0;
        if ((!options.glb_path.empty() || !options.scene_path.empty()) && exit_code == 0) {
            if (load_model()) {
                if (options.scene_path.empty()) {
                    watcher.watch(options.glb_path);
                } else {
                    for (const auto& f : scene_files) watcher.watch(f);
                }
            } else {
                exit_code = 1;
            }
        }
        const bool show_model = !model_draws.empty();

        noor::render::FlyCamera fly;
        fly.position = options.cubes > 0 ? Vec3{0.0f, 2.0f + grid_half * 0.5f, grid_half + 8.0f} : Vec3{0.0f, 1.8f, 5.0f};
        fly.look_at(options.cubes > 0 ? Vec3{0.0f, 0.0f, grid_center_z} : Vec3{0, 0, 0});
        if (show_model) {  // a three-quarter view from the street side (+Z)
            const Vec3 c = (model_min + model_max) * 0.5f;
            const float size = std::max({model_max.x - model_min.x, model_max.y - model_min.y, model_max.z - model_min.z});
            if (options.scene_path.empty()) {
                fly.position = {c.x + size * 0.75f, c.y + size * 0.35f, c.z + size * 1.1f};
            } else {  // a whole scene (e.g. a street): lower and closer, looking along it
                fly.position = {c.x - size * 0.42f, c.y + size * 0.1f, c.z + size * 0.22f};
            }
            fly.look_at(c);
            fly.speed = std::max(5.0f, size * 0.25f);
        }
        bool mouse_captured = false;
        if (!options.verify_render && options.max_frames == 0) {
            NOOR_LOG_INFO("Diag", "Controls: hold right mouse = look, WASD = move, Q/E = down/up, Shift = faster, "
                                  "wheel = speed, Esc = quit");
        }

        noor::render::Camera camera;
        camera.view = noor::math::look_at({0.0f, 1.8f, 5.0f}, {0, 0, 0}, {0, 1, 0});
        camera.fov_y = noor::math::radians(50.0f);

        double cpu_ms_sum = 0.0, gpu_ms_sum = 0.0, gpu_ms_max = 0.0;
        std::uint64_t timed_frames = 0, gpu_timed_frames = 0;
        double last_frame_time = noor::platform::time_seconds();

        constexpr std::uint64_t kVerifyFrame = 30;
        constexpr std::uint64_t kScreenshotFrame = 90;
        bool verified = !options.verify_render;
        bool screenshot_taken = options.screenshot_path.empty() || options.verify_render;
        const double start = noor::platform::time_seconds();
        double last_report = start;
        std::uint64_t frames_at_last_report = 0;

        // While the user drags a window edge, Windows blocks this loop in its own modal loop.
        // The platform layer calls this from there so the picture keeps up with the new size.
        std::uint64_t live_redraws = 0;
        noor::platform::set_live_redraw_callback([&] {
            if (draws.empty()) return;  // nothing built yet
            (*renderer)->notify_resized();
            if ((*renderer)->render_frame(diag_frame(camera, draws))) ++live_redraws;
        });

        Lifecycle lifecycle;
        lifecycle.enabled = options.lifecycle_test;

        while (exit_code == 0) {
            const auto events = noor::platform::pump_events();
            if (events.quit_requested) {
                NOOR_LOG_INFO("Diag", "Window closed by user");
                break;
            }
            if (events.window_resized || events.window_restored) {
                (*renderer)->notify_resized();
            }
            if (lifecycle.enabled) {
                if (lifecycle.step(**window, **renderer)) break;  // finished
            }
            if (show_model && noor::platform::time_seconds() - last_watch_poll > 0.25) {
                last_watch_poll = noor::platform::time_seconds();
                if (!watcher.poll().empty()) {
                    NOOR_LOG_INFO("Diag", "'{}' changed on disk: reloading",
                                  options.scene_path.empty() ? options.glb_path : options.scene_path);
                    if (load_model()) {
                        ++model_reloads;
                        for (const auto& f : scene_files) watcher.watch(f);  // newly used assets, if any
                    }
                }
            }

            const double now = noor::platform::time_seconds();
            const auto dt = static_cast<float>(std::min(now - last_frame_time, 0.1));
            last_frame_time = now;

            // Camera controls (not in verify mode, which needs a fixed camera).
            const auto& in = noor::platform::input();
            using noor::platform::Key;
            using noor::platform::MouseButton;
            if (in.pressed(Key::Escape)) {
                NOOR_LOG_INFO("Diag", "Escape pressed");
                break;
            }
            if (!options.verify_render) {
                const bool want_capture = in.down(MouseButton::Right);
                if (want_capture != mouse_captured) {
                    noor::platform::set_relative_mouse(**window, want_capture);
                    mouse_captured = want_capture;
                }
                if (in.wheel != 0.0f) {
                    fly.speed = std::clamp(fly.speed * std::pow(1.25f, in.wheel), 0.5f, 200.0f);
                }
                const auto axis = [&](Key pos, Key neg) {
                    return (in.down(pos) ? 1.0f : 0.0f) - (in.down(neg) ? 1.0f : 0.0f);
                };
                fly.update({.move = {axis(Key::D, Key::A), axis(Key::E, Key::Q), axis(Key::W, Key::S)},
                            .look_dx = mouse_captured ? in.mouse_dx : 0.0f,
                            .look_dy = mouse_captured ? in.mouse_dy : 0.0f,
                            .boost = in.down(Key::LeftShift)},
                           dt);
                camera.view = fly.view();
            }

            // In verify mode the cube holds still and nothing else is drawn, so the pixel probes
            // are deterministic.
            const auto t = static_cast<float>(now - start);
            draws.clear();
            if (show_model) {
                draws.insert(draws.end(), model_draws.begin(), model_draws.end());
                const float half = std::max({40.0f, model_max.x - model_min.x, model_max.z - model_min.z});
                draws.push_back({*ground, translation({0.0f, -0.1f, 0.0f}) * scale({half, 0.1f, half})});
            } else {
                draws.push_back({*cube, rotation({0.2f, 1.0f, 0.1f}, options.verify_render ? 0.0f : t * 0.8f)});
            }
            if (!options.verify_render && !show_model) {
                draws.push_back({*ground, translation({0.0f, -1.6f, grid_center_z * 0.5f}) *
                                              scale({ground_half, 0.1f, ground_half - grid_center_z * 0.5f})});
                for (std::uint32_t i = 0; i < options.cubes; ++i) {
                    const float gx = static_cast<float>(i % grid_side) * kSpacing - grid_half + kSpacing * 0.5f;
                    const float gz = static_cast<float>(i / grid_side) * kSpacing - grid_half + kSpacing * 0.5f;
                    const float phase = static_cast<float>(i) * 0.37f;
                    draws.push_back({*cube, translation({gx, -0.9f, gz + grid_center_z}) *
                                                rotation({0.3f, 1.0f, 0.2f}, t * 0.6f + phase) *
                                                scale({0.5f, 0.5f, 0.5f})});
                }
            }

            const std::uint64_t presented = (*renderer)->frames_presented();
            if ((options.verify_render && presented == kVerifyFrame) ||
                (!screenshot_taken && presented == kScreenshotFrame)) {
                (*renderer)->request_capture();
            }

            auto frame = (*renderer)->render_frame(diag_frame(camera, draws));
            if (!frame) {
                NOOR_LOG_ERROR("Diag", "Frame failed: {}", frame.error().describe());
                exit_code = 1;
                break;
            }
            if (*frame == noor::render::FrameResult::Skipped && (*window)->is_minimized()) {
                noor::platform::wait_for_events(100);
                continue;
            }
            if (*frame == noor::render::FrameResult::Presented) {
                const auto& stats = (*renderer)->last_frame_stats();
                cpu_ms_sum += stats.cpu_record_ms;
                ++timed_frames;
                if (stats.gpu_ms >= 0.0) {
                    gpu_ms_sum += stats.gpu_ms;
                    gpu_ms_max = std::max(gpu_ms_max, stats.gpu_ms);
                    ++gpu_timed_frames;
                }
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
                (*window)->set_title(std::format(
                    "NoorEngine Diagnostics | {} | {:.0f} FPS (vsync) | CPU {:.2f} ms | GPU {:.2f} ms | {} draws, {} tris",
                    gpu.name, fps, stats.cpu_record_ms, stats.gpu_ms, stats.draw_calls, stats.triangles));
                last_report = now;
                frames_at_last_report = done;
            }
            if (options.max_frames > 0 && (*renderer)->frames_presented() >= options.max_frames) {
                break;
            }
        }

        noor::platform::set_live_redraw_callback({});
        if (model_reloads > 0) NOOR_LOG_INFO("Diag", "Model hot-reloaded {} time(s)", model_reloads);
        if (live_redraws > 0) {
            NOOR_LOG_INFO("Diag", "{} frame(s) drawn during live window resizing", live_redraws);
        }
        if (lifecycle.enabled && exit_code == 0) {
            if (!lifecycle.finished) {
                NOOR_LOG_ERROR("Lifecycle", "Test did not finish (window closed or frame limit hit)");
                exit_code = 4;
            } else if (lifecycle.failures > 0) {
                exit_code = 4;
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
        if (timed_frames > 0) {
            const auto& last = (*renderer)->last_frame_stats();
            NOOR_LOG_INFO("Diag", "Frame cost: {} draws, {} triangles | CPU record avg {:.3f} ms | GPU avg {} ms, max {} ms",
                          last.draw_calls, last.triangles, cpu_ms_sum / static_cast<double>(timed_frames),
                          gpu_timed_frames ? std::format("{:.3f}", gpu_ms_sum / static_cast<double>(gpu_timed_frames)) : "n/a",
                          gpu_timed_frames ? std::format("{:.3f}", gpu_ms_max) : "n/a");
        }

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
        NOOR_LOG_ERROR("Diag", "Usage: noor_diag [--frames N] [--cubes N] [--no-validation] [--verify-render] [--lifecycle-test] "
                               "[--screenshot <file.bmp>] [--log <file>]");
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
