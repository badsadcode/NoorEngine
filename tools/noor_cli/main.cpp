// noor_cli: headless NoorEngine project tool. No window, no GPU: runs in CI and scripts.
//
//   noor_cli new <folder> <name>                     create a project
//   noor_cli scan <project>                          create/repair .noormeta files under Assets/
//   noor_cli info <file.glb>                         summarize a GLB (nodes, meshes, markers)
//   noor_cli add-model <project> <asset> <scene>     instantiate an asset into a scene (creates it)
//   noor_cli validate <project>                      check every scene's references; exit 1 on problems
//   noor_cli migrate <project> [--dry-run]           upgrade scenes to the current format (keeps .bak)
//
// <project> is the project folder or its .noorproj file; <asset> and <scene> are project-relative.

#include "noor/asset/gltf_import.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/log.h"
#include "noor/scene/scene.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
               "  noor_cli migrate <project> [--dry-run]\n",
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
        const auto issues = scene::validate(*scene, [&](const Guid& g) { return registry.find(g) != nullptr; });
        std::printf("%s %s: %zu entities, %zu issue(s)%s\n", issues.empty() ? "OK  " : "FAIL",
                    rel ? rel->c_str() : "?", scene->entities.size(), issues.size(),
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
    return usage();
}
