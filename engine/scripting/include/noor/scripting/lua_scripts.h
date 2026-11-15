#pragma once
// Lua scripting (ADR-009: Luau) as a runtime system (ADR-027).
//
// An entity runs a script through its `script` component:
//   "script": {"file": "Scripts/spinner.luau", "params": {"speed": 90}}
//
// A script file returns a table of hooks; each entity gets its own instance (`self`) whose
// fields `entity` (the entity) and `params` (the component's params) are set by the engine:
//
//   local Spinner = {}
//   function Spinner:on_start() noor.log("hello from", self.entity:name()) end
//   function Spinner:on_update(dt) self.entity:rotate(vector.create(0, 1, 0), self.params.speed * dt) end
//   return Spinner
//
// Hooks: on_start, on_update(dt), on_fixed_update(dt), on_late_update(dt), on_destroy, and with
// physics on_collision_enter(other), on_collision_exit(other), on_trigger_enter(other),
// on_trigger_exit(other) (called before on_late_update in the frame they happen).
// Sandbox: Luau's safe library set (no file, OS or process access; globals and built-in libraries
// read-only; each file gets its own globals). A hook that runs longer than the call budget is
// stopped. A script that errors is reported (file, line, stack) and switched off for that entity;
// the game keeps running.
//
// The API scripts see (global `noor`, entity methods) is documented in docs/scripting/LUA_API.md.

#include "noor/runtime/world.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace noor::scripting {

class LuaScriptSystem final : public runtime::System {
public:
    struct Options {
        std::filesystem::path project_root;  // script paths are relative to it
        double call_budget_seconds = 0.25;   // per hook call
    };

    explicit LuaScriptSystem(Options options);
    ~LuaScriptSystem() override;
    LuaScriptSystem(const LuaScriptSystem&) = delete;
    LuaScriptSystem& operator=(const LuaScriptSystem&) = delete;

    [[nodiscard]] std::string_view name() const override { return "lua"; }
    void entity_started(runtime::World& world, scene::Entity& entity) override;
    void update(runtime::World& world, double dt) override;
    void fixed_update(runtime::World& world, double dt) override;
    void late_update(runtime::World& world, double dt) override;
    void entity_destroyed(runtime::World& world, const scene::Entity& entity) override;
    void shutdown(runtime::World& world) override;

    // Source for a script path, used instead of reading the file (tests, generated scripts).
    void add_source(std::string path, std::string source);

    // Every error reported so far ("file:line: message" plus a stack trace).
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept;
    // Script instances currently running (one per entity with a working script).
    [[nodiscard]] std::size_t instance_count() const noexcept;

    // True for files this system runs (.lua, .luau).
    [[nodiscard]] static bool handles(const std::filesystem::path& file);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::scripting
