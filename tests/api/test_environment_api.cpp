// Environment assets through the editor API (Milestone 8A, part 6): presets, apply, duplicate, edit, reset, save and
// reopen, checked on what a game would actually draw (game::collect_view), not just on the files.
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/asset/project.h"
#include "noor/asset/registry.h"
#include "noor/foundation/guid.h"
#include "noor/game/game.h"
#include "noor/scene/scene.h"

#include <filesystem>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

Json parsed(const char* text) {
    REQUIRE(text != nullptr);
    return Json::parse(text);
}

struct EnvDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_env_" + noor::Guid::generate().to_string());
    noor_doc* doc = noor_doc_create_project(dir.string().c_str(), "EnvTest");
    EnvDoc() {
        REQUIRE(doc != nullptr);
        REQUIRE(noor_doc_new_scene(doc, "Scenes/Main.noorscene", "Main") == 1);
        // A camera that exposes at EV 14 and a sun, like a fresh scene in the editor.
        REQUIRE(noor_doc_execute(doc, Json{{"op", "create"}, {"name", "Camera"}, {"components", {{"camera", {{"fov", 60}, {"ev100", 14}}}}}}.dump().c_str()) == 1);
        REQUIRE(noor_doc_execute(doc, Json{{"op", "create"}, {"name", "Sun"},
                                           {"transform", {{"rotation", {-0.38268343, 0, 0, 0.92387953}}}},
                                           {"components", {{"light", {{"type", "directional"}, {"illuminance", 100000}}}}}}.dump().c_str()) == 1);
    }
    ~EnvDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json env(const Json& command) const { return parsed(noor_doc_environment(doc, command.dump().c_str())); }
    // What a game would draw from the document's scene right now.
    noor::game::SceneView view() const {
        // The scene as a game reads it: saved, then loaded from its file.
        REQUIRE(noor_doc_save_scene(doc) == 1);
        auto scene = noor::scene::Scene::load(dir / "Scenes" / "Main.noorscene");
        REQUIRE(scene.has_value());
        auto opened = noor::asset::Project::open(dir);
        REQUIRE(opened.has_value());
        project = std::move(*opened);
        registry = {};
        REQUIRE(registry.scan(*project).has_value());
        return noor::game::collect_view(*scene, noor::game::registry_asset_path(*project, registry));
    }
    // The resolver collect_view gets keeps references to these.
    mutable std::optional<noor::asset::Project> project;
    mutable noor::asset::AssetRegistry registry;
    Json environment_component() const {
        const Json scene = parsed(noor_doc_scene(doc));  // kept alive: a range-for over a temporary's member dangles
        for (const auto& e : scene["entities"])
            if (e.contains("components") && e["components"].contains("environment")) return e["components"]["environment"];
        return Json();
    }
};

} // namespace

TEST_CASE("environment api: nine built-in presets, every one a complete environment") {
    const Json presets = parsed(noor_environment_presets());
    const char* names[] = {"Clear Noon", "Fair Weather", "Golden Evening", "Dusk", "Overcast", "Foggy Dawn", "Pink Mist", "Moonlit Night", "Stormy Coast"};
    REQUIRE(presets.size() == std::size(names));
    for (std::size_t i = 0; i < std::size(names); ++i) {
        CAPTURE(names[i]);
        CHECK(presets[i]["name"] == names[i]);
        CHECK(!presets[i]["description"].get<std::string>().empty());
        CHECK(presets[i]["environment"]["sky"] == "atmosphere");
        CHECK(presets[i]["environment"]["ev100"] == "auto");  // exposure travels with the environment
        CHECK(presets[i]["environment"].contains("time_of_day"));
    }
}

TEST_CASE("environment api: applying a preset changes sky, light, fog and exposure together, in one undo step") {
    EnvDoc d;
    const Json before_component = d.environment_component();
    const auto before = d.view();

    const Json applied = d.env({{"op", "apply"}, {"preset", "Foggy Dawn"}});
    CHECK(applied["path"] == "Assets/Environments/Foggy Dawn.noorenv");
    CHECK(stdfs::exists(d.dir / "Assets" / "Environments" / "Foggy Dawn.noorenv"));
    CHECK(d.environment_component() == Json{{"asset", applied["guid"]}});

    const auto after = d.view();
    CHECK(after.environment.atmosphere);                                         // sky
    CHECK(after.lighting.sun_direction.y > -0.3f);                               // light: a dawn sun, low in the sky
    CHECK(after.lighting.fog.density == doctest::Approx(0.02f));                 // fog
    CHECK(after.lighting.ev100 < before.lighting.ev100 - 1.0f);                  // exposure: metered, dawn is darker
    CHECK_FALSE(before.environment.atmosphere);

    // One undo step brings the old environment back.
    REQUIRE(noor_doc_undo(d.doc) == 1);
    CHECK(d.environment_component() == before_component);
    CHECK(d.view().lighting.fog.density == 0.0f);
}

TEST_CASE("environment api: duplicate, edit and reset never touch the installed preset") {
    EnvDoc d;
    const Json presets_before = parsed(noor_environment_presets());
    const Json evening = d.env({{"op", "apply"}, {"preset", "Golden Evening"}});

    // A duplicate keeps the settings and where it came from.
    const Json copy = d.env({{"op", "duplicate"}, {"asset", evening["path"]}});
    CHECK(copy["path"] == "Assets/Environments/Golden Evening copy.noorenv");
    CHECK(copy["preset"] == "Golden Evening");
    CHECK(copy["environment"] == evening["environment"]);
    CHECK(copy["guid"] != evening["guid"]);

    // Edit the copy: only the copy changes.
    Json edited = copy["environment"];
    edited["fog_density"] = 0.05;
    edited["time_of_day"] = 20.0;
    const Json saved = d.env({{"op", "save"}, {"asset", copy["path"]}, {"environment", edited}});
    CHECK(saved["environment"]["fog_density"] == 0.05);
    CHECK(d.env({{"op", "read"}, {"asset", evening["path"]}})["environment"] == evening["environment"]);
    CHECK(parsed(noor_environment_presets()) == presets_before);

    // Reset: back to the built-in preset, by name.
    const Json reset = d.env({{"op", "reset"}, {"asset", copy["path"]}});
    CHECK(reset["environment"] == presets_before[2]["environment"]);  // Golden Evening
    CHECK(parsed(noor_environment_presets()) == presets_before);

    // Applying the preset again keeps the project's copy (and its edits), it doesn't overwrite it.
    Json mine = evening["environment"];
    mine["turbidity"] = 7.0;
    d.env({{"op", "save"}, {"asset", evening["path"]}, {"environment", mine}});
    CHECK(d.env({{"op", "apply"}, {"preset", "Golden Evening"}})["environment"]["turbidity"] == 7.0);

    // The list shows them all; a bad request says why.
    CHECK(d.env({{"op", "list"}}).size() == 2);
    CHECK(noor_doc_environment(d.doc, R"({"op": "apply", "preset": "Martian Tuesday"})") == nullptr);
    CHECK(std::string(noor_last_error()).find("Martian Tuesday") != std::string::npos);
}

TEST_CASE("environment api: a custom environment saves and reopens identically") {
    EnvDoc d;
    const Json night = d.env({{"op", "apply"}, {"preset", "Moonlit Night"}});
    Json custom = night["environment"];
    custom["moon_phase"] = 0.3;
    custom["fog_density"] = 0.006;
    const Json saved = d.env({{"op", "save"}, {"asset", night["path"]}, {"environment", custom}, {"name", "Waxing Mist"}});
    REQUIRE(noor_doc_save_scene(d.doc) == 1);
    const auto view_before = d.view();
    const Json scene_before = parsed(noor_doc_scene(d.doc));

    // Close and reopen the project and its scene.
    noor_doc_close(d.doc);
    d.doc = noor_doc_open_project(d.dir.string().c_str());
    REQUIRE(d.doc != nullptr);
    REQUIRE(noor_doc_open_scene(d.doc, "Scenes/Main.noorscene") == 1);

    const Json reread = d.env({{"op", "read"}, {"asset", night["path"]}});
    CHECK(reread["name"] == "Waxing Mist");
    CHECK(reread["environment"] == saved["environment"]);
    CHECK(reread["guid"] == night["guid"]);
    CHECK(parsed(noor_doc_scene(d.doc))["entities"] == scene_before["entities"]);
    const auto view_after = d.view();
    CHECK(view_after.lighting.fog.density == view_before.lighting.fog.density);
    CHECK(view_after.lighting.ev100 == view_before.lighting.ev100);
    CHECK(view_after.lighting.sky.moon_relative == view_before.lighting.sky.moon_relative);
}
