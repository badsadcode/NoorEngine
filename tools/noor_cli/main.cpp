// noor_cli: headless NoorEngine project tool. No window, no GPU: runs in CI and scripts.
//
//   noor_cli new <folder> <name>                     create a project
//   noor_cli scan <project>                          create/repair .noormeta files under Assets/
//   noor_cli info <file.glb>                         summarize a GLB (nodes, meshes, markers)
//   noor_cli add-model <project> <asset> <scene>     instantiate an asset into a scene (creates it)
//   noor_cli validate <project>                      check every scene's references; exit 1 on problems
//   noor_cli migrate <project> [--dry-run]           upgrade scenes to the current format (keeps .bak)
//   noor_cli make-prefab <project> <scene> <entity name> <prefab path>
//                                                    turn an entity's subtree into a prefab and an instance
//   noor_cli add-prefab <project> <prefab> <scene> [x y z [yaw°]]
//                                                    place an instance of a prefab in a scene
//   noor_cli build <project> <output folder> [--scene <path>]
//                                                    a runnable game: the player + the project's content
//
// <project> is the project folder or its .noorproj file; <asset> and <scene> are project-relative.

#include "noor/asset/gltf_import.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/log.h"
#include "noor/scene/prefab.h"
#include "noor/scene/scene.h"

#include "noor/asset/json.h"
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
               "  noor_cli add-model <project> <asset path> <scene path>\n"
               "  noor_cli validate <project>\n"
               "  noor_cli migrate <project> [--dry-run]\n"
               "  noor_cli make-prefab <project> <scene> <entity name> <prefab path>\n"
               "  noor_cli add-prefab <project> <prefab> <scene> [x y z [yaw degrees]]\n"
               "  noor_cli build <project> <output folder> [--scene <path>]\n",
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
    stdfs::copy_file(player, out / exe, stdfs::copy_options::overwrite_existing, ec);
    if (ec) return report(Error{ErrorCode::IoError, std::format("can't copy the player: {}", ec.message())});

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
    if (cmd == "add-model") return cmd_add_model(args);
    if (cmd == "validate") return cmd_validate(args);
    if (cmd == "migrate") return cmd_migrate(args);
    if (cmd == "make-prefab") return cmd_make_prefab(args);
    if (cmd == "add-prefab") return cmd_add_prefab(args);
    if (cmd == "build") return cmd_build(args);
    return usage();
}
