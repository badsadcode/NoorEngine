#pragma once
// A NoorEngine project: a folder with a <name>.noorproj file (ADR-005).
//
//   MyGame/
//     MyGame.noorproj
//     Assets/        source files + <file>.noormeta sidecars (ADR-006)
//     Scenes/        *.noorscene (ADR-004)
//     .noor/         derived data, never the source of truth (ignored by Git)

#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace noor::asset {

inline constexpr int kProjectFormat = 1;

struct ProjectInfo {
    Guid id;
    std::string name;
    std::string engine_version;
    std::string created;  // ISO 8601 UTC
};

class Project {
public:
    // Creates the folder structure and the project file. Fails if a project already exists there.
    [[nodiscard]] static Result<Project> create(const std::filesystem::path& folder, std::string_view name);
    // Opens a project from its folder or its .noorproj file.
    [[nodiscard]] static Result<Project> open(const std::filesystem::path& folder_or_file);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }
    [[nodiscard]] const ProjectInfo& info() const noexcept { return info_; }
    [[nodiscard]] std::filesystem::path assets_dir() const { return root_ / "Assets"; }
    [[nodiscard]] std::filesystem::path scenes_dir() const { return root_ / "Scenes"; }

    // Project-relative path ("Assets/Buildings/a.glb") to an absolute path. Rejects absolute
    // paths, drive letters and anything that escapes the project folder (spec 28.2).
    [[nodiscard]] Result<std::filesystem::path> resolve(std::string_view relative) const;
    // Absolute path inside the project to its relative form with '/' separators.
    [[nodiscard]] Result<std::string> relative(const std::filesystem::path& absolute) const;

private:
    std::filesystem::path root_;
    std::filesystem::path file_;
    ProjectInfo info_;
};

// Current UTC time as "YYYY-MM-DDTHH:MM:SSZ".
[[nodiscard]] std::string utc_now_iso8601();

} // namespace noor::asset
