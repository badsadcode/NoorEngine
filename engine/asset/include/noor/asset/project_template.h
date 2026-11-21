#pragma once
// Project templates (2D foundations, part 7; spec 24.1): clean starting projects. A template is a folder holding a
// project without its .noorproj (Assets, Scenes, Scripts, Settings...) and a template.json:
//
//   {"noor_template": 1, "name": "2D Platformer", "description": "...", "order": 20}
//
// Creating a project from one makes a new project (its own id, name and date, as Project::create does) and copies the
// template's files in. Templates are made only of public features: what they show, a game can do. NoorEngine's own
// live in the repository's templates/ and are copied next to noor_cli and the editor when they're built.

#include "noor/asset/project.h"
#include "noor/foundation/error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace noor::asset {

struct ProjectTemplate {
    std::string id;           // the folder's name ("Platformer2D")
    std::string name;         // what people read ("2D Platformer")
    std::string description;
    int order = 100;          // where it's listed (smaller first)
    std::filesystem::path folder;
};

// The templates in `dir` (each a folder with a template.json), by order then name. Folders without a readable
// template.json are skipped (a warning in the log).
[[nodiscard]] std::vector<ProjectTemplate> list_templates(const std::filesystem::path& dir);
// The template `dir/<id>`, or a folder given directly.
[[nodiscard]] Result<ProjectTemplate> load_template(const std::filesystem::path& folder);
// A new project at `folder` named `name`, with the template's files. Fails like Project::create (a project already
// there, a bad name) and if the template is missing; a half-copied project is left for the person to look at, with the
// error naming the file that failed.
[[nodiscard]] Result<Project> create_project_from_template(const ProjectTemplate& t, const std::filesystem::path& folder,
                                                           std::string_view name);

} // namespace noor::asset
