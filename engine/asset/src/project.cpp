#include "noor/asset/project.h"

#include "noor/asset/json.h"
#include "noor/foundation/file_io.h"
#include "noor/foundation/log.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <vector>

namespace noor::asset {
namespace {
constexpr std::string_view kEngineVersion = "0.0.1";
namespace stdfs = std::filesystem;

Result<stdfs::path> find_project_file(const stdfs::path& folder) {
    std::error_code ec;
    stdfs::path found;
    for (const auto& entry : stdfs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".noorproj") {
            if (!found.empty()) {
                return fail(ErrorCode::InvalidArgument,
                            std::format("'{}' contains more than one .noorproj file", folder.generic_string()));
            }
            found = entry.path();
        }
    }
    if (ec || found.empty()) {
        return fail(ErrorCode::NotFound, std::format("No .noorproj file in '{}'", folder.generic_string()));
    }
    return found;
}
} // namespace

std::string utc_now_iso8601() {
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", now);
}

Result<Project> Project::create(const stdfs::path& folder, std::string_view name) {
    if (name.empty() || name.find_first_of("/\\:*?\"<>|") != std::string_view::npos) {
        return fail(ErrorCode::InvalidArgument, std::format("'{}' is not a valid project name", name));
    }
    std::error_code ec;
    if (stdfs::exists(folder, ec) && find_project_file(folder)) {
        return fail(ErrorCode::AlreadyExists, std::format("'{}' already contains a project", folder.generic_string()));
    }
    for (const char* sub : {"Assets", "Scenes", ".noor"}) {
        stdfs::create_directories(folder / sub, ec);
        if (ec) {
            return fail(ErrorCode::IoError, std::format("Cannot create '{}': {}", (folder / sub).generic_string(), ec.message()));
        }
    }

    Project project;
    project.root_ = stdfs::absolute(folder).lexically_normal();
    project.file_ = project.root_ / (std::string(name) + ".noorproj");
    project.info_ = {Guid::generate(), std::string(name), std::string(kEngineVersion), utc_now_iso8601()};

    Json json;
    json["noor_project"] = kProjectFormat;
    json["id"] = project.info_.id.to_string();
    json["name"] = project.info_.name;
    json["engine"] = project.info_.engine_version;
    json["created"] = project.info_.created;
    if (auto s = fs::write_file_atomic(project.file_, to_text(json), false); !s) return std::unexpected(s.error());
    if (auto s = fs::write_file_atomic(project.root_ / ".gitignore", "# Derived data, rebuilt on demand\n.noor/\n*.tmp\n", false); !s) {
        return std::unexpected(s.error());
    }
    NOOR_LOG_INFO("Asset", "Created project '{}' at {}", project.info_.name, project.root_.generic_string());
    return project;
}

Result<Project> Project::open(const stdfs::path& folder_or_file) {
    stdfs::path file = folder_or_file;
    if (stdfs::is_directory(folder_or_file)) {
        auto found = find_project_file(folder_or_file);
        if (!found) return std::unexpected(found.error());
        file = *found;
    }
    auto text = fs::read_file(file, 1 << 20);
    if (!text) return std::unexpected(text.error());
    auto json = parse_json(*text, file.generic_string());
    if (!json) return std::unexpected(json.error());

    const int format = json->value("noor_project", 0);
    if (format == 0) {
        return fail(ErrorCode::ParseError, std::format("'{}' is not a NoorEngine project file", file.generic_string()));
    }
    if (format > kProjectFormat) {
        return fail(ErrorCode::VersionMismatch,
                    std::format("'{}' uses project format {}, this engine reads up to {}. Open it with a newer NoorEngine.",
                                file.generic_string(), format, kProjectFormat));
    }
    auto id = guid_field(*json, "id");
    if (!id) {
        return fail(ErrorCode::ParseError, std::format("'{}' has no valid project id", file.generic_string()));
    }

    Project project;
    project.file_ = stdfs::absolute(file).lexically_normal();
    project.root_ = project.file_.parent_path();
    project.info_ = {*id, json->value("name", std::string{}), json->value("engine", std::string{}),
                     json->value("created", std::string{})};
    return project;
}

Result<stdfs::path> Project::resolve(std::string_view relative) const {
    const stdfs::path rel(relative);
    if (relative.empty() || rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
        return fail(ErrorCode::InvalidArgument, std::format("'{}' must be a path relative to the project", relative));
    }
    const stdfs::path normal = rel.lexically_normal();
    for (const auto& part : normal) {
        if (part == "..") {
            return fail(ErrorCode::InvalidArgument, std::format("'{}' leaves the project folder", relative));
        }
    }
    return root_ / normal;
}

std::optional<std::string> Project::default_start_scene() const {
    const stdfs::path scenes = scenes_dir();
    std::error_code ec;
    if (stdfs::exists(scenes / "Main.noorscene", ec)) return std::string("Scenes/Main.noorscene");
    std::vector<std::string> found;
    for (stdfs::recursive_directory_iterator it(scenes, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".noorscene")
            found.push_back(stdfs::relative(it->path(), root_).generic_string());
    }
    if (found.empty()) return std::nullopt;
    std::ranges::sort(found);
    return found.front();
}

Result<std::string> Project::relative(const stdfs::path& absolute) const {
    const stdfs::path rel = stdfs::absolute(absolute).lexically_normal().lexically_relative(root_);
    if (rel.empty() || *rel.begin() == "..") {
        return fail(ErrorCode::InvalidArgument,
                    std::format("'{}' is outside the project '{}'", absolute.generic_string(), root_.generic_string()));
    }
    return rel.generic_string();
}

} // namespace noor::asset
