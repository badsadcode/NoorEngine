# NoorEditor

The NoorEngine editor: Avalonia + Dock (ADR-003) on top of the engine's C API,
`noor_editor_api.dll` (ADR-025). The engine owns the project, scene, undo history and asset
registry; the editor shows JSON snapshots of them and changes things only through engine
commands, so every edit is undoable and the editor holds no second copy of the scene.

## What works (Milestone 3)

- **Project hub**: recent projects, New Project (creates the folder and an empty `Scenes/Main.noorscene`), Open Project.
- **Viewport**: the engine renders the open scene (GLB models, prefab instances, markers) on its
  own thread. Float the viewport tab into its own window and it keeps rendering.
- **Camera**: **hold the right mouse button** to look around and fly with **WASD**, **Q/E** for
  down/up, **Shift** for 4x speed, the **wheel** (while holding right) to change the fly speed.
  **Alt + left-drag** orbits, **middle-drag** pans, the **wheel** zooms, **F** frames the
  selection (or everything).
- **Selecting in the viewport**: click an object to select all of it (a whole building); click it
  again to go one level deeper, toward the part you clicked. Click empty space to deselect. The
  selection is tinted purple.
- **Gizmos**: **Q** select, **W** move (arrows along X/Y/Z, the yellow square slides along the
  ground), **E** rotate (rings around X/Y/Z), **R** scale (handles along the object's own axes,
  the white centre cube scales evenly). Move and rotate are world-aligned. Each drag is one undo
  step. Prefab contents get no gizmo (they're read-only).
- **Snapping**: the **Snap** toggle in the viewport toolbar (or hold **Ctrl** while dragging to
  flip it). Moves land on a world grid, rotations on angle steps, scales on scale steps.
- **Preferences** (Edit ▸ Preferences): fly speed, look sensitivity, gizmo size, snap steps.
- **Play / Stop** (Ctrl+P): runs the scene in the engine's runtime (ADR-026/027). Click the
  viewport and your keys are the game's input (W/A/S/D, arrows, Space...; the mapping lives in
  the project's `Settings/input.json`). Until scripts arrive, the built-in behaviours are
  `rotator` (`degrees_per_second`) and `input_mover` (`speed`). Edits are off while playing;
  Stop brings back the scene as you left it.
- **Lua scripts**: Inspector ▸ **Add component ▸ Lua script…** creates `Scripts/<Name>.luau`
  and attaches it; **Open** edits it in your editor; **+ Param** adds values the script reads as
  `self.params`. Press Play to run it (changes are picked up on the next Play). API:
  [docs/scripting/LUA_API.md](../../docs/scripting/LUA_API.md).
- **Create** menu: empty, box, sphere, capsule, floor, physics crate, physics ball, and
  **Player (first person)**: press Play, click into the view, then WASD to walk, the mouse to look,
  Space to jump, Shift to sprint, Esc to get the mouse back.
- **Physics**: Inspector ▸ **Add component ▸ Physics** (rigidbody, box/sphere/capsule/mesh
  collider). Press Play and things fall, bounce and collide (Jolt). A mesh collider on a GLB model
  makes the whole building solid.
- **Live reload**: when a model or prefab in the project changes on disk (BadBuilding regenerates
  a building, say), the viewport reloads it within a second. New files need **Rescan**.
- **Scene** panel: the entity tree, with a filter. Prefab contents are shown dimmed and read-only.
  Double-click frames an entity, **Delete** removes it with its children.
- **Inspector**: name, position, rotation (Euler degrees, yaw about +Y) and scale. Each value you
  commit (Enter or leaving the box) is one undo step. `1,5` and `1.5` both work.
- **Components**: edit fields (numbers, text, true/false, short number lists), add or remove
  fields, add a marker or a custom component, remove components. Model, mesh and prefab
  components are managed by the engine and shown read-only.
- **Content Browser**: scenes and importable assets (GLB/glTF, `.noorprefab`). Double-click a scene
  to open it, double-click a model or prefab to add it to the scene. **Rescan** picks up new files.
- **Console**: engine and editor log.
- **Shortcuts**: Ctrl+S save, Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo, F frame, Delete.
  Unsaved changes are confirmed on close, scene switch and Close Project.
- **Layout**: panel sizes and active tabs are remembered (per user); **Window ▸ Reset Layout**.

Not yet: scripts, local-space move/rotate, multi-select, typed component editors,
remembering panels moved to another dock or floated, multiple viewports per document.

## Run

From the repository root, after building the engine (`cmake --build --preset ninja-debug`):

```bash
dotnet run --project editor/NoorEditor
```

Open a project directly:

```bash
dotnet run --project editor/NoorEditor -- --project build/street
```

## Test

`ctest --preset ninja-debug -L editor` builds the editor against that build's DLL and runs
`NoorEditor --smoke <dir> --test-data tests/data`: it creates a project, adds a BadBuilding
model from the content browser, edits it through the inspector, undoes/redoes, saves and checks
the file, deletes and restores, then clicks and drags in the real viewport (mouse and key messages
posted to its own child window: pick, drill down, a 3 m move along X, a 40° turn, a 2x scale, a snapped
move, flying with W, mouse-look), edits components, saves preferences, live-reloads a model,
edits through a floating Inspector, plays and stops, checks swapchain size against render scaling, checks layout
save/restore, floats the viewport, and requires zero validation and engine errors. Results (`editor_results.json`) and screenshots of the editor's own windows land in
`build/ninja/tests/editor_smoke/<config>/`. It needs a GPU, so CI only builds the editor.
