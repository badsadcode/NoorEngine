// noor_cli: headless NoorEngine project tool. No window, no GPU: runs in CI and scripts.
//
//   noor_cli new <folder> <name>                     create a project
//   noor_cli scan <project>                          create/repair .noormeta files under Assets/
//   noor_cli info <file.glb>                         summarize a GLB (nodes, meshes, markers)
//   noor_cli texture <image files...>                how the engine reads each texture: kind (from its
//                                                    .noormeta or the name), size, mips, GPU memory
//   noor_cli make-material <project> <texture>       a .noormat from the texture set that texture belongs
//                                                    to (bricks_albedo/_normal/_orm/...), next to it
//   noor_cli add-model <project> <asset> <scene>     instantiate an asset into a scene (creates it)
//   noor_cli validate <project>                      check every scene's references; exit 1 on problems
//   noor_cli migrate <project> [--dry-run]           upgrade scenes to the current format (keeps .bak)
//   noor_cli make-prefab <project> <scene> <entity name> <prefab path>
//                                                    turn an entity's subtree into a prefab and an instance
//   noor_cli add-prefab <project> <prefab> <scene> [x y z [yaw°]]
//                                                    place an instance of a prefab in a scene
//   noor_cli build <project> <output folder> [--scene <path>]
//   noor_cli generate <project> <graph> [--out <folder>] [--threads <n>] [--no-cache]
//                                                    run a procedural graph (Milestone 7): write its outputs
//                                                    and a fingerprint file; results cached in .noor/cache/procgraph
//                                                    a runnable game: the player + the project's content
//
// <project> is the project folder or its .noorproj file; <asset> and <scene> are project-relative.

#include "noor/asset/gltf_import.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/asset/material.h"
#include "noor/asset/texture.h"
#include "noor/foundation/log.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"

#include "noor/asset/json.h"
#include "noor/csg/scene_csg.h"
#include "noor/game/cooked_csg.h"
#include "noor/procgraph/procgraph.h"

#include <chrono>
#include "noor/foundation/file_io.h"
#include "noor/foundation/guid.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>  // GetModuleFileNameW
#endif

namespace {
namespace stdfs = std::filesystem;
using namespace noor;

int usage() {
    std::fputs("usage:\n"
               "  noor_cli new <folder> <name>\n"
               "  noor_cli scan <project>\n"
               "  noor_cli info <file.glb>\n"
               "  noor_cli texture <image files...>\n"
               "  noor_cli make-material <project> <texture path>\n"
               "  noor_cli add-model <project> <asset path> <scene path>\n"
               "  noor_cli validate <project>\n"
               "  noor_cli migrate <project> [--dry-run]\n"
               "  noor_cli make-prefab <project> <scene> <entity name> <prefab path>\n"
               "  noor_cli add-prefab <project> <prefab> <scene> [x y z [yaw degrees]]\n"
               "  noor_cli build <project> <output folder> [--scene <path>]\n"
               "  noor_cli cook <project>\n"
               "  noor_cli generate <project> <graph path> [--out <folder>] [--threads <n>] [--no-cache]\n",
               stderr);
    return 64;
}

int report(const Error& e) {
    NOOR_LOG_ERROR("CLI", "{}", e.describe());
    return 1;
}

int cmd_new(const std::vector<std::string>& a) {
    if (a.size() != 2) return usage();
    auto p = asset::Project::create(a[0], a[1]);
    if (!p) return report(p.error());
    std::printf("Created project '%s' (%s)\n", p->info().name.c_str(), p->file().generic_string().c_str());
    return 0;
}

int cmd_scan(const std::vector<std::string>& a) {
    if (a.size() != 1) return usage();
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    asset::AssetRegistry registry;
    auto r = registry.scan(*p);
    if (!r) return report(r.error());
    std::printf("%d asset(s): %d new, %d moved, %d changed, %zu orphaned meta(s), %zu warning(s)\n", r->assets,
                r->created, r->moved, r->content_changed, r->orphaned_metas.size(), r->warnings.size());
    for (const auto& a2 : registry.assets()) {
        std::printf("  %s  %s%s\n", a2.guid.to_string().c_str(), a2.path.c_str(),
                    a2.from_interchange ? "  (id from Noor Interchange manifest)" : "");
    }
    for (const auto& o : r->orphaned_metas) std::printf("  orphaned: %s\n", o.c_str());
    return r->warnings.empty() ? 0 : 2;
}

// One line per texture: what import would do with it. Exit 1 if any can't be read.
int cmd_texture(const std::vector<std::string>& a) {
    if (a.empty()) return usage();
    int failed = 0;
    std::uint64_t total = 0;
    for (const auto& file : a) {
        const auto settings = asset::texture_settings_for(file);
        const auto start = std::chrono::steady_clock::now();
        auto t = asset::import_texture(file, settings);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!t) {
            std::printf("FAIL  %s: %s\n", file.c_str(), t.error().message.c_str());
            ++failed;
            continue;
        }
        total += t->gpu_bytes();
        std::printf("%-6s %-24s %5ux%-5u %2zu mips %-10s %7.2f MB %6.0f ms  %s\n", std::string(asset::kind_name(settings.kind)).c_str(),
                    settings.kind == asset::TextureKind::Normal ? (settings.directx_normals ? "(directx, green flipped)" : "(opengl)") : "",
                    t->width(), t->height(), t->mips.size(), std::string(asset::format_name(t->format)).c_str(),
                    static_cast<double>(t->gpu_bytes()) / (1024.0 * 1024.0), ms,
                    std::filesystem::path(file).filename().string().c_str());
    }
    std::printf("%zu texture(s), %.1f MB of GPU memory, %d failed\n", a.size() - static_cast<std::size_t>(failed),
                static_cast<double>(total) / (1024.0 * 1024.0), failed);
    return failed ? 1 : 0;
}

// make-material: finds the set around one of its maps, writes <set>.noormat beside the maps (an
// existing one is kept, not overwritten), and gives it a .noormeta.
int cmd_make_material(const std::vector<std::string>& a) {
    if (a.size() != 2) return usage();
    auto project = asset::Project::open(a[0]);
    if (!project) return report(project.error());
    asset::AssetRegistry registry;
    if (auto s = registry.scan(*project); !s) return report(s.error());
    const auto texture = project->resolve(a[1]);
    if (!texture || !std::filesystem::exists(*texture)) {
        std::fprintf(stderr, "no texture at %s\n", a[1].c_str());
        return 1;
    }
    const auto set = asset::find_texture_set(*texture);
    const auto out = texture->parent_path() / (set.name + std::string(asset::kMaterialExtension));
    for (const auto& [role, file] : set.maps)
        std::printf("  %-18s %s\n", std::string(asset::role_name(role)).c_str(), file.filename().string().c_str());
    if (std::filesystem::exists(out)) {
        std::printf("%s already exists: left as it is\n", out.filename().string().c_str());
        return 0;
    }
    const auto json = asset::material_json_for(set, [&](const std::filesystem::path& f) -> std::optional<Guid> {
        const auto rel = project->relative(f);
        const auto* rec = rel ? registry.find_path(*rel) : nullptr;
        return rec ? std::optional(rec->guid) : std::nullopt;
    });
    if (auto w = fs::write_file_atomic(out, asset::to_text(json), false); !w) return report(w.error());
    if (auto s = registry.scan(*project); !s) return report(s.error());
    const auto rel = project->relative(out);
    std::printf("Material written: %s\n", rel ? rel->c_str() : out.generic_string().c_str());
    return 0;
}

int cmd_info(const std::vector<std::string>& a) {
    if (a.size() != 1) return usage();
    auto m = asset::import_gltf(a[0]);
    if (!m) return report(m.error());
    std::map<std::string, int> markers;
    int collision = 0;
    for (const auto& n : m->nodes) {
        if (n.is_marker()) ++markers[n.noor.value("marker", std::string("?"))];
        if (n.is_collision()) ++collision;
    }
    std::printf("%s\n  nodes %zu, meshes %zu\n  render triangles %llu, collision triangles %llu (%d node(s))\n",
                a[0].c_str(), m->nodes.size(), m->meshes.size(),
                static_cast<unsigned long long>(m->render_triangles()),
                static_cast<unsigned long long>(m->collision_triangles()), collision);
    for (const auto& [type, count] : markers) std::printf("  marker %s: %d\n", type.c_str(), count);
    for (const auto& w : m->warnings) std::printf("  warning: %s\n", w.c_str());
    return 0;
}

int cmd_add_model(const std::vector<std::string>& a) {
    if (a.size() != 3) return usage();
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    asset::AssetRegistry registry;
    if (auto r = registry.scan(*p); !r) return report(r.error());
    const asset::AssetRecord* rec = registry.find_path(a[1]);
    if (!rec) {
        NOOR_LOG_ERROR("CLI", "'{}' is not a known asset (run scan; paths are project-relative, like Assets/x.glb)", a[1]);
        return 1;
    }
    auto source = p->resolve(rec->path);
    auto scene_path = p->resolve(a[2]);
    if (!source) return report(source.error());
    if (!scene_path) return report(scene_path.error());
    auto model = asset::import_gltf(*source);
    if (!model) return report(model.error());

    scene::Scene scene;
    if (stdfs::exists(*scene_path)) {
        auto loaded = scene::Scene::load(*scene_path);
        if (!loaded) return report(loaded.error());
        scene = std::move(*loaded);
    } else {
        scene.name = scene_path->stem().string();
    }
    const auto root = scene::instantiate_model(scene, rec->guid, *model, stdfs::path(rec->path).stem().string());
    if (auto s = scene.save(*scene_path); !s) return report(s.error());
    std::printf("Added '%s' to %s as entity %s (%zu entities in the scene)\n", rec->path.c_str(), a[2].c_str(),
                root.to_string().c_str(), scene.entities.size());
    return 0;
}

// Cooks every CSG model of every scene (ADR-016) into `root`/.noor/cooked/csg: what games load.
// Returns the number of models cooked, or -1 if one failed.
int cook_project(const asset::Project& p, const asset::AssetRegistry& registry, const stdfs::path& root) {
    int cooked = 0;
    std::error_code ec;
    for (auto it = stdfs::recursive_directory_iterator(p.scenes_dir(), ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".noorscene") continue;
        auto scene = scene::Scene::load(it->path());
        if (!scene) continue;  // validate reports it
        scene::PrefabLibrary prefabs(p, registry);
        const auto expanded = scene::expand_prefabs(*scene, prefabs.resolver()).scene;
        for (std::size_t i = 0; i < expanded.entities.size(); ++i) {
            if (!expanded.entities[i].has("csg_model")) continue;
            std::vector<std::string> warnings;
            auto model = csg::cook_model(expanded, i, nullptr, &warnings);
            for (const auto& w : warnings) NOOR_LOG_WARN("CLI", "{}: CSG model {}: {}", it->path().filename().string(), expanded.entities[i].name, w);
            if (!model) {
                NOOR_LOG_ERROR("CLI", "{}: CSG model {}: {}", it->path().filename().string(), expanded.entities[i].name, model.error().describe());
                return -1;
            }
            if (auto w = game::write_cooked_csg(game::cooked_csg_path(root, expanded.entities[i].id), *model); !w) {
                report(w.error());
                return -1;
            }
            ++cooked;
        }
    }
    return cooked;
}

int cmd_cook(const std::vector<std::string>& a) {
    if (a.size() != 1) return usage();
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    asset::AssetRegistry registry;
    if (auto r = registry.scan(*p); !r) return report(r.error());
    const int n = cook_project(*p, registry, p->root());
    if (n < 0) return 1;
    std::printf("Cooked %d CSG model(s) into %s\n", n, (p->root() / ".noor" / "cooked" / "csg").generic_string().c_str());
    return 0;
}

int cmd_validate(const std::vector<std::string>& a) {
    if (a.size() != 1) return usage();
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    asset::AssetRegistry registry;
    auto r = registry.scan(*p);
    if (!r) return report(r.error());

    int scenes = 0;
    int problems = static_cast<int>(r->warnings.size());
    std::error_code ec;
    for (auto it = stdfs::recursive_directory_iterator(p->scenes_dir(), ec);
         !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".noorscene") continue;
        ++scenes;
        auto rel = p->relative(it->path());
        auto scene = scene::Scene::load(it->path());
        if (!scene) {
            std::printf("FAIL %s: %s\n", rel ? rel->c_str() : "?", scene.error().describe().c_str());
            ++problems;
            continue;
        }
        // Validate the scene as it will run: with every prefab expanded.
        scene::PrefabLibrary prefabs(*p, registry);
        const auto expanded = scene::expand_prefabs(*scene, prefabs.resolver());
        auto issues = scene::validate(expanded.scene, [&](const Guid& g) { return registry.find(g) != nullptr; });
        for (const auto& e : expanded.issues) {
            if (e.error) {
                issues.push_back({e.entity, e.message});
            } else {
                std::printf("     warning: %s: %s\n", e.entity.c_str(), e.message.c_str());
            }
        }
        for (const auto& e : prefabs.errors()) issues.push_back({"prefab file", e});
        std::printf("%s %s: %zu entities (%zu after expanding %zu prefab instance(s)), %zu issue(s)%s\n",
                    issues.empty() ? "OK  " : "FAIL", rel ? rel->c_str() : "?", scene->entities.size(),
                    expanded.scene.entities.size(), expanded.instances, issues.size(),
                    scene->migrations_applied.empty() ? "" : "  (older format: run 'noor_cli migrate')");
        // Group identical messages (one missing building = many entities pointing at it).
        std::map<std::string, std::vector<std::string>> grouped;
        for (const auto& i : issues) grouped[i.message].push_back(i.entity);
        for (const auto& [message, entities] : grouped) {
            if (entities.size() == 1) {
                std::printf("     %s: %s\n", entities[0].c_str(), message.c_str());
            } else {
                std::printf("     %zu entities (first: %s): %s\n", entities.size(), entities[0].c_str(), message.c_str());
            }
        }
        problems += static_cast<int>(issues.size());
    }
    std::printf("%s: %zu asset(s), %d scene(s), %d problem(s)\n", p->info().name.c_str(), registry.assets().size(),
                scenes, problems);
    return problems == 0 ? 0 : 1;
}

int cmd_make_prefab(const std::vector<std::string>& a) {
    if (a.size() != 4) return usage();
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    auto scene_path = p->resolve(a[1]);
    auto prefab_path = p->resolve(a[3]);
    if (!scene_path) return report(scene_path.error());
    if (!prefab_path) return report(prefab_path.error());
    if (prefab_path->extension() != scene::kPrefabExtension) {
        NOOR_LOG_ERROR("CLI", "Prefab files end in {}", scene::kPrefabExtension);
        return 1;
    }
    if (stdfs::exists(*prefab_path)) {
        NOOR_LOG_ERROR("CLI", "'{}' already exists", a[3]);
        return 1;
    }
    auto s = scene::Scene::load(*scene_path);
    if (!s) return report(s.error());
    const auto it = std::ranges::find_if(s->entities, [&](const scene::Entity& e) {
        return e.name == a[2] || e.id.to_string() == a[2];
    });
    if (it == s->entities.end()) {
        NOOR_LOG_ERROR("CLI", "No entity named '{}' in {}", a[2], a[1]);
        return 1;
    }
    auto prefab = scene::make_prefab(*s, it->id, prefab_path->stem().string(), /*replace_with_instance=*/true);
    if (!prefab) return report(prefab.error());
    if (auto st = prefab->save(*prefab_path); !st) return report(st.error());
    if (auto st = s->save(*scene_path); !st) return report(st.error());
    asset::AssetRegistry registry;
    if (auto r = registry.scan(*p); !r) return report(r.error());
    std::printf("Created prefab %s (%zu entities, id %s); '%s' in %s is now an instance of it\n", a[3].c_str(),
                prefab->entities.size(), prefab->id.to_string().c_str(), a[2].c_str(), a[1].c_str());
    return 0;
}

int cmd_add_prefab(const std::vector<std::string>& a) {
    if (a.size() != 3 && a.size() != 6 && a.size() != 7) return usage();
    float v[4] = {0, 0, 0, 0};
    for (std::size_t i = 3; i < a.size(); ++i) {
        const auto r = std::from_chars(a[i].data(), a[i].data() + a[i].size(), v[i - 3]);
        if (r.ec != std::errc{}) {
            NOOR_LOG_ERROR("CLI", "'{}' is not a number", a[i]);
            return 64;
        }
    }
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    asset::AssetRegistry registry;
    if (auto r = registry.scan(*p); !r) return report(r.error());
    const asset::AssetRecord* rec = registry.find_path(a[1]);
    if (!rec || rec->importer != "prefab") {
        NOOR_LOG_ERROR("CLI", "'{}' is not a prefab in this project", a[1]);
        return 1;
    }
    auto scene_path = p->resolve(a[2]);
    if (!scene_path) return report(scene_path.error());
    scene::Scene s;
    if (stdfs::exists(*scene_path)) {
        auto loaded = scene::Scene::load(*scene_path);
        if (!loaded) return report(loaded.error());
        s = std::move(*loaded);
    } else {
        s.name = scene_path->stem().string();
    }
    scene::Transform placement;
    placement.position = {v[0], v[1], v[2]};
    placement.rotation = math::Quat::from_axis_angle({0, 1, 0}, math::radians(v[3]));
    const std::string name = stdfs::path(rec->path).stem().string() + " " + std::to_string(s.entities.size() + 1);
    s.entities.push_back(scene::make_instance(rec->guid, name, placement));
    if (auto st = s.save(*scene_path); !st) return report(st.error());
    std::printf("Placed '%s' as '%s' at (%g, %g, %g), yaw %g\n", a[1].c_str(), name.c_str(), v[0], v[1], v[2], v[3]);
    return 0;
}

int cmd_migrate(const std::vector<std::string>& a) {
    if (a.empty() || a.size() > 2 || (a.size() == 2 && a[1] != "--dry-run")) return usage();
    const bool dry = a.size() == 2;
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    int upgraded = 0, failed = 0;
    std::error_code ec;
    for (auto it = stdfs::recursive_directory_iterator(p->scenes_dir(), ec);
         !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".noorscene") continue;
        auto rel = p->relative(it->path());
        const char* name = rel ? rel->c_str() : "?";
        auto scene = scene::Scene::load(it->path());
        if (!scene) {
            std::printf("FAIL %s: %s\n", name, scene.error().describe().c_str());
            ++failed;
            continue;
        }
        if (scene->migrations_applied.empty()) continue;
        for (const auto& step : scene->migrations_applied) {
            std::printf("%s %s: %s\n", dry ? "would upgrade" : "upgraded", name, step.c_str());
        }
        if (!dry) {
            if (auto s = scene->save(it->path()); !s) {
                std::printf("FAIL %s: %s\n", name, s.error().describe().c_str());
                ++failed;
                continue;
            }
        }
        ++upgraded;
    }
    std::printf("%d scene(s) %s%s\n", upgraded, dry ? "need upgrading (dry run, nothing written)" : "upgraded",
                dry || upgraded == 0 ? "" : "; previous versions kept as .bak");
    return failed == 0 ? 0 : 1;
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

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
#endif

// Runs a command line (arguments already quoted) and returns its exit code and everything it printed.
std::pair<int, std::string> run_command(const stdfs::path& program, const std::vector<std::wstring>& args) {
    std::string output;
#if defined(_WIN32)
    std::wstring line = L"\"" + program.wstring() + L"\"";
    for (const auto& a : args) line += L" \"" + a + L"\"";
    // cmd.exe /c strips the outer quotes when there are more than two: wrap the whole line once more.
    const std::wstring full = L"\"" + line + L" 2>&1\"";
    std::FILE* pipe = _wpopen(full.c_str(), L"r");
    if (!pipe) return {-1, "can't start " + program.string()};
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
    return {_pclose(pipe), output};
#else
    (void)program;
    (void)args;
    return {-1, "not supported on this platform yet"};
#endif
}

std::optional<stdfs::path> find_dotnet() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD n = SearchPathW(nullptr, L"dotnet.exe", nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (n > 0 && n < buffer.size()) return stdfs::path(buffer.substr(0, n));
#endif
    return std::nullopt;
}

std::string xml_escape(std::string s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

// Compiles the project's C# scripts (Scripts/**/*.cs) into `out_dir`/Scripts.dll (Release, with a
// .pdb for line numbers) against the engine's NoorEngine.Scripting in `managed`. Uses a throw-away
// .csproj in the temp folder, so nothing is written into the project. Compile errors are printed.
bool compile_scripts(const asset::Project& project, const stdfs::path& managed, const stdfs::path& out_dir) {
    const auto dotnet = find_dotnet();
    if (!dotnet) {
        NOOR_LOG_ERROR("CLI", "C# scripts need the .NET 10 SDK to build (the dotnet command wasn't found)");
        return false;
    }
    std::error_code ec;
    const stdfs::path work = stdfs::temp_directory_path() / ("noor_scripts_" + Guid::generate().to_string());
    stdfs::create_directories(work, ec);
    const std::string scripts = (project.root() / "Scripts").generic_string();
    const std::string csproj = std::format(R"(<Project Sdk="Microsoft.NET.Sdk">
  <!-- Generated by noor_cli build (throw-away): compiles the project's C# scripts for a game. -->
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <AssemblyName>Scripts</AssemblyName>
    <RootNamespace>Scripts</RootNamespace>
    <DebugType>portable</DebugType>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="{0}/**/*.cs" Exclude="{0}/bin/**;{0}/obj/**" />
    <Reference Include="NoorEngine.Scripting">
      <HintPath>{1}</HintPath>
      <Private>false</Private>
    </Reference>
  </ItemGroup>
</Project>
)",
                                           xml_escape(scripts), xml_escape((managed / "NoorEngine.Scripting.dll").generic_string()));
    if (auto w = fs::write_file_atomic(work / "Scripts.csproj", csproj); !w) {
        report(w.error());
        return false;
    }
#if defined(_WIN32)
    _wputenv_s(L"DOTNET_CLI_TELEMETRY_OPTOUT", L"1");
    _wputenv_s(L"DOTNET_NOLOGO", L"1");
    _wputenv_s(L"DOTNET_CLI_FORCE_UTF8_ENCODING", L"1");  // compiler messages in UTF-8 (Polish ones were garbled)
#endif
    std::printf("Compiling C# scripts...\n");
    std::fflush(stdout);
    const auto [code, output] = run_command(*dotnet, {L"build", (work / "Scripts.csproj").wstring(), L"-c", L"Release", L"-nologo",
                                                      L"-v", L"quiet", L"-clp:NoSummary", L"-o", out_dir.wstring()});
    stdfs::remove_all(work, ec);
    if (code == 0 && stdfs::exists(out_dir / "Scripts.dll", ec)) return true;
    // "E:\Game\Scripts\Spinner.cs(7,10): error CS1002: ; expected [C:\...\Scripts.csproj]"
    //   -> "Scripts/Spinner.cs:7: CS1002: ; expected" (the editor's format; MSBuild prints each twice)
    static const std::regex pattern(R"(^\s*(.+?)\((\d+),\d+\): error (CS\d+): (.*?)(?: \[[^\]]*\])?\s*$)");
    std::vector<std::string> shown;
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        const std::string line = output.substr(start, end - start);
        start = end + 1;
        std::smatch m;
        if (!std::regex_match(line, m, pattern)) continue;
        const auto rel = project.relative(stdfs::path(m[1].str()));
        const std::string text = std::format("{}:{}: {}: {}", rel ? *rel : m[1].str(), m[2].str(), m[3].str(), m[4].str());
        if (std::ranges::find(shown, text) != shown.end()) continue;
        NOOR_LOG_ERROR("CLI", "{}", text);
        shown.push_back(text);
    }
    if (shown.empty()) NOOR_LOG_ERROR("CLI", "Compiling the C# scripts failed ({}): {}", code, output);
    return false;
}

// What a build leaves out: derived data, editor leftovers, IDE build output.
bool excluded(const stdfs::path& rel) {
    const std::string first = rel.begin() != rel.end() ? rel.begin()->string() : std::string{};
    if (first == ".noor" || first == ".git" || first == ".vs") return true;
    if (first == "Scripts" && rel.has_parent_path()) {
        const auto second = std::next(rel.begin());
        if (second != rel.end() && (second->string() == "bin" || second->string() == "obj")) return true;
    }
    const std::string ext = rel.extension().string();
    return ext == ".bak" || ext == ".tmp";
}

// A file name for the game's executable: the project name without characters Windows refuses.
std::string executable_name(const std::string& project_name) {
    std::string out;
    for (const char c : project_name) out += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? std::string("Game") : out;
}

// noor_cli build: the player executable renamed after the project, the project's content in game/,
// and game.json (which scene to start, the window title). The folder runs on any Windows PC with a
// Vulkan 1.3 GPU; nothing is installed.
int cmd_build(const std::vector<std::string>& a) {
    if (a.size() != 2 && !(a.size() == 4 && a[2] == "--scene")) return usage();
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    const stdfs::path out = stdfs::absolute(a[1]).lexically_normal();
    const auto rel_out = out.lexically_relative(p->root());
    if (!rel_out.empty() && rel_out.begin()->string() != "..") {
        NOOR_LOG_ERROR("CLI", "The output folder can't be inside the project ({})", out.generic_string());
        return 1;
    }
    std::error_code ec;
    if (stdfs::exists(out, ec) && !stdfs::is_empty(out, ec) && !stdfs::exists(out / "game.json", ec)) {
        NOOR_LOG_ERROR("CLI", "'{}' isn't empty and isn't an earlier build: choose an empty or new folder", out.generic_string());
        return 1;
    }

    // Check the project first: a build of a broken project is a broken game.
    if (cmd_validate({a[0]}) != 0) {
        NOOR_LOG_ERROR("CLI", "Build stopped: fix the problems above first");
        return 1;
    }
    std::string start = a.size() == 4 ? a[3] : p->default_start_scene().value_or("");
    if (start.empty()) {
        NOOR_LOG_ERROR("CLI", "'{}' has no scenes to start with", p->info().name);
        return 1;
    }
    auto start_path = p->resolve(start);
    if (!start_path || !stdfs::exists(*start_path, ec)) {
        NOOR_LOG_ERROR("CLI", "Start scene '{}' not found", start);
        return 1;
    }
    const stdfs::path player = executable_dir() / "noor_player.exe";
    if (!stdfs::exists(player, ec)) {
        NOOR_LOG_ERROR("CLI", "noor_player.exe isn't next to noor_cli ({})", player.generic_string());
        return 1;
    }

    // C# scripts are compiled first, into a temp folder: a compile error stops the build before
    // anything in the output folder is touched.
    bool csharp = false;
    for (auto it = stdfs::recursive_directory_iterator(p->root() / "Scripts", ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        if (excluded(it->path().lexically_relative(p->root()))) {
            if (it->is_directory()) it.disable_recursion_pending();
        } else if (it->path().extension() == ".cs") {
            csharp = true;
        }
    }
    ec.clear();
    const stdfs::path compiled = stdfs::temp_directory_path() / ("noor_build_" + Guid::generate().to_string());
    struct RemoveOnExit {
        stdfs::path dir;
        ~RemoveOnExit() {
            std::error_code ignored;
            stdfs::remove_all(dir, ignored);
        }
    } cleanup{compiled};
    const stdfs::path managed = executable_dir() / "managed";
    if (csharp) {
        if (!stdfs::exists(managed / "NoorEngine.Scripting.dll", ec)) {
            NOOR_LOG_ERROR("CLI", "The project has C# scripts, but this noor_cli has no managed scripting next to it "
                                  "(the engine was built without the .NET SDK)");
            return 1;
        }
        if (!compile_scripts(*p, managed, compiled)) {
            NOOR_LOG_ERROR("CLI", "Build stopped: the C# scripts don't compile");
            return 1;
        }
    }

    // An earlier build is replaced; nothing else in the folder is touched.
    const std::string exe = executable_name(p->info().name) + ".exe";
    if (stdfs::exists(out / "game.json", ec)) {
        stdfs::remove_all(out / "game", ec);
        if (auto old = asset::parse_json(fs::read_file(out / "game.json").value_or("{}"), "game.json"); old && old->contains("executable"))
            stdfs::remove(out / (*old)["executable"].get<std::string>(), ec);
    }
    stdfs::create_directories(out / "game", ec);
    if (ec) return report(Error{ErrorCode::IoError, std::format("can't create {}: {}", (out / "game").generic_string(), ec.message())});

    std::size_t files = 0;
    std::uintmax_t bytes = 0;
    for (auto it = stdfs::recursive_directory_iterator(p->root(), ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        const stdfs::path rel = it->path().lexically_relative(p->root());
        if (excluded(rel)) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        const stdfs::path dest = out / "game" / rel;
        if (it->is_directory()) {
            stdfs::create_directories(dest, ec);
            continue;
        }
        if (rel.extension() == ".cs" || rel.extension() == ".csproj") continue;  // compiled above; the sources stay home
        stdfs::copy_file(it->path(), dest, stdfs::copy_options::overwrite_existing, ec);
        if (ec) return report(Error{ErrorCode::IoError, std::format("can't copy {}: {}", rel.generic_string(), ec.message())});
        ++files;
        bytes += stdfs::file_size(it->path(), ec);
    }
    if (ec) return report(Error{ErrorCode::IoError, ec.message()});
    // C# scripts: the compiled Scripts.dll with the engine's managed half (the player starts .NET with it).
    stdfs::remove_all(out / "managed", ec);
    if (csharp) {
        stdfs::create_directories(out / "managed", ec);
        for (const auto& from : {compiled, managed}) {
            for (stdfs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
                if (it->is_regular_file()) stdfs::copy_file(it->path(), out / "managed" / it->path().filename(), stdfs::copy_options::overwrite_existing, ec);
                if (ec) return report(Error{ErrorCode::IoError, std::format("can't copy {}: {}", it->path().filename().string(), ec.message())});
            }
        }
    }
    // Licences of what the engine builds into every game (the HUD font).
    if (stdfs::exists(executable_dir() / "licenses", ec)) {
        stdfs::create_directories(out / "licenses", ec);
        for (stdfs::directory_iterator it(executable_dir() / "licenses", ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file()) stdfs::copy_file(it->path(), out / "licenses" / it->path().filename(), stdfs::copy_options::overwrite_existing, ec);
        ec.clear();
    }
    stdfs::copy_file(player, out / exe, stdfs::copy_options::overwrite_existing, ec);
    if (ec) return report(Error{ErrorCode::IoError, std::format("can't copy the player: {}", ec.message())});

    // CSG models are cooked fresh into the game (the project's own .noor isn't packed).
    {
        asset::AssetRegistry registry;
        if (auto r = registry.scan(*p); !r) return report(r.error());
        const int models = cook_project(*p, registry, out / "game");
        if (models < 0) {
            NOOR_LOG_ERROR("CLI", "Build stopped: a CSG model couldn't be cooked");
            return 1;
        }
        if (models > 0) std::printf("  %d CSG model(s) cooked\n", models);
    }

    const asset::Json manifest{{"noor_game", 1},
                               {"title", p->info().name},
                               {"project", "game"},
                               {"start_scene", start},
                               {"executable", exe},
                               {"engine", p->info().engine_version},
                               {"built", asset::utc_now_iso8601()}};
    asset::Json game_manifest = manifest;
    if (csharp) game_manifest["csharp"] = "managed/Scripts.dll";
    if (auto w = fs::write_file_atomic(out / "game.json", game_manifest.dump(2) + "\n"); !w) return report(w.error());

#if !defined(NDEBUG)
    NOOR_LOG_WARN("CLI", "This is a Debug build of the engine tools: the game needs Visual Studio's debug runtime and only runs "
                         "on development PCs. Use the Release noor_cli to make a game to share");
#endif
    if (csharp) std::printf("  C# scripts compiled into managed/Scripts.dll (the PC needs the .NET 10 runtime)\n");
    std::printf("Built '%s' into %s\n  %s  (start scene %s)\n  %zu project files, %.1f MB\n", p->info().name.c_str(),
                out.generic_string().c_str(), exe.c_str(), start.c_str(), files, static_cast<double>(bytes) / (1024.0 * 1024.0));
    return 0;
}

// Runs a graph headless: the same code and the same bytes as the editor (ADR-014).
int cmd_generate(const std::vector<std::string>& a) {
    if (a.size() < 2) return usage();
    auto p = asset::Project::open(a[0]);
    if (!p) return report(p.error());
    auto graph_file = p->resolve(a[1]);
    if (!graph_file) return report(graph_file.error());
    stdfs::path out = p->root() / "Generated" / graph_file->stem();
    unsigned threads = 0;
    bool cache = true;
    for (std::size_t i = 2; i < a.size(); ++i) {
        if (a[i] == "--out" && i + 1 < a.size()) out = a[++i];
        else if (a[i] == "--threads" && i + 1 < a.size()) threads = static_cast<unsigned>(std::max(1, std::atoi(a[++i].c_str())));
        else if (a[i] == "--no-cache") cache = false;
        else return usage();
    }
    auto graph = procgraph::Graph::load(*graph_file);
    if (!graph) return report(graph.error());
    procgraph::Cache results(p->root() / ".noor" / "cache" / "procgraph");
    procgraph::GenerateOptions opts;
    opts.out = out;
    opts.name = graph_file->stem().string();
    opts.run.cache = cache ? &results : nullptr;
    opts.run.threads = threads;
    const auto t0 = std::chrono::steady_clock::now();
    auto r = procgraph::generate(*graph, procgraph::Registry::builtin(), opts);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!r) {
        // Show what went wrong on which node, then the reason nothing was written.
        const auto ev = procgraph::evaluate(*graph, procgraph::Registry::builtin(), opts.run);
        for (const auto& i : ev.issues) std::printf("  %s%s: %s\n", i.node.empty() ? "(graph)" : i.node.c_str(), i.pin.empty() ? "" : ("." + i.pin).c_str(), i.message.c_str());
        return report(r.error());
    }
    for (const auto& i : r->evaluation.issues) std::printf("  warning: %s: %s\n", i.node.empty() ? "(graph)" : i.node.c_str(), i.message.c_str());
    std::printf("Generated '%s': %zu node(s) ran, %zu from the cache, %.0f ms\n  fingerprint %016llx\n", opts.name.c_str(), r->evaluation.ran,
                r->evaluation.cached, ms, static_cast<unsigned long long>(r->fingerprint));
    for (const auto& f : r->files) std::printf("  %s\n", f.generic_string().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    noor::log::add_sink(std::make_shared<noor::ConsoleLogSink>());
    noor::log::set_min_level(noor::LogLevel::Warn);
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);
    if (cmd == "new") return cmd_new(args);
    if (cmd == "scan") return cmd_scan(args);
    if (cmd == "info") return cmd_info(args);
    if (cmd == "texture") return cmd_texture(args);
    if (cmd == "make-material") return cmd_make_material(args);
    if (cmd == "add-model") return cmd_add_model(args);
    if (cmd == "validate") return cmd_validate(args);
    if (cmd == "migrate") return cmd_migrate(args);
    if (cmd == "make-prefab") return cmd_make_prefab(args);
    if (cmd == "add-prefab") return cmd_add_prefab(args);
    if (cmd == "build") return cmd_build(args);
    if (cmd == "cook") return cmd_cook(args);
    if (cmd == "generate") return cmd_generate(args);
    return usage();
}
