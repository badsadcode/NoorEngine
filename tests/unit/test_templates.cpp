// Project templates (2D foundations, part 7; spec 24.1): listing, creating a project from one, and what's refused.
#include "noor/asset/project_template.h"
#include "noor/foundation/file_io.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>

using namespace noor;
namespace stdfs = std::filesystem;

namespace {
const stdfs::path kTemplates = stdfs::path(NOOR_TEST_DATA_DIR).parent_path().parent_path() / "templates";

stdfs::path fresh_dir(const char* name) {
    const auto dir = stdfs::temp_directory_path() / "noor_template_tests" / name;
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    return dir;
}
} // namespace

TEST_CASE("templates: NoorEngine's own are listed in order, with names and descriptions") {
    const auto list = asset::list_templates(kTemplates);
    REQUIRE(list.size() >= 3);
    CHECK(list[0].id == "Empty2D");
    CHECK(list[0].name == "Empty 2D");
    CHECK(list[1].id == "Platformer2D");
    CHECK(list[2].id == "TopDown2D");
    for (const auto& t : list) {
        CHECK_FALSE(t.description.empty());
        CHECK(stdfs::exists(t.folder / "Scenes" / "Main.noorscene"));
    }
    CHECK(asset::list_templates(kTemplates / "no such folder").empty());
}

TEST_CASE("templates: a project made from one is a new project with the template's files") {
    const auto t = asset::load_template(kTemplates / "Platformer2D");
    REQUIRE(t);
    const auto dir = fresh_dir("Platformer");
    auto p = asset::create_project_from_template(*t, dir, "Jumpy");
    REQUIRE(p);
    CHECK(p->info().name == "Jumpy");
    CHECK(stdfs::exists(dir / "Jumpy.noorproj"));
    CHECK(stdfs::exists(dir / "Scenes" / "Main.noorscene"));
    CHECK(stdfs::exists(dir / "Scripts" / "Player.luau"));
    CHECK(stdfs::exists(dir / "Settings" / "display.json"));
    CHECK(stdfs::exists(dir / "Assets" / "Sprites" / "player.png.noormeta"));
    CHECK_FALSE(stdfs::exists(dir / "template.json"));  // the template's own description stays behind
    CHECK(p->default_start_scene() == "Scenes/Main.noorscene");
    // Two projects from one template are two projects.
    auto again = asset::create_project_from_template(*t, fresh_dir("Platformer2"), "Jumpy");
    REQUIRE(again);
    CHECK(again->info().id != p->info().id);
    // Refused: a project already there, a bad name, a folder that isn't a template.
    CHECK_FALSE(asset::create_project_from_template(*t, dir, "Again"));
    CHECK_FALSE(asset::create_project_from_template(*t, fresh_dir("Bad"), "a/b"));
    CHECK_FALSE(asset::load_template(kTemplates / ".."));
}
