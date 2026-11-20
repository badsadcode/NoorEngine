// The engine half of C# scripting (ADR-008): CSharpScriptSystem driving a host through
// noor_rt_host, and scripts calling back through noor_rt_api. A fake host stands in for .NET here;
// the editor smoke test runs real C# scripts.
#include "noor/runtime/script_api.h"
#include "noor/scripting/csharp_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace noor;
using scripting::CSharpScriptSystem;

namespace {

// A pretend "Mover" class: moves +X by params.speed per second, logs hooks, throws on request.
struct Fake {
    static inline std::vector<std::string> calls;
    static inline std::string error;
    static inline std::int64_t next = 1;
    static inline std::unordered_map<std::int64_t, std::pair<noor_rt_entity, double>> objects;  // entity, speed
    static inline bool throw_on_update = false;

    static std::int64_t create(noor_rt_entity e, const char* cls, const char* params) {
        if (std::string(cls) != "Mover") {
            error = std::string("no class ") + cls;
            return 0;
        }
        const auto j = asset::Json::parse(params);
        objects[next] = {e, j.value("speed", 1.0)};
        calls.push_back("create");
        return next++;
    }
    static int32_t call(std::int64_t h, int32_t hook, float dt) {
        calls.push_back("hook " + std::to_string(hook));
        if (hook == NOOR_RT_UPDATE) {
            if (throw_on_update) {
                error = "NullReferenceException at Mover.OnUpdate";
                return 0;
            }
            const auto* api = scripting::rt_api();
            api->translate(objects[h].first, {static_cast<float>(objects[h].second * dt), 0, 0}, 0);
            if (api->input(NOOR_RT_PRESSED, "jump")) api->set_component(objects[h].first, "jumped", "{\"yes\": true}");
        }
        return 1;
    }
    static int32_t event(std::int64_t, int32_t kind, noor_rt_entity) {
        calls.push_back("event " + std::to_string(kind));
        if (kind == NOOR_RT_ANIMATION_END) calls.push_back(std::string("animation ") + scripting::rt_api()->event_name());
        return 1;
    }
    static void release(std::int64_t h) {
        objects.erase(h);
        calls.push_back("release");
    }
    static const char* last_error() { return error.c_str(); }
    static void reset() {
        calls.clear();
        objects.clear();
        error.clear();
        throw_on_update = false;
    }
};

const noor_rt_host kFakeHost{Fake::create, Fake::call, Fake::event, Fake::release, Fake::last_error};

scene::Scene scene_with(const std::string& file, double speed) {
    scene::Scene s;
    scene::Entity e;
    e.id = Guid::generate();
    e.name = "Cube";
    e.components["script"] = {{"file", file}, {"params", {{"speed", speed}}}};
    s.entities.push_back(e);
    return s;
}

} // namespace

TEST_CASE("csharp host: instances are created, hooks run in order, scripts call back into the world") {
    Fake::reset();
    runtime::World w(scene_with("Scripts/Mover.cs", 2.0));
    w.add_system(std::make_unique<CSharpScriptSystem>(&kFakeHost));
    runtime::InputState jump;
    jump.set(runtime::Key::Space);
    for (int i = 0; i < 30; ++i) w.frame(1.0 / 30.0, i == 3 ? jump : runtime::InputState{});  // 1 s
    const Guid id = w.scene().entities[0].id;
    CHECK(runtime::api::position(w, id)->x == doctest::Approx(2.0f).epsilon(1e-3));
    CHECK(runtime::api::component(w, id, "jumped") != nullptr);
    REQUIRE(Fake::calls.size() > 3);
    CHECK(Fake::calls[0] == "create");
    CHECK(Fake::calls[1] == "hook 0");  // OnStart
    CHECK(Fake::calls[2] == "hook 1");  // OnUpdate
    CHECK(Fake::calls[3] == "hook 2");  // OnFixedUpdate (2 per 1/30 s frame)
    CHECK(Fake::calls[4] == "hook 2");
    CHECK(Fake::calls[5] == "hook 3");  // OnLateUpdate
}

TEST_CASE("csharp host: sprites through the ABI, and the animation end event carries its name") {
    Fake::reset();
    runtime::World w(scene_with("Scripts/Mover.cs", 0.0));
    const Guid id = w.scene().entities[0].id;
    w.scene().entities[0].components["sprite"] = {{"sheet", Guid::generate().to_string()}};
    w.add_system(std::make_unique<CSharpScriptSystem>(&kFakeHost));
    w.frame(1.0 / 60.0, {});
    const auto* api = scripting::rt_api();
    const auto e = scripting::to_rt(id);
    // Outside a hook the API does nothing (scripts only call it from hooks); post the event the sprite system would.
    w.post({runtime::Event::Kind::AnimationEnd, id, id, "attack"});
    w.frame(1.0 / 60.0, {});
    CHECK(std::ranges::find(Fake::calls, "event 4") != Fake::calls.end());
    CHECK(std::ranges::find(Fake::calls, "animation attack") != Fake::calls.end());
    CHECK(api->version == 11u);  // v5 (8C part 3): cross-fades and animation parameters; v6 (part 4): bones, events; v7 (8D): water; v8: characters in water; v9: flow; v10: jump release, drop; v11: shape casts, overlaps, contacts
    REQUIRE(api->bone != nullptr);
    REQUIRE(api->water != nullptr);
    float height = 1.0f;
    CHECK(api->water(0.0f, 0.0f, &height, nullptr) == 0);  // no world outside a hook: no water, nothing written
    CHECK(height == 1.0f);
    REQUIRE(api->character_water != nullptr);
    CHECK(api->character_water(e, nullptr, nullptr) == -1);  // no world outside a hook: not a character
    REQUIRE(api->water_flow != nullptr);
    CHECK(api->water_flow(0.0f, 0.0f, nullptr) == 0);
    CHECK(api->sprite(e, NOOR_RT_SPRITE_PLAY, "walk", 1.0f) == 0);  // no world outside a hook: refused, not a crash
    REQUIRE(api->animation_parameter != nullptr);
    CHECK(api->animation_parameter(e, "speed", 1.0f, NOOR_RT_PARAM_NUMBER) == 0);
    CHECK(api->sprite(e, NOOR_RT_SPRITE_CROSS_FADE, "walk", 0.2f) == 0);
}

TEST_CASE("csharp host: an exception stops that script, missing classes and hosts are reported") {
    Fake::reset();
    {
        runtime::World w(scene_with("Scripts/Mover.cs", 1.0));
        auto sys = std::make_unique<CSharpScriptSystem>(&kFakeHost);
        auto* s = sys.get();
        w.add_system(std::move(sys));
        w.frame(0.016, {});
        Fake::throw_on_update = true;
        w.frame(0.016, {});
        REQUIRE(s->errors().size() == 1);
        CHECK(s->errors()[0].find("NullReferenceException") != std::string::npos);
        CHECK(s->errors()[0].find("OnUpdate on 'Cube'") != std::string::npos);
        CHECK(s->instance_count() == 0);
        CHECK(Fake::calls.back() == "release");
    }
    {
        runtime::World w(scene_with("Scripts/Nope.cs", 1.0));
        auto sys = std::make_unique<CSharpScriptSystem>(&kFakeHost);
        auto* s = sys.get();
        w.add_system(std::move(sys));
        w.frame(0.016, {});
        REQUIRE(s->errors().size() == 1);
        CHECK(s->errors()[0].find("no class Nope") != std::string::npos);
    }
    {
        runtime::World w(scene_with("Scripts/Mover.cs", 1.0));
        auto sys = std::make_unique<CSharpScriptSystem>(nullptr);
        auto* s = sys.get();
        w.add_system(std::move(sys));
        w.frame(0.016, {});
        w.frame(0.016, {});
        REQUIRE(s->errors().size() == 1);  // once, not every frame
        CHECK(s->errors()[0].find(".NET script host") != std::string::npos);
    }
}

TEST_CASE("csharp host: entity ids round-trip through the ABI, Lua files are not ours") {
    const Guid g = Guid::generate();
    CHECK(scripting::from_rt(scripting::to_rt(g)) == g);
    CHECK(CSharpScriptSystem::handles("Scripts/A.cs"));
    CHECK_FALSE(CSharpScriptSystem::handles("Scripts/A.luau"));
    CHECK(scripting::rt_api()->version == NOOR_RT_API_VERSION);
    CHECK(scripting::rt_api()->size == sizeof(noor_rt_api));
}
