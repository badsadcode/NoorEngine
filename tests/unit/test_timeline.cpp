// Timelines (gameplay foundations, part 5): the file format, keys around a time, and the animation player: value tracks
// on children, call and emit keys fired once as play passes them, looping, the end message, and the script API.
#include "noor/asset/timeline_asset.h"
#include "noor/foundation/file_io.h"
#include "noor/game/timeline.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <filesystem>

using namespace noor;
using asset::Json;
namespace api = runtime::api;
namespace stdfs = std::filesystem;

namespace {

const Json kDoorOpen = Json::parse(R"({
    "noor_timeline": 1, "name": "DoorOpen", "length": 1.0, "loop": false,
    "tracks": [
      {"target": "Hinge", "property": "euler", "keys": [{"t": 0, "v": [0, 0, 0]}, {"t": 1.0, "v": [0, 90, 0]}]},
      {"target": "", "property": "light.intensity", "keys": [{"t": 0.25, "v": 0}, {"t": 0.75, "v": 100, "ease": "ease_in"}]},
      {"target": "", "call": true, "keys": [{"t": 0, "function": "note", "args": ["start"]}, {"t": 0.5, "function": "note", "args": ["creak"]}]},
      {"target": "Hinge", "emit": true, "keys": [{"t": 1.0, "message": "latched", "data": {"how": "click"}}]}
    ]})");

struct Door {
    stdfs::path dir = stdfs::temp_directory_path() / ("noor_timeline_" + Guid::generate().to_string());
    Guid door = Guid::generate(), hinge = Guid::generate(), timeline_id = Guid::generate();
    runtime::World world;
    scripting::LuaScriptSystem* lua = nullptr;
    Door(Json timeline, const std::string& autoplay = "open") : world(make(autoplay)) {
        stdfs::create_directories(dir);
        REQUIRE(fs::write_file_atomic(dir / "door.noortimeline", timeline.dump(), false));
        auto l = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
        l->add_source("Scripts/door.luau", R"(
            local D = {}
            function D:on_start()
                self.log = {}
                self.entity:child("Hinge"):on("latched", function(data) table.insert(self.log, "latched " .. data.how) end)
                self.entity:on("timeline_end", function(data) table.insert(self.log, "end " .. data.name) end)
            end
            function D:note(what) table.insert(self.log, string.format("%s %.3f", what, noor.time())) end
            function D:on_late_update() self.entity:set("out", { log = self.log }) end
            return D
        )");
        lua = l.get();
        world.add_system(std::move(l));
        world.add_system(std::make_unique<game::TimelineSystem>([this](const Guid& id) -> std::optional<stdfs::path> {
            if (id == timeline_id) return dir / "door.noortimeline";
            return std::nullopt;
        }));
    }
    ~Door() {
        std::error_code ec;
        stdfs::remove_all(dir, ec);
    }
    scene::Scene make(const std::string& autoplay) {
        scene::Scene s;
        scene::Entity d;
        d.id = door;
        d.name = "Door";
        d.components["light"] = {{"type", "point"}, {"intensity", 50.0}};
        d.components["script"] = {{"file", "Scripts/door.luau"}};
        d.components["animation_player"] = {{"timelines", {{"open", timeline_id.to_string()}}}};
        if (!autoplay.empty()) d.components["animation_player"]["autoplay"] = autoplay;
        scene::Entity h;
        h.id = hinge;
        h.name = "Hinge";
        h.parent = door;
        s.entities = {d, h};
        return s;
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) world.frame(1.0 / 60.0, {});
    }
    Json log() {
        const Json* o = api::component(world, door, "out");
        return o ? (*o)["log"] : Json();
    }
};

} // namespace

TEST_CASE("timeline files: parsed, keys sorted, saved and read back the same, bad ones refused with a reason") {
    auto t = asset::parse_timeline(kDoorOpen);
    REQUIRE(t);
    CHECK(t->name == "DoorOpen");
    REQUIRE(t->tracks.size() == 4);
    CHECK(t->tracks[0].target == "Hinge");
    CHECK(t->tracks[2].kind == asset::TimelineTrack::Kind::Call);
    CHECK(t->tracks[3].kind == asset::TimelineTrack::Kind::Emit);
    auto again = asset::parse_timeline(asset::timeline_file_json(*t));
    REQUIRE(again);
    CHECK(asset::timeline_file_json(*again) == asset::timeline_file_json(*t));
    // Keys come out in time order whatever order they were written in.
    Json shuffled = kDoorOpen;
    shuffled["tracks"][1]["keys"] = Json::array({{{"t", 0.75}, {"v", 100}}, {{"t", 0.25}, {"v", 0}}});
    CHECK(asset::parse_timeline(shuffled)->tracks[1].keys[0].t == 0.25);
    CHECK_FALSE(asset::parse_timeline(Json::parse(R"({"noor_timeline": 1, "tracks": [{"keys": []}]})")));
    CHECK(asset::parse_timeline(Json::parse(R"({"noor_timeline": 1, "tracks": [{"property": "x", "keys": [{"t": 0}]}]})"))
              .error()
              .message.find("no value") != std::string::npos);
    // Where a time falls between keys.
    const auto& track = t->tracks[1];
    CHECK(asset::key_span(track, 0.0)->f == 0.0);
    CHECK(asset::key_span(track, 0.0)->a == &track.keys[0]);
    CHECK(asset::key_span(track, 0.5)->f == doctest::Approx(0.5));
    CHECK(asset::key_span(track, 0.5)->b == &track.keys[1]);
    CHECK(asset::key_span(track, 9.0)->a == &track.keys[1]);
}

TEST_CASE("animation player: values on the entity and a child, call and emit keys once, then it ends with a message") {
    Door d(kDoorOpen);
    d.frames(30);  // 0.5 s played (autoplay starts with the first frame)
    CHECK(api::euler(d.world, d.hinge)->y == doctest::Approx(45).epsilon(1e-3));
    // light.intensity: 0 until 0.25, then ease_in towards 100 at 0.75: at 0.5 it's 100 * 0.5^2
    CHECK(api::component(d.world, d.door, "light")->at("intensity").get<double>() == doctest::Approx(25).epsilon(1e-3));
    d.frames(60);
    CHECK(api::euler(d.world, d.hinge)->y == doctest::Approx(90));
    CHECK(api::component(d.world, d.door, "light")->at("intensity").get<double>() == doctest::Approx(100));
    CHECK_FALSE(api::current_timeline(d.world, d.door));
    // Keys at 0 fire on the first step; each fires once; the end message after the emit at the very end.
    CHECK(d.log() == Json::array({"start 0.000", "creak 0.500", "latched click", "end open"}));
    CHECK(d.lua->errors().empty());
}

TEST_CASE("animation player: looping fires every pass, scripts play, stop and ask, pausing freezes it") {
    Json looping = kDoorOpen;
    looping["loop"] = true;
    Door d(looping, "");
    CHECK_FALSE(api::current_timeline(d.world, d.door));
    d.frames(2);
    CHECK(api::euler(d.world, d.hinge)->y == 0.0f);  // no autoplay
    CHECK(api::play_timeline(d.world, d.door, "open", 2.0f));
    CHECK_FALSE(api::play_timeline(d.world, d.door, "close"));
    d.frames(62);  // a little over 2 s at double speed: two whole passes
    CHECK(api::current_timeline(d.world, d.door) == "open");
    int creaks = 0, latches = 0;
    for (const auto& line : d.log()) {
        creaks += line.get<std::string>().starts_with("creak") ? 1 : 0;
        latches += line == "latched click" ? 1 : 0;
    }
    CHECK(creaks == 2);
    CHECK(latches == 2);
    d.world.set_paused(true);
    const float y = api::euler(d.world, d.hinge)->y;
    d.frames(20);
    CHECK(api::euler(d.world, d.hinge)->y == y);
    d.world.set_paused(false);
    api::stop_timeline(d.world, d.door);
    d.frames(5);
    CHECK(api::euler(d.world, d.hinge)->y == y);  // stopped where it was
    CHECK_FALSE(api::current_timeline(d.world, d.door));
}
