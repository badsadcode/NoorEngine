# NoorEditor

The NoorEngine editor: Avalonia + Dock (ADR-003) on top of the engine's C API,
`noor_editor_api.dll` (ADR-025). The engine owns the project, scene, undo history and asset
registry; the editor shows JSON snapshots of them and changes things only through engine
commands, so every edit is undoable and the editor holds no second copy of the scene.

## What works (Milestone 3, part 1)

- **Project hub**: recent projects, New Project (creates the folder and an empty `Scenes/Main.noorscene`), Open Project.
- **Viewport**: the engine renders the open scene (GLB models, prefab instances, markers) on its
  own thread. Right-drag orbits, middle-drag pans, the wheel zooms, **F** frames the selection
  (or everything). Float the viewport tab into its own window and it keeps rendering.
- **Scene** panel: the entity tree, with a filter. Prefab contents are shown dimmed and read-only.
  Double-click frames an entity, **Delete** removes it with its children.
- **Inspector**: name, position, rotation (Euler degrees, yaw about +Y) and scale. Each value you
  commit (Enter or leaving the box) is one undo step. `1,5` and `1.5` both work. Components are
  read-only for now.
- **Content Browser**: scenes and importable assets (GLB/glTF, `.noorprefab`). Double-click a scene
  to open it, double-click a model or prefab to add it to the scene. **Rescan** picks up new files.
- **Console**: engine and editor log.
- **Shortcuts**: Ctrl+S save, Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo, F frame, Delete.
  Unsaved changes are confirmed on close, scene switch and Close Project.

Not yet: gizmos, picking in the viewport, selection highlight, component editors, play mode,
layout persistence (only the window size is remembered), multiple viewports per document.

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
the file, deletes and restores, floats the viewport, and requires zero validation and engine
errors. Results (`editor_results.json`) and screenshots of the editor's own windows land in
`build/ninja/tests/editor_smoke/<config>/`. It needs a GPU, so CI only builds the editor.
