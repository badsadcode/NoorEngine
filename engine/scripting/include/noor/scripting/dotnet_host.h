#pragma once
// Hosting .NET in a native program (ADR-008), for noor_player: NoorEditor is a .NET program already
// and hands the engine its host table itself.
//
// Uses hostfxr, the official hosting entry point of an installed .NET runtime (no SDK needed on the
// player's PC, only the .NET 10 runtime). Found through DOTNET_ROOT, else %ProgramFiles%\dotnet.

#include "noor/foundation/error.h"
#include "noor_rt_api.h"

#include <filesystem>

namespace noor::scripting {

// Starts .NET with NoorEngine.Scripting from `managed_dir` (NoorEngine.Scripting.dll and its
// .runtimeconfig.json), loads the compiled scripts `scripts_dll`, and returns the noor_rt_host table
// for CSharpScriptSystem. .NET can be started once per process; a second call returns the same
// table when the scripts are the same file, or an error.
[[nodiscard]] Result<const noor_rt_host*> start_dotnet(const std::filesystem::path& managed_dir,
                                                       const std::filesystem::path& scripts_dll);

// The hostfxr library that start_dotnet would use, or an error saying why there is none.
[[nodiscard]] Result<std::filesystem::path> find_hostfxr();

} // namespace noor::scripting
