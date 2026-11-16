# ADR-028: The game module, noor_player and game builds

**Status:** Accepted · **Date:** 2026-11-16 · **Milestone:** 4 (part 6)

## Context
Until now a NoorEngine game only ran inside NoorEditor: `Document::play()` built the world (scripts,
physics, audio, behaviours) and the editor viewport drew it. Spec 30 and the Milestone 4 gate need
a game that runs on its own: a player executable and a way to package a project for it. ADR-027
already planned "one runtime that the editor's play mode and a standalone player both use"; this ADR
says where the shared code lives, what a build is, and what is deliberately left for later.

## Options
1. **Packaging**
   - **A. A build is the project's content plus the player.** The player reads the same files the
     editor does (scenes, .noormeta, GLBs, scripts) and rescans the registry at start.
   - **B. Cooked builds:** convert everything to engine-ready binary packs (meshes, compressed
     textures, compiled scripts, one archive). Faster loading and harder to tamper with, but needs
     a cook pipeline, a pack format and its versioning before anything runs.
2. **Shared code**
   - **A. A `game` module** (static library) that both editor_api and noor_player link.
   - **B. Make noor_player a client of noor_editor_api.dll.** Reuses everything but ships an editor
     DLL (undo/redo, gizmos, file watching) inside every game.

## Decision
**1A and 2A.**
- **`engine/game`** (depends on asset, scene, runtime, render; physics, audio and scripting
  privately): `make_world()` (the world with every system, in the editor's order), `collect_view()`
  (models, primitive shapes, the game camera, from a running scene), `SceneRenderer` (draws a view
  with the renderer; loads models on first use), and the primitive meshes. The editor's Play now goes
  through `make_world()` and its snapshots through `collect_view()`, so editor and player can't drift.
  The editor viewport keeps its own drawing (it needs picking data, highlights and gizmos).
- **`noor_player`** (tools/noor_player): an SDL window, the renderer, one `World`. Input from the
  platform layer is mapped to `runtime::InputState`; scripts ask for the mouse (`capture_mouse`),
  Esc always releases it. Without a camera in the scene it warns once and looks at the origin.
  `noor_player <project>` runs a project folder directly (useful while developing).
- **`noor_cli build <project> <out> [--scene]`**: validates the project (a broken project is a broken
  game), then writes
  ```
  <out>/<Project Name>.exe   the player, renamed
  <out>/game.json            {"title", "project": "game", "start_scene", "executable", "engine", "built"}
  <out>/game/                the project's files, minus .noor/, .git/, Scripts/bin|obj, *.bak, *.tmp,
                             and C# sources
  <out>/managed/             with C# scripts: Scripts.dll (+pdb) and NoorEngine.Scripting (part 6b)
  ```
  The default start scene is `Scenes/Main.noorscene`, else the first scene by path
  (`asset::Project::default_start_scene`); the editor passes the open scene. Rebuilding replaces an
  earlier build; a non-empty folder that isn't a build, or a folder inside the project, is refused.
  noor_cli stays headless (no Vulkan): it copies the player executable, it doesn't link it.
- NoorEditor: **File ▸ Build Game…** runs noor_cli (copied next to the editor with noor_player) and
  shows its output in the Console.

## Consequences
- A build runs on a Windows PC with a Vulkan 1.3 GPU and the **Visual C++ 2015–2022 runtime**
  (usually installed). SDL is linked statically. Debug builds need Visual Studio's debug runtime:
  noor_cli warns. Shipping the runtime app-local is later work.
- Content is plain files: anyone can read or edit a built game's scenes and scripts. Fine for now;
  cooking/packing (1B) is a later milestone and will be its own ADR.
- Every start rescans the registry (hashes every asset). Fine for small games; the cook step will
  remove it.
- **C# scripts (part 6b):** compiled at build time and run through .NET started by the player
  (ADR-008 implementation notes). The PC needs the .NET 10 runtime; building needs the .NET SDK.
  `noor_player <project>` doesn't compile C#: it says so and points at the editor or a build.

## Tests
`cli.build` (headless, CI): build layout, rebuilds, refusals. `cli.build_csharp` and
`cli.build_csharp_error` (CI): C# compiled into the build; a compile error stops it before anything
is written and names the file and line. `player.built_csharp_game` (GPU): the built game ran a C#
script. `player.project` and
`player.built_game` (GPU, local): 120 fixed frames of `tests/data/player_project`, checked from the
player's `--report`: the ball rests on the floor (physics), a Lua script lifted a box exactly 2 m,
and the centre pixel is the red box seen through the scene's camera. The editor smoke test runs
File ▸ Build Game.

## Reviewed by
Sage.
