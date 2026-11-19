// 8-direction sprites and animations played by scripts (Milestone 8B, part 6, ADR-035): the direction a sprite shows
// from where the camera is, a walk round a circle frame by frame, scripts playing walk then attack and getting the end
// event, speed and stop, and emitter bursts from scripts. Coth's sheet (The Uncounted) is the 8-direction test sheet.
#include "noor/asset/sprite_asset.h"
#include "noor/fx/particle_system.h"
#include "noor/game/sprite_animation.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace noor;
using asset::Json;

namespace {

const std::filesystem::path kCoth = std::filesystem::path(NOOR_TEST_DATA_DIR) / "render_scenes" / "Assets" / "Sprites" / "coth.noorsprite";

asset::SpriteSheet coth_sheet() {
    auto s = asset::load_sprite_file(kCoth);
    REQUIRE(s.has_value());
    REQUIRE(asset::slice(*s, 1024, 768).has_value());  // coth.png's size
    return *s;
}

// An entity at `pos` turned `yaw` degrees about +Y (forward -Z turned with it).
math::Mat4 placed(math::Vec3 pos, float yaw) {
    return math::translation(pos) * math::rotation({0, 1, 0}, yaw * 3.14159265f / 180.0f);
}

} // namespace

TEST_CASE("sprite directions: 0 faces the viewer, 2 their left, 4 away, 6 their right") {
    const math::Vec3 eye{0, 1.7f, 10};  // the camera in front (+Z), looking at the origin
    CHECK(game::sprite_direction(placed({0, 0, 0}, 180), eye, 8) == 0);  // forward +Z: towards the camera
    CHECK(game::sprite_direction(placed({0, 0, 0}, 90), eye, 8) == 2);   // forward -X: the viewer's left
    CHECK(game::sprite_direction(placed({0, 0, 0}, 0), eye, 8) == 4);    // forward -Z: away
    CHECK(game::sprite_direction(placed({0, 0, 0}, 270), eye, 8) == 6);  // forward +X: the viewer's right
    CHECK(game::sprite_direction(placed({0, 0, 0}, 135), eye, 8) == 1);  // between
    CHECK(game::sprite_direction(placed({0, 0, 0}, 160), eye, 8) == 0);  // nearest wins (20 degrees off front)
    CHECK(game::sprite_direction(placed({0, 0, 0}, 180), eye, 1) == 0);  // not a directional sheet
    // It's the angle to the camera that counts, not the world: seen from +X (the viewer's right is then -Z), a sprite
    // facing +Z faces the viewer's left.
    CHECK(game::sprite_direction(placed({0, 0, 0}, 180), {10, 1.7f, 0}, 8) == 2);

    const auto sheet = coth_sheet();
    CHECK(sheet.directions == 8);
    // Walk frames name the direction-0 column; direction d is d columns along. "die" isn't directional.
    CHECK(sheet.frame_at("walk", 0.0, 0) == 8);
    CHECK(sheet.frame_at("walk", 0.0, 3) == 11);
    CHECK(sheet.frame_at("walk", 1.0 / 6.0, 3) == 19);
    CHECK(sheet.frame_at("die", 10.0, 5) == 44);
    CHECK(sheet.animation("attack")->duration() == doctest::Approx(1.0 / 6.0));
    // A sheet whose columns aren't a multiple of its directions is refused.
    auto bad = asset::parse_sprite_sheet(Json{{"noor_sprite", 1}, {"grid", {{"columns", 6}, {"rows", 1}}}, {"directions", 8}});
    REQUIRE(bad.has_value());
    CHECK(!asset::slice(*bad, 600, 100).has_value());
}

TEST_CASE("sprite directions: walking a circle shows each of the 8 directions in turn, frame by frame") {
    const auto sheet = coth_sheet();
    const math::Vec3 eye{0, 2, 14};
    const Json walk{{"animation", "walk"}, {"started", 0.0}, {"speed", 1.0}};
    std::vector<int> seen;
    int changes = 0;
    for (int frame = 0; frame <= 480; ++frame) {  // 8 s at 60 Hz: one lap
        const double t = frame / 60.0;
        const double a = t / 8.0 * 2.0 * 3.14159265358979;
        const math::Vec3 pos{3.0f * static_cast<float>(std::sin(a)), 0, 3.0f * static_cast<float>(std::cos(a))};
        // Facing along the way round: the velocity (cos a, 0, -sin a) as the entity's forward (-Z).
        const float yaw = static_cast<float>((a - 3.14159265358979 / 2.0) * 180.0 / 3.14159265358979);
        const auto world = placed(pos, yaw);
        const int direction = game::sprite_direction(world, eye, 8);
        const int shown = game::sprite_frame(sheet, walk, world, eye, t);
        const int step = static_cast<int>(std::floor(t * 6.0 + 1e-9)) % 2;
        REQUIRE(shown == (step == 0 ? 8 : 16) + direction);  // the walk's frame, in this frame's direction
        if (seen.empty() || seen.back() != direction) {
            if (!seen.empty()) {
                CHECK(((direction - seen.back() + 8) % 8 == 1 || (seen.back() - direction + 8) % 8 == 1));  // one step at a time
                ++changes;
            }
            seen.push_back(direction);
        }
    }
    std::vector<int> sorted = seen;
    std::ranges::sort(sorted);
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    CHECK(sorted.size() == 8);  // every direction
    CHECK(changes >= 8);        // round the whole way (all eight turns, at least)
}

TEST_CASE("sprite scripts: play walk, then attack, get the end event, then the next animation") {
    scene::Scene s;
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Coth";
    const Guid sheet_id = Guid::generate();
    e.components["sprite"] = {{"sheet", sheet_id.to_string()}, {"animation", "idle"}};
    e.components["script"] = {{"file", "Scripts/coth.luau"}};
    s.entities.push_back(e);
    runtime::World world(std::move(s));
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/coth.luau", R"(
        local Coth = {}
        function Coth:on_start()
            self.story = {}
            self.entity:play_animation("walk")
        end
        function Coth:on_update(dt)
            self.t = (self.t or 0) + dt
            if self.t >= 0.5 and not self.attacked then
                self.attacked = true
                table.insert(self.story, "attack@" .. tostring(noor.time()))
                self.entity:play_animation("attack")
            end
        end
        function Coth:on_animation_end(name)
            table.insert(self.story, name .. " ended")
            if name == "attack" then self.entity:play_animation("die", 2) end
            self.entity:set("story", { text = table.concat(self.story, ", "), now = self.entity:animation() })
        end
        return Coth
    )");
    auto* scripts = lua.get();
    world.add_system(std::make_unique<game::SpriteAnimationSystem>([&](const Guid& id) -> std::optional<std::filesystem::path> {
        if (id == sheet_id) return kCoth;
        return std::nullopt;
    }));
    world.add_system(std::move(lua));
    for (int i = 0; i < 120; ++i) world.frame(1.0 / 60.0, {});  // 2 s
    CHECK(scripts->errors().empty());
    const auto* c = world.find(e.id);
    REQUIRE(c != nullptr);
    REQUIRE(c->components.contains("story"));
    const std::string story = c->components.at("story")["text"].get<std::string>();
    // The walk loops (no end); the attack (1/6 s) ends and the die (2 frames at 3 fps, played at double speed) ends.
    CHECK(story.find("attack ended") != std::string::npos);
    CHECK(story.find("die ended") != std::string::npos);
    CHECK(story.find("walk ended") == std::string::npos);
    CHECK(story.find("attack ended") < story.find("die ended"));
    CHECK(c->components.at("sprite")["animation"] == "die");
    auto* system = world.system<game::SpriteAnimationSystem>();
    CHECK(system->ended() == 2);  // once each, not every frame after
}

TEST_CASE("sprite scripts: speed changes don't jump, stop freezes the frame") {
    runtime::World world{scene::Scene{}};
    scene::Entity e;
    e.id = Guid::generate();
    e.components["sprite"] = {{"sheet", Guid::generate().to_string()}};
    world.spawn(e);
    world.frame(1.0, {});
    REQUIRE(runtime::api::sprite_play(world, e.id, "walk", 1.0f));
    for (int i = 0; i < 10; ++i) world.frame(0.05, {});  // 0.5 s in
    const auto& c = world.find(e.id)->components.at("sprite");
    const double before = game::sprite_time(c, world.time());
    runtime::api::sprite_set_speed(world, e.id, 3.0f);
    CHECK(game::sprite_time(world.find(e.id)->components.at("sprite"), world.time()) == doctest::Approx(before));  // no jump
    world.frame(0.1, {});
    CHECK(game::sprite_time(world.find(e.id)->components.at("sprite"), world.time()) == doctest::Approx(before + 0.3));  // 3x
    runtime::api::sprite_stop(world, e.id);
    const double frozen = game::sprite_time(world.find(e.id)->components.at("sprite"), world.time());
    world.frame(0.1, {});
    CHECK(game::sprite_time(world.find(e.id)->components.at("sprite"), world.time()) == doctest::Approx(frozen));
    CHECK(runtime::api::sprite_animation(world, e.id) == "walk");
    CHECK(!runtime::api::sprite_play(world, Guid::generate(), "walk"));  // no such entity: nothing happens
}

TEST_CASE("emitter scripts: a burst from a script spawns that many particles at the next fixed step") {
    scene::Scene s;
    scene::Entity e;
    e.id = Guid::generate();
    e.components["emitter"] = {{"rate", 0}, {"lifetime", 5}, {"seed", 3}};
    e.components["script"] = {{"file", "Scripts/burst.luau"}};
    s.entities.push_back(e);
    runtime::World world(std::move(s));
    world.add_system(std::make_unique<fx::ParticleSystem>());
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{".", 0.25});
    lua->add_source("Scripts/burst.luau", R"(
        local B = {}
        function B:on_update(dt)
            self.n = (self.n or 0) + 1
            if self.n == 3 then self.ok = self.entity:burst(25) ; self.entity:set("result", { ok = self.ok }) end
        end
        return B
    )");
    auto* scripts = lua.get();
    world.add_system(std::move(lua));
    for (int i = 0; i < 6; ++i) world.frame(1.0 / 60.0, {});
    CHECK(scripts->errors().empty());
    CHECK(world.find(e.id)->components.at("result")["ok"] == true);
    const auto* ps = world.system<fx::ParticleSystem>();
    CHECK(ps->stats().particles == 25);  // rate 0: only the burst's
}
