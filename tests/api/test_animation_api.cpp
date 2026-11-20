// The Animation panel's engine side through the editor API (Milestone 8C, part 6): a model's clips, skeleton and
// humanoid bone map; correcting the map in its .noormeta; the scene's characters; previewing a clip (scrubbing poses
// the character in the viewports, never the scene) and a controller (running, one state, one transition), and an
// edited transition's fade followed by the preview. Ward and his controller are the test character
// (tests/data/animation/Ward, tests/data/render_scenes/Assets/Characters/Ward.nooranim).
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/foundation/guid.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

const stdfs::path kWard = stdfs::path(NOOR_TEST_DATA_DIR) / "animation" / "Ward" / "Ward_Anim.glb";
const stdfs::path kController = stdfs::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Characters" / "Ward.nooranim";

Json parsed(const char* text) {
    REQUIRE_MESSAGE(text != nullptr, noor_last_error());
    return Json::parse(text);
}

Json read_json(const stdfs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return Json::parse(in);
}

struct AnimDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_anim_" + noor::Guid::generate().to_string());
    noor_doc* doc = nullptr;
    std::string root;  // Ward's model root, once placed
    AnimDoc() {
        doc = noor_doc_create_project(dir.string().c_str(), "AnimTest");
        REQUIRE(doc != nullptr);
        stdfs::create_directories(dir / "Assets" / "Characters");
        stdfs::copy_file(kWard, dir / "Assets" / "Characters" / "Ward.glb");
        stdfs::copy_file(kController, dir / "Assets" / "Characters" / "Ward.nooranim");
        REQUIRE(noor_doc_assets(doc, 1) != nullptr);
        REQUIRE(noor_doc_new_scene(doc, "Scenes/Main.noorscene", "Main") == 1);
    }
    ~AnimDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json anim(const Json& command) const { return parsed(noor_doc_animation(doc, command.dump().c_str())); }
    std::string guid(const std::string& path) const {
        for (const auto& a : parsed(noor_doc_assets(doc, 0)))
            if (a["path"] == path) return a["guid"];
        return {};
    }
    Json scene() const { return parsed(noor_doc_scene(doc)); }
    // Ward placed at the origin, his animator on the controller.
    void place() {
        REQUIRE(noor_doc_execute(doc, Json{{"op", "add_asset"}, {"asset", guid("Assets/Characters/Ward.glb")}, {"name", "Ward"}}.dump().c_str()) == 1);
        const Json s = scene();  // kept: the loop reads into it
        for (const auto& e : s["entities"])
            if (e["name"] == "Ward" && (!e.contains("parent") || e["parent"].is_null())) root = e["id"];
        REQUIRE(!root.empty());
        const Json animator{{"controller", guid("Assets/Characters/Ward.nooranim")}, {"parameters", {{"speed", 0}}}};
        REQUIRE(noor_doc_execute(doc, Json{{"op", "set_component"}, {"entity", root}, {"type", "animator"}, {"value", animator}}.dump().c_str()) == 1);
    }
};

const Json* bone(const Json& model, const std::string& name) {
    for (const auto& b : model["bone_map"]["bones"])
        if (b["bone"] == name) return &b;
    return nullptr;
}

} // namespace

TEST_CASE("animation api: a model's clips, skeleton and bone map, and a correction in its .noormeta") {
    AnimDoc d;
    const Json model = d.anim({{"op", "model"}, {"asset", "Assets/Characters/Ward.glb"}});
    CHECK(model["clips"].size() == 9);
    bool walk = false;
    for (const auto& c : model["clips"])
        if (c["name"] == "Walk") walk = c["duration"].get<double>() > 0.5 && c["channels"].get<int>() > 0;
    CHECK(walk);
    CHECK(model["skinned"] == true);
    int joints = 0;
    for (const auto& n : model["nodes"]) joints += n["joint"].get<bool>() ? 1 : 0;
    CHECK(joints == 17);
    CHECK(model["bone_map"]["usable"] == true);
    CHECK(model["bone_map"]["bones"].size() == 22);
    REQUIRE(bone(model, "LeftHand") != nullptr);
    CHECK((*bone(model, "LeftHand"))["node"] == "DEF-hand.L");
    CHECK((*bone(model, "LeftHand"))["required"] == true);
    CHECK((*bone(model, "LeftHand"))["corrected"] == false);

    // No left hand: written into the .noormeta; the map says what's wrong and can't be used.
    const Json none = d.anim({{"op", "set_bone"}, {"asset", "Assets/Characters/Ward.glb"}, {"bone", "LeftHand"}, {"node", nullptr}});
    CHECK((*bone(none, "LeftHand"))["node"].is_null());
    CHECK((*bone(none, "LeftHand"))["corrected"] == true);
    CHECK(none["bone_map"]["usable"] == false);
    CHECK(none["bone_map"]["preset"].get<std::string>().ends_with("(edited)"));
    bool said = false;
    for (const auto& w : none["bone_map"]["warnings"]) said = said || w.get<std::string>().find("LeftHand") != std::string::npos;
    CHECK(said);
    const Json meta = read_json(d.dir / "Assets" / "Characters" / "Ward.glb.noormeta");
    CHECK(meta["settings"]["bone_map"].contains("LeftHand"));
    CHECK(meta["settings"]["bone_map"]["LeftHand"].is_null());
    CHECK(meta.contains("guid"));  // the rest of the .noormeta kept

    // Mapped to another bone; then detected again.
    const Json other = d.anim({{"op", "set_bone"}, {"asset", "Assets/Characters/Ward.glb"}, {"bone", "LeftHand"}, {"node", "DEF-forearm.L"}});
    CHECK((*bone(other, "LeftHand"))["node"] == "DEF-forearm.L");
    const Json back = d.anim({{"op", "set_bone"}, {"asset", "Assets/Characters/Ward.glb"}, {"bone", "LeftHand"}, {"auto", true}});
    CHECK((*bone(back, "LeftHand"))["node"] == "DEF-hand.L");
    CHECK(back["bone_map"]["usable"] == true);
    CHECK(!read_json(d.dir / "Assets" / "Characters" / "Ward.glb.noormeta")["settings"].contains("bone_map"));

    // Refused: a bone the model doesn't have, a humanoid bone that doesn't exist, a controller where a model belongs.
    CHECK(noor_doc_animation(d.doc, R"({"op": "set_bone", "asset": "Assets/Characters/Ward.glb", "bone": "LeftHand", "node": "Tail"})") == nullptr);
    CHECK(noor_doc_animation(d.doc, R"({"op": "set_bone", "asset": "Assets/Characters/Ward.glb", "bone": "LeftWing", "node": null})") == nullptr);
    CHECK(noor_doc_animation(d.doc, R"({"op": "model", "asset": "Assets/Characters/Ward.nooranim"})") == nullptr);
    CHECK(noor_doc_animation(d.doc, R"({"op": "dance"})") == nullptr);
}

TEST_CASE("animation api: scrubbing a clip poses the character in the viewports, never the scene") {
    AnimDoc d;
    d.place();
    const Json characters = d.anim({{"op", "characters"}});
    REQUIRE(characters.size() == 1);
    CHECK(characters[0]["entity"] == d.root);
    CHECK(characters[0]["controller"] == "Assets/Characters/Ward.nooranim");
    CHECK(characters[0]["clips"].size() == 9);

    const Json before = d.scene();
    const Json at0 = d.anim({{"op", "preview"}, {"entity", d.root}, {"clip", "Walk"}, {"time", 0.0}, {"highlight", "DEF-hand.L"}});
    CHECK(at0["mode"] == "clip");
    CHECK(at0["joints"] == 17);
    CHECK(at0["bones"] == 17);  // a line (or a dot, for the topmost) per joint
    CHECK(at0["nodes"].get<int>() > 0);
    const Json at_half = d.anim({{"op", "preview"}, {"time", 0.5}});
    CHECK(at_half["fingerprint"] != at0["fingerprint"]);
    CHECK(at_half["length"].get<double>() > 0.5);
    REQUIRE(at0.contains("highlight_position"));
    CHECK(at_half["highlight_position"] != at0["highlight_position"]);  // the hand swings through a walk
    // Back to the start: exactly the same pose (a pure function of time).
    CHECK(d.anim({{"op", "preview"}, {"time", 0.0}})["fingerprint"] == at0["fingerprint"]);
    // The scene itself: not one transform or revision changed.
    const Json after = d.scene();
    CHECK(after["entities"] == before["entities"]);
    CHECK(after["revision"] == before["revision"]);
    CHECK(after["modified"] == before["modified"]);

    // A bone picked in the panel: where it is, drawn brighter; the skeleton can be hidden.
    const Json picked = d.anim({{"op", "preview"}, {"highlight", "DEF-foot.R"}});
    CHECK(picked["highlight"] == "DEF-foot.R");
    CHECK(picked.contains("highlight_position"));
    CHECK(picked["highlight_position"] != at0["highlight_position"]);
    CHECK(d.anim({{"op", "preview"}, {"skeleton", false}})["bones"] == 0);
    // In place: the hips held horizontally (where root motion would move the character instead).
    const Json placed = d.anim({{"op", "preview"}, {"time", 0.5}, {"in_place", true}, {"clip", "WalkForward"}});
    const Json moving = d.anim({{"op", "preview"}, {"in_place", false}});
    CHECK(placed["root_position"][2] != moving["root_position"][2]);
    // A clip it doesn't have says so; ending the preview.
    CHECK(d.anim({{"op", "preview"}, {"clip", "Moonwalk"}})["error"].get<std::string>().find("Moonwalk") != std::string::npos);
    CHECK(d.anim({{"op", "preview"}, {"entity", nullptr}})["entity"].is_null());
}

TEST_CASE("animation api: a controller previewed running, by state and by transition, an edited fade followed") {
    AnimDoc d;
    d.place();
    // Running, as in a game: the nod trigger fires once.
    const Json moving = d.anim({{"op", "preview"}, {"entity", d.root}, {"controller", true}, {"parameters", {{"speed", 1.4}}}, {"time", 1.0}});
    CHECK(moving["mode"] == "controller");
    REQUIRE(moving["states"].size() == 3);
    CHECK(moving["states"][0] == "Move");
    CHECK(moving["states"][1] == "Off");
    const Json nod = d.anim({{"op", "preview"}, {"parameters", {{"nod", true}}}, {"time", 0.05}});
    CHECK(nod["states"][0] == "Nod");
    // One state, from its start.
    const Json wave = d.anim({{"op", "preview"}, {"layer", "Arm"}, {"state", "Wave"}, {"time", 0.2}});
    CHECK(wave["states"][1] == "Wave");
    CHECK(wave["length"].get<double>() > 0.0);

    // One transition: Arm's Off -> Wave (fade 0.3). Off is empty (a second long), so it leads in for half a second.
    const Json fading = d.anim({{"op", "preview"}, {"transition", 0}, {"time", 0.65}});
    CHECK(fading["part"] == "fading");
    CHECK(fading["from"] == "Off");
    CHECK(fading["to"] == "Wave");
    CHECK(fading["lead"].get<double>() == doctest::Approx(0.5));
    CHECK(fading["length"].get<double>() == doctest::Approx(0.5 + 0.3 + 1.0));
    CHECK(fading["fade_weights"][1].get<double>() == doctest::Approx(0.5).epsilon(1e-4));  // 9 of 18 steps
    CHECK(d.anim({{"op", "preview"}, {"time", 0.3}})["part"] == "before");
    CHECK(d.anim({{"op", "preview"}, {"time", 1.5}})["part"] == "after");

    // The panel saves a slower fade: the same moment of the preview is now a quarter of the way.
    const Json file = d.anim({{"op", "controller"}, {"asset", "Assets/Characters/Ward.nooranim"}});
    CHECK(!file.contains("error"));
    Json ctrl = file["controller"];
    ctrl["layers"][1]["transitions"][0]["fade"] = 0.6;
    const Json saved = d.anim({{"op", "save_controller"}, {"asset", "Assets/Characters/Ward.nooranim"}, {"controller", ctrl}});
    CHECK(saved["controller"]["layers"][1]["transitions"][0]["fade"] == 0.6);
    const Json slower = d.anim({{"op", "preview"}, {"time", 0.65}});
    CHECK(slower["fade"].get<double>() == doctest::Approx(0.6));
    CHECK(slower["fade_weights"][1].get<double>() == doctest::Approx(0.25).epsilon(1e-4));
    CHECK(slower["fingerprint"] != fading["fingerprint"]);
    CHECK(read_json(d.dir / "Assets" / "Characters" / "Ward.nooranim")["layers"][1]["transitions"][0]["fade"] == 0.6);

    // A controller that doesn't parse isn't written.
    Json broken = ctrl;
    broken["layers"][1]["transitions"][0]["to"] = "Cartwheel";
    CHECK(noor_doc_animation(d.doc, Json{{"op", "save_controller"}, {"asset", "Assets/Characters/Ward.nooranim"}, {"controller", broken}}.dump().c_str()) ==
          nullptr);
    CHECK(std::string(noor_last_error()).find("Cartwheel") != std::string::npos);
    CHECK(read_json(d.dir / "Assets" / "Characters" / "Ward.nooranim")["layers"][1]["transitions"][0]["to"] == "Wave");
    CHECK(d.anim({{"op", "preview"}, {"transition", 7}})["error"].get<std::string>().find("no transition 7") != std::string::npos);
}
