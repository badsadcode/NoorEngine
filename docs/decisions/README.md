# Architecture Decision Records

Template: [ADR-000](ADR-000-template.md). IDs follow the decision register in the
[master spec, section 33](../NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md).

| ADR | Decision | Status |
|---|---|---|
| [001](ADR-001-core-language-and-toolchain.md) | C++23 subset, CMake, MSVC first | Accepted |
| [002](ADR-002-graphics-backend.md) | Vulkan 1.3, single backend, own thin RHI | Accepted |
| [003](ADR-003-editor-gui-framework.md) | Avalonia (C#) + Dock for NoorEditor | Accepted after spike (open verification items) |
| [004](ADR-004-scene-model.md) | Entities + typed components, JSON scenes, unknown components kept | Accepted |
| [005](ADR-005-project-serialization.md) | Deterministic JSON, atomic saves with backup, path rules | Accepted |
| [006](ADR-006-asset-registry.md) | `.noormeta` sidecar GUIDs, move repair by content hash | Accepted |
| [008](ADR-008-dotnet-hosting.md) | .NET 10 CoreCLR for C# scripts: in-process in the editor (collectible load context), hostfxr in the player | Accepted |
| [009](ADR-009-lua-integration.md) | Lua: Luau (sandboxing, types) over Lua 5.4 / LuaJIT | Accepted |
| [010](ADR-010-physics.md) | Physics: Jolt (MIT), fixed 60 Hz tick, `CharacterVirtual` controller | Accepted |
| [011](ADR-011-audio.md) | Audio: miniaudio (public domain / MIT-0), buses, 3D sources | Accepted |
| [019](ADR-019-licensing.md) | MIT license | Accepted |
| [021](ADR-021-platform-layer-and-dependencies.md) | SDL3 platform layer, pinned-dependency policy | Accepted |
| [022](ADR-022-math-and-coordinate-conventions.md) | Right-handed, +Y up, meters, column-major, reverse-Z | Accepted |
| [023](ADR-023-noor-interchange.md) | Noor Interchange: GLB + `extras.noor` markers + `.noor.json` manifest for all Noor tools | Proposed (spec v0.3; BadBuilding exports v0.2) |
| [024](ADR-024-prefabs.md) | Prefabs: instance + overrides stored, expanded on load with derived ids | Accepted |
| [025](ADR-025-editor-engine-boundary.md) | Editor/engine boundary: the engine owns the document, the editor sends commands (C API, JSON) | Accepted |
| [026](ADR-026-play-mode.md) | Play mode: a runtime copy of the scene, built-in behaviours until scripting, edits refused while playing | Accepted |
| [027](ADR-027-runtime-and-scripting-api.md) | Runtime module: `World`, frame order, fixed step, input actions, one `noor_rt_*` API for C# and Lua | Accepted |
| [028](ADR-028-player-and-builds.md) | The `game` module shared by Play and `noor_player`; `noor_cli build` (player + project content + game.json) | Accepted |

Still open from the register: 007 plugin ABI, 012 PBR model, 013 world coordinates,
014–018, 020 branding.
