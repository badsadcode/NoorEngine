#include "noor/asset/project_template.h"

#include "noor/asset/json.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"

#include <algorithm>
#include <format>

namespace noor::asset {

namespace stdfs = std::filesystem;

Result<ProjectTemplate> load_template(const stdfs::path& folder) {
    const auto file = folder / "template.json";
    auto text = fs::read_file(file, 1u << 20);
    if (!text) return std::unexpected(Error{ErrorCode::NotFound, std::format("'{}' isn't a project template (no template.json)", folder.generic_string())});
    auto j = parse_json(*text, file.generic_string());
    if (!j) return std::unexpected(j.error());
    if (!j->is_object() || j->value("noor_template", 0) != 1 || !j->contains("name") || !(*j)["name"].is_string())
        return std::unexpected(Error{ErrorCode::ParseError, std::format("{}: needs \"noor_template\": 1 and a \"name\"", file.generic_string())});
    ProjectTemplate t;
    t.id = folder.filename().string();
    t.name = (*j)["name"].get<std::string>();
    t.description = j->value("description", std::string());
    t.order = j->contains("order") && (*j)["order"].is_number_integer() ? (*j)["order"].get<int>() : 100;
    t.folder = stdfs::absolute(folder).lexically_normal();
    return t;
}

std::vector<ProjectTemplate> list_templates(const stdfs::path& dir) {
    std::vector<ProjectTemplate> out;
    std::error_code ec;
    for (auto it = stdfs::directory_iterator(dir, ec); !ec && it != stdfs::directory_iterator(); it.increment(ec)) {
        if (!it->is_directory()) continue;
        if (auto t = load_template(it->path())) out.push_back(std::move(*t));
        else NOOR_LOG_WARN("Asset", "{}", t.error().describe());
    }
    std::ranges::sort(out, [](const ProjectTemplate& a, const ProjectTemplate& b) { return std::tie(a.order, a.name) < std::tie(b.order, b.name); });
    return out;
}

Result<Project> create_project_from_template(const ProjectTemplate& t, const stdfs::path& folder, std::string_view name) {
    std::error_code ec;
    if (!stdfs::is_directory(t.folder, ec))
        return std::unexpected(Error{ErrorCode::NotFound, std::format("template '{}' isn't at {}", t.name, t.folder.generic_string())});
    auto project = Project::create(folder, name);
    if (!project) return project;
    for (auto it = stdfs::recursive_directory_iterator(t.folder, ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        const stdfs::path rel = it->path().lexically_relative(t.folder);
        const std::string first = rel.begin()->string();
        // Not the template's own description, a project file, derived data or a game built from it.
        if (rel == "template.json" || rel.extension() == ".noorproj" || first == ".noor" || first == "Build") {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        const stdfs::path dest = project->root() / rel;
        if (it->is_directory()) {
            stdfs::create_directories(dest, ec);
        } else if (rel != ".gitignore") {  // the new project already has its own
            stdfs::create_directories(dest.parent_path(), ec);
            if (!ec) stdfs::copy_file(it->path(), dest, stdfs::copy_options::overwrite_existing, ec);
        }
        if (ec) return std::unexpected(Error{ErrorCode::IoError, std::format("copying the template's {}: {}", rel.generic_string(), ec.message())});
    }
    if (ec) return std::unexpected(Error{ErrorCode::IoError, std::format("reading template '{}': {}", t.name, ec.message())});
    NOOR_LOG_INFO("Asset", "Project '{}' made from the template '{}'", project->info().name, t.name);
    return project;
}

} // namespace noor::asset
