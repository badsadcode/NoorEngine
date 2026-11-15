#pragma once
// C# scripting (ADR-008) as a runtime system (ADR-027).
//
// An entity runs a C# script through its `script` component, like Lua:
//   "script": {"file": "Scripts/Spinner.cs", "params": {"speed": 90}}
// The class name is the file name (Spinner), compiled into the project's script assembly.
//
// This system only does the engine's half: it asks the managed host (noor_rt_host, implemented
// by NoorEngine.Scripting) to create instances and run hooks, in the same order as Lua's. Scripts
// call back through rt_api() (noor_rt_api). Without a host (no .NET, or the scripts failed to
// compile) C# scripts are reported once and skipped; the game still runs.

#include "noor/runtime/world.h"
#include "noor_rt_api.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::scripting {

// The engine functions scripts call. Valid for the process lifetime.
[[nodiscard]] const noor_rt_api* rt_api();

class CSharpScriptSystem final : public runtime::System {
public:
    explicit CSharpScriptSystem(const noor_rt_host* host) : host_(host) {}
    ~CSharpScriptSystem() override;

    [[nodiscard]] std::string_view name() const override { return "csharp"; }
    [[nodiscard]] bool starts_late() const override { return true; }
    void entity_started(runtime::World& world, scene::Entity& entity) override;
    void update(runtime::World& world, double dt) override;
    void fixed_update(runtime::World& world, double dt) override;
    void late_update(runtime::World& world, double dt) override;
    void entity_destroyed(runtime::World& world, const scene::Entity& entity) override;
    void shutdown(runtime::World& world) override;

    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }
    [[nodiscard]] std::size_t instance_count() const noexcept { return instances_.size(); }
    [[nodiscard]] static bool handles(const std::filesystem::path& file) { return file.extension() == ".cs"; }

private:
    void report(std::string message);
    void each(runtime::World& world, int hook, double dt);
    bool call(runtime::World& world, const Guid& id, int hook, double dt);
    void release(const Guid& id);

    const noor_rt_host* host_;
    std::unordered_map<Guid, std::int64_t> instances_;
    std::vector<Guid> order_;
    std::vector<std::string> errors_;
    bool reported_missing_host_ = false;
};

// Converts entity ids for the ABI.
[[nodiscard]] noor_rt_entity to_rt(const Guid& id);
[[nodiscard]] Guid from_rt(const noor_rt_entity& e);

} // namespace noor::scripting
