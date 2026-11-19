// Effect assets through the editor API (Milestone 8B, part 4): presets, apply, edit, duplicate, reset and the preview
// controls, checked on the files and on what a game would simulate from them (fx::resolve_emitter + an Emitter).
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/foundation/guid.h"
#include "noor/fx/fx_asset.h"
#include "noor/fx/particles.h"
#include "noor/fx/presets.h"
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

struct FxDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_fx_" + noor::Guid::generate().to_string());
    noor_doc* doc = noor_doc_create_project(dir.string().c_str(), "FxTest");
    FxDoc() {
        REQUIRE(doc != nullptr);
        REQUIRE(noor_doc_new_scene(doc, "Scenes/Main.noorscene", "Main") == 1);
    }
    ~FxDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json fx(const Json& command) const { return parsed(noor_doc_effect(doc, command.dump().c_str())); }
    // The emitter component of an entity in the saved scene.
    Json emitter_of(const std::string& id) const {
        REQUIRE(noor_doc_save_scene(doc) == 1);
        auto scene = noor::scene::Scene::load(dir / "Scenes" / "Main.noorscene");
        REQUIRE(scene.has_value());
        for (const auto& e : scene->entities)
            if (e.id.to_string() == id && e.has("emitter")) return e.components.at("emitter");
        return Json();
    }
    // How many particles a game would have from this effect file after `seconds`.
    static std::size_t particles_after(const stdfs::path& file, float seconds) {
        auto json = noor::fx::load_fx_file(file);
        REQUIRE(json.has_value());
        noor::fx::Emitter e(noor::fx::EmitterSettings::parse((*json)["emitter"]), 1);
        for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) e.step(1.0f / 60.0f, noor::math::Mat4::identity(), {0, 0, 0});
        return e.particles().size();
    }
};

} // namespace

TEST_CASE("effect api: six built-in presets, every one an effect that runs") {
    const Json presets = parsed(noor_effect_presets());
    REQUIRE(presets.size() == 6);
    const char* names[] = {"Fire", "Smoke", "Sparks", "Dust", "Rain splash", "Magic"};
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(presets[i]["name"] == names[i]);
        CHECK(!presets[i]["description"].get<std::string>().empty());
        noor::fx::Emitter e(noor::fx::EmitterSettings::parse(presets[i]["emitter"]), 1);
        for (int s = 0; s < 120; ++s) e.step(1.0f / 60.0f, noor::math::Mat4::identity(), {0, 0, 0});
        CHECK(e.particles().size() > 0);  // two seconds in, every preset has particles
    }
}

TEST_CASE("effect api: applying a preset copies it into the project and puts it on an entity, in one undo step") {
    FxDoc d;
    const Json fire = d.fx(Json{{"op", "apply"}, {"preset", "Fire"}, {"position", {1, 0, 2}}});
    CHECK(fire["path"] == "Assets/Effects/Fire.noorfx");
    CHECK(fire["preset"] == "Fire");
    REQUIRE(fire.contains("entity"));
    CHECK(stdfs::exists(d.dir / "Assets" / "Effects" / "Fire.noorfx"));
    CHECK(d.emitter_of(fire["entity"])["asset"] == fire["guid"]);
    CHECK(noor_doc_undo(d.doc) == 1);  // the entity goes again in one step
    CHECK(d.emitter_of(fire["entity"]).is_null());
    // Onto an existing entity, and listed.
    CHECK(noor_doc_redo(d.doc) == 1);
    const Json smoke = d.fx(Json{{"op", "apply"}, {"preset", "Smoke"}, {"entity", fire["entity"]}});
    CHECK(d.emitter_of(fire["entity"])["asset"] == smoke["guid"]);
    CHECK(d.fx(Json{{"op", "list"}}).size() == 2);
}

TEST_CASE("effect api: editing a copy changes what a game simulates, and never the preset") {
    FxDoc d;
    const Json fire = d.fx(Json{{"op", "apply"}, {"preset", "Fire"}});
    const stdfs::path file = d.dir / "Assets" / "Effects" / "Fire.noorfx";
    const std::size_t before = FxDoc::particles_after(file, 1.0f);
    Json emitter = fire["emitter"];
    emitter["rate"] = 10;  // a much smaller fire
    const Json saved = d.fx(Json{{"op", "save"}, {"asset", fire["path"]}, {"emitter", emitter}});
    CHECK(saved["emitter"]["rate"] == 10);
    CHECK(saved["preset"] == "Fire");  // still remembers where it came from
    CHECK(FxDoc::particles_after(file, 1.0f) < before / 3);
    // The built-in preset is untouched, and Reset puts the copy back.
    CHECK(noor::fx::find_effect_preset("Fire")->emitter["rate"] == 90);
    CHECK(parsed(noor_effect_presets())[0]["emitter"]["rate"] == 90);
    const Json reset = d.fx(Json{{"op", "reset"}, {"asset", fire["path"]}});
    CHECK(reset["emitter"]["rate"] == 90);
    // A duplicate is its own file with the same settings.
    const Json copy = d.fx(Json{{"op", "duplicate"}, {"asset", fire["path"]}});
    CHECK(copy["path"] == "Assets/Effects/Fire copy.noorfx");
    CHECK(copy["emitter"] == reset["emitter"]);
    CHECK(copy["guid"] != reset["guid"]);
}

TEST_CASE("effect api: the preview's solo, mute, restart and pause") {
    FxDoc d;
    const Json fire = d.fx(Json{{"op", "apply"}, {"preset", "Fire"}});
    Json state = d.fx(Json{{"op", "preview"}, {"solo", fire["entity"]}});
    CHECK(state["solo"] == fire["entity"]);
    state = d.fx(Json{{"op", "preview"}, {"solo", nullptr}, {"muted", {fire["entity"]}}, {"restart", true}, {"paused", true}});
    CHECK(state["solo"].is_null());
    CHECK(state["muted"].size() == 1);
    CHECK(state["restart"] == 1);
    CHECK(state["paused"] == true);
    CHECK(noor_doc_effect(d.doc, R"({"op": "nonsense"})") == nullptr);  // unknown ops are refused, with an error
}
