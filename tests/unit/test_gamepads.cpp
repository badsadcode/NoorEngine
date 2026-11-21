// Gamepads and rebinding (gameplay foundations part 6e), without a pad: bindings by name, the default map's pad controls,
// sticks' round dead zone and smooth values, players (the keyboard is player 1's only), presses of buttons, sticks and
// triggers, what was pressed (a settings menu's "press a key"), pads plugged in and out as messages, and rebinding from
// scripts: moved from wherever else, reset, saved to the player's file and read back over the game's own at the next
// start.
#include "noor/foundation/file_io.h"
#include "noor/foundation/guid.h"
#include "noor/runtime/input.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <format>
#include <filesystem>

using namespace noor;
using namespace noor::runtime;
using asset::Json;

namespace {

InputState with_pad(int player, std::initializer_list<PadButton> buttons, std::initializer_list<std::pair<PadAxis, float>> axes = {}) {
    InputState s;
    auto& pad = s.pads[static_cast<std::size_t>(player - 1)];
    pad.connected = true;
    pad.name = "Test pad";
    for (const auto b : buttons) pad.buttons.set(static_cast<std::size_t>(b));
    for (const auto& [a, v] : axes) pad.axes[static_cast<std::size_t>(a)] = v;
    return s;
}

class Recorder final : public System {
public:
    std::vector<std::string> heard;
    std::string_view name() const override { return "recorder"; }
    void deliver(World&, std::uint64_t, const std::optional<Guid>&, std::string_view message, const Json& data) override {
        heard.push_back(std::format("{} {}", message, data.value("player", 0)));
    }
};

} // namespace

TEST_CASE("gamepads: bindings by name, both ways, aliases included") {
    for (const char* name : {"Space", "MouseLeft", "Pad.South", "Pad.Start", "Pad.LB", "Pad.Up", "Pad.LeftX+", "Pad.RightY-", "Pad.LT", "Pad.RT"}) {
        const auto b = binding_from_name(name);
        REQUIRE_MESSAGE(b, name);
        CHECK(binding_name(*b) == name);
    }
    CHECK(binding_from_name("pad.a") == Binding::pad(PadButton::South));  // Xbox letters, any case
    CHECK(binding_from_name("Pad.Y") == Binding::pad(PadButton::North));
    CHECK_FALSE(binding_from_name("Pad.Banana"));
    const auto map = InputMap::from_json(Json::parse(R"({"actions": {"jump": ["Space", "Pad.A"]}, "dead_zone": 0.3})"));
    REQUIRE(map);
    CHECK(map->actions.at("jump") == std::vector<Binding>{Key::Space, Binding::pad(PadButton::South)});
    CHECK(map->dead_zone == doctest::Approx(0.3));
    CHECK_FALSE(InputMap::from_json(Json::parse(R"({"actions": {"jump": ["Pad.Banana"]}})")));
    CHECK_FALSE(InputMap::from_json(Json::parse(R"({"dead_zone": 1.5})")));
}

TEST_CASE("gamepads: the default map moves, jumps and pauses with a pad, and sticks are smooth past a round dead zone") {
    Input in;
    in.begin_frame(with_pad(1, {PadButton::South, PadButton::Start}, {{PadAxis::LeftX, 1.0f}}));
    CHECK(in.down("jump"));
    CHECK(in.pressed("pause"));
    CHECK(in.axis("move_x") == doctest::Approx(1.0));
    // Half way: past the 0.2 dead zone, rescaled (0.5 - 0.2) / 0.8.
    in.begin_frame(with_pad(1, {}, {{PadAxis::LeftX, -0.5f}}));
    CHECK(in.axis("move_x") == doctest::Approx(-0.375));
    CHECK(in.released("jump"));
    // Inside the dead zone: nothing, even diagonally (round, not per axis: 0.15 each way is 0.21 from the centre).
    in.begin_frame(with_pad(1, {}, {{PadAxis::LeftX, 0.1f}, {PadAxis::LeftY, 0.1f}}));
    CHECK(in.axis("move_x") == 0.0f);
    in.begin_frame(with_pad(1, {}, {{PadAxis::LeftX, 0.15f}, {PadAxis::LeftY, 0.15f}}));
    CHECK(in.axis("move_x") > 0.0f);
    // Stick up is move_y +1; the d-pad as well; a trigger fires past half way.
    in.begin_frame(with_pad(1, {}, {{PadAxis::LeftY, -1.0f}, {PadAxis::RightTrigger, 0.4f}}));
    CHECK(in.axis("move_y") == doctest::Approx(1.0));
    CHECK_FALSE(in.down("fire"));
    in.begin_frame(with_pad(1, {PadButton::DpadDown}, {{PadAxis::RightTrigger, 0.8f}}));
    CHECK(in.axis("move_y") == doctest::Approx(-1.0));
    CHECK(in.pressed("fire"));
}

TEST_CASE("gamepads: players: the keyboard is player 1's, each pad its player's") {
    Input in;
    InputState s = with_pad(2, {PadButton::South});
    s.set(Key::D);
    in.begin_frame(s);
    CHECK_FALSE(in.down("jump"));  // player 1: no pad, no Space
    CHECK(in.down("jump", 2));
    CHECK(in.axis("move_x") == doctest::Approx(1.0));  // D
    CHECK(in.axis("move_x", 2) == 0.0f);               // the keyboard isn't player 2's
    CHECK_FALSE(in.down("jump", 5));                   // no such player
    // A disconnected pad reads nothing.
    s.pads[1].connected = false;
    in.begin_frame(s);
    CHECK_FALSE(in.down("jump", 2));
}

TEST_CASE("gamepads: what was pressed, for a settings menu") {
    Input in;
    in.begin_frame({});
    InputState s;
    s.set(Key::J);
    in.begin_frame(s);
    CHECK(binding_name(*in.last_pressed()) == "J");
    in.begin_frame(s);  // still held: nothing new
    CHECK_FALSE(in.last_pressed());
    in.begin_frame(with_pad(1, {PadButton::North}));
    CHECK(binding_name(*in.last_pressed()) == "Pad.North");
    in.begin_frame(with_pad(1, {}, {{PadAxis::RightX, -0.9f}}));
    CHECK(binding_name(*in.last_pressed()) == "Pad.RightX-");
    in.begin_frame(with_pad(3, {PadButton::East}));
    CHECK_FALSE(in.last_pressed());  // player 3's pad isn't player 1's
    in.begin_frame({});
    in.begin_frame(with_pad(3, {PadButton::East}));
    CHECK(binding_name(*in.last_pressed(3)) == "Pad.East");
}

TEST_CASE("gamepads: plugged in and out, as game-wide messages") {
    scene::Scene sc;
    sc.id = Guid::generate();
    scene::Entity e;
    e.id = Guid::generate();
    sc.entities.push_back(e);
    World w(std::move(sc));
    auto rec = std::make_unique<Recorder>();
    Recorder* r = rec.get();
    w.add_system(std::move(rec));
    w.listen(std::nullopt, "pad_connected", e.id, r, 1);
    w.listen(std::nullopt, "pad_disconnected", e.id, r, 2);
    w.frame(1.0 / 60.0, {});
    w.frame(1.0 / 60.0, with_pad(2, {}));
    w.frame(1.0 / 60.0, with_pad(2, {}));
    w.frame(1.0 / 60.0, {});
    CHECK(r->heard == std::vector<std::string>{"pad_connected 2", "pad_disconnected 2"});
}

TEST_CASE("gamepads: rebinding from scripts, reset, saved and read back at the next start") {
    const auto dir = std::filesystem::temp_directory_path() / ("noor_rebind_" + Guid::generate().to_string());
    World w(scene::Scene{});
    const auto req = [&](Json j) { return api::input(w, j); };
    const auto names = [&](const char* name) { return std::get<Json>(req({{"op", "bindings"}, {"name", name}})); };
    CHECK(names("jump") == Json::array({"Space", "Pad.South"}));
    CHECK(names("move_x+") == Json::array({"D", "Right", "Pad.LeftX+", "Pad.Right"}));

    // Space for jump becomes J; then E (interact's) for jump too: it moves.
    CHECK(std::get<Json>(req({{"op", "rebind"}, {"name", "jump"}, {"old", "Space"}, {"new", "J"}})) == Json::array({"J", "Pad.South"}));
    CHECK(std::get<Json>(req({{"op", "rebind"}, {"name", "jump"}, {"new", "E"}})) == Json::array({"J", "Pad.South", "E"}));
    CHECK(names("interact") == Json::array({"Pad.West"}));
    CHECK(std::get<Json>(req({{"op", "rebind"}, {"name", "jump"}, {"old", "Pad.South"}})) == Json::array({"J", "E"}));
    // Mistakes say what.
    CHECK(std::holds_alternative<std::string>(req({{"op", "rebind"}, {"name", "jump"}, {"new", "Hyperdrive"}})));
    CHECK(std::holds_alternative<std::string>(req({{"op", "rebind"}, {"name", "teleport"}, {"new", "T"}})));
    CHECK(std::holds_alternative<std::string>(req({{"op", "rebind"}, {"name", "jump"}, {"old", "Q"}, {"new", "T"}})));
    CHECK(std::holds_alternative<std::string>(req({{"op", "save"}})));  // nowhere to save in this world

    // Saved: only what changed goes in the player's file; read over the game's own at the next start.
    w.set_input_overrides(dir / "input.json");
    CHECK(std::holds_alternative<Json>(req({{"op", "save"}})));
    const auto saved = Json::parse(*fs::read_file(dir / "input.json"));
    CHECK(saved["actions"].contains("jump"));
    CHECK(saved["actions"].contains("interact"));
    CHECK_FALSE(saved["actions"].contains("pause"));
    auto next = InputMap::load(dir / "no_project_file.json", dir / "input.json");
    REQUIRE(next);
    CHECK(next->actions.at("jump") == std::vector<Binding>{Key::J, Key::E});
    CHECK(next->actions.at("pause") == InputMap::defaults().actions.at("pause"));

    // Reset: one action, then all.
    CHECK(std::get<Json>(req({{"op", "reset"}, {"name", "jump"}})) == Json::array({"Space", "Pad.South"}));
    CHECK(names("interact") == Json::array({"Pad.West"}));
    req({{"op", "reset"}});
    CHECK(names("interact") == Json::array({"E", "Pad.West"}));
    // The pads, all four, none connected.
    const auto pads = std::get<Json>(req({{"op", "pads"}}));
    CHECK(pads.size() == 4);
    CHECK(pads[1]["player"] == 2);
    CHECK_FALSE(pads[1]["connected"].get<bool>());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
