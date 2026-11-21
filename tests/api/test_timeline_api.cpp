// The Timeline panel through the editor API (gameplay foundations, part 5): a new timeline on an entity, keys taken
// from the scene, events, the preview that shows a moment without touching the scene, and removing keys.
#include <ostream>
#include <doctest/doctest.h>

#include "noor_editor_api.h"
#include "noor/asset/json.h"
#include "noor/asset/timeline_asset.h"
#include "noor/foundation/guid.h"

#include <filesystem>
#include <string>

namespace stdfs = std::filesystem;
using noor::asset::Json;

namespace {

Json parsed(const char* text) {
    REQUIRE(text != nullptr);
    return Json::parse(text);
}

struct TimelineDoc {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_timeline_api_" + noor::Guid::generate().to_string());
    noor_doc* doc = noor_doc_create_project(dir.string().c_str(), "TimelineTest");
    std::string door, hinge;
    TimelineDoc() {
        REQUIRE(doc != nullptr);
        REQUIRE(noor_doc_new_scene(doc, "Scenes/Main.noorscene", "Main") == 1);
        door = noor::Guid::generate().to_string();
        hinge = noor::Guid::generate().to_string();
        REQUIRE(noor_doc_execute(doc, Json{{"op", "create"}, {"id", door}, {"name", "Door"}}.dump().c_str()) == 1);
        REQUIRE(noor_doc_execute(doc, Json{{"op", "create"}, {"id", hinge}, {"name", "Hinge"}, {"parent", door}}.dump().c_str()) == 1);
    }
    ~TimelineDoc() {
        if (doc) noor_doc_close(doc);
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    Json timeline(Json command) const {
        if (!command.contains("entity") && command["op"] != "list" && command["op"] != "preview_end") command["entity"] = door;
        return parsed(noor_doc_timeline(doc, command.dump().c_str()));
    }
    Json entity(const std::string& id) const {
        const Json scene = parsed(noor_doc_scene(doc));
        for (const auto& e : scene["entities"])
            if (e["id"] == id) return e;
        return Json();
    }
    bool set_y_rotation(const std::string& id, double degrees) const {
        const double h = degrees * 3.14159265358979 / 360.0;
        return noor_doc_execute(doc, Json{{"op", "set_transform"}, {"entity", id},
                                          {"transform", {{"rotation", {0, std::sin(h), 0, std::cos(h)}}}}}.dump().c_str()) == 1;
    }
};

} // namespace

TEST_CASE("timeline api: new, keys from the scene, events, info, preview without touching the scene, removing keys") {
    TimelineDoc d;
    // A new timeline: a file, and the door's animation player with it (its autoplay, being the first).
    const Json made = d.timeline({{"op", "new"}, {"name", "open"}, {"length", 2.0}});
    CHECK(made["path"] == "Assets/Timelines/Door_open.noortimeline");
    CHECK(stdfs::exists(d.dir / "Assets" / "Timelines" / "Door_open.noortimeline"));
    const Json player = d.entity(d.door)["components"]["animation_player"];
    CHECK(player["timelines"]["open"] == made["id"]);
    CHECK(player["autoplay"] == "open");
    CHECK(d.timeline({{"op", "list"}})["timelines"].size() == 1);
    // Keys as an animator sets them: pose the hinge, key it, pose again, key it later.
    REQUIRE(d.set_y_rotation(d.hinge, 0));
    d.timeline({{"op", "key"}, {"timeline", "open"}, {"t", 0.0}, {"target", "Hinge"}, {"property", "euler"}});
    REQUIRE(d.set_y_rotation(d.hinge, 90));
    const Json k = d.timeline({{"op", "key"}, {"timeline", "open"}, {"t", 2.0}, {"target", "Hinge"}, {"property", "euler"}, {"ease", "back_out"}});
    CHECK(k["keys"] == 2);
    CHECK(k["value"][1].get<double>() == doctest::Approx(90).epsilon(1e-3));  // read from the scene
    d.timeline({{"op", "event"}, {"timeline", "open"}, {"t", 1.0}, {"function", "creak"}});
    d.timeline({{"op", "set"}, {"timeline", "open"}, {"loop", true}});
    const Json all = d.timeline({{"op", "info"}});
    const Json info = all["timelines"]["open"];
    CHECK(info["length"] == 2.0);
    CHECK(info["loop"] == true);
    REQUIRE(info["tracks"].size() == 2);
    CHECK(info["tracks"][0]["kind"] == "value");
    CHECK(info["tracks"][0]["keys"][1]["ease"] == "back_out");
    CHECK(info["tracks"][1]["kind"] == "call");
    // Preview: the hinge shows the timeline's pose at 0 (it's really at 90), and comes back when the preview ends.
    const Json shown = d.timeline({{"op", "preview"}, {"timeline", "open"}, {"t", 0.0}});
    CHECK(shown["changed"] == 1);
    CHECK(std::abs(d.entity(d.hinge)["transform"]["rotation"][1].get<double>()) < 1e-6);
    d.timeline({{"op", "preview"}, {"timeline", "open"}, {"t", 2.0}});  // scrubbing on
    d.timeline({{"op", "preview_end"}});
    CHECK(d.entity(d.hinge)["transform"]["rotation"][1].get<double>() == doctest::Approx(std::sin(90 * 3.14159265358979 / 360)));
    // A preview isn't an edit: undo still undoes the last real one (the hinge turned to 90).
    d.timeline({{"op", "preview"}, {"timeline", "open"}, {"t", 0.0}});
    CHECK(noor_doc_undo(d.doc) == 1);  // the preview ends first, then the undo
    CHECK(std::abs(d.entity(d.hinge)["transform"]["rotation"][1].get<double>()) < 1e-6);
    // Removing keys; a track left empty goes.
    d.timeline({{"op", "remove_key"}, {"timeline", "open"}, {"track", 1}, {"key", 0}});
    CHECK(d.timeline({{"op", "info"}})["timelines"]["open"]["tracks"].size() == 1);
    // Bad requests say why.
    CHECK(noor_doc_timeline(d.doc, Json{{"op", "key"}, {"entity", d.door}, {"timeline", "close"}, {"property", "euler"}}.dump().c_str()) == nullptr);
    CHECK(std::string(noor_last_error()).find("no timeline 'close'") != std::string::npos);
}
