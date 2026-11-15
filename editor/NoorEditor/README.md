# NoorEditor

The NoorEngine editor: Avalonia + Dock (ADR-003) on top of the engine's C API,
`noor_editor_api.dll` (ADR-025). The engine owns the project, scene, undo history and asset
registry; the editor shows JSON snapshots of them and changes things only through engine
commands, so every edit is undoable and the editor holds no second copy of the scene.

## What works (Milestone 3, parts 1 and 2)

- **Project hub**: recent projects, New Project (creates the folder and an empty `Scenes/Main.noorscene`), Open Project.
- **Viewport**: the engine renders the open scene (GLB models, prefab instances, markers) on its
  own thread. Right-drag orbits, middle-drag pans, the wheel zooms, **F** frames the selection
  (or everything). Float the viewport tab into its own window and it keeps rendering.
- **Selecting in the viewport**: click an object to select all of it (a whole building); click it
  again to go one level deeper, toward the part you clicked. Click empty space to deselect. The
  selection is tinted purple.
- **Gizmos**: **Q** select, **W** move (arrows along X/Y/Z, the yellow square slides along the
  ground), **E** rotate (rings around X/Y/Z). The gizmo is world-aligned and sits on the object's
  origin. Each drag is one undo step. Prefab contents get no gizmo (they're read-only).
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
- **Layout**: panel sizes and active tabs are remembered (per user); **Window ▸ Reset Layout**.

Not yet: snapping, scale gizmo, local-space gizmos, multi-select, component editors, play mode,
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
the file, deletes and restores, then clicks and drags in the real viewport (mouse messages posted
to its own child window: pick, drill down, a 3 m move along X, a 40° turn), checks layout
save/restore, floats the viewport, and requires zero validation and engine errors. Results (`editor_results.json`) and screenshots of the editor's own windows land in
`build/ninja/tests/editor_smoke/<config>/`. It needs a GPU, so CI only builds the editor.
