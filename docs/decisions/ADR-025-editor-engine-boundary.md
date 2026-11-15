# ADR-025: Editor / engine boundary (the document API)

**Status:** Accepted
**Date:** 2026-11-15

## Context
NoorEditor is C# (Avalonia, ADR-003); projects, the asset registry, scenes, prefabs and undo are
C++ (Milestone 2). The editor must show and change scenes without the two sides ever disagreeing,
and without the editor reimplementing engine rules (prefab expansion, migrations, validation).

## Options
- **A: Reimplement the scene model in C#.** Two implementations of every rule; they will drift.
- **B: Engine owns the document; editor is a view that sends commands.** One source of truth.
  Needs a C ABI for documents.
- **C: Engine as a separate process (RPC).** Crash isolation, but latency and much more plumbing
  than we need now.

## Decision
**B, in-process, through `noor_editor_api.dll`:**
- A **document** (`noor_doc`) owns the open project, its asset registry, the open scene, its
  `scene::History` (undo/redo) and its prefab library. All edits go through
  `noor_doc_execute(command JSON)`, so every editor action is undoable by construction.
- The editor reads state as **JSON snapshots** (`noor_doc_scene`, `noor_doc_assets`) and redraws its
  panels from them. JSON keeps the ABI tiny, versionable and debuggable (it's the scene file
  format); snapshots are cheap at editor scales.
- A **revision counter** increments on every change, so views can tell whether to refresh.
- **Viewports render the document**: on each change the document publishes an immutable render
  snapshot (prefabs expanded, world matrices, asset paths) that viewport render threads pick up.
  The UI thread never waits for the GPU.
- Command JSON (v1): `set_transform`, `rename`, `set_component`, `add_asset` (a model or prefab,
  instantiated at a position), `remove`, `reparent`, plus `begin`/`end` transaction markers.
- Strings returned by the API are owned by the DLL and valid until the next call on the same
  thread; errors via `noor_last_error()`.

### Addendum (API v2, Milestone 3 part 2): viewport interaction
Viewports never edit the document either. Picking and gizmo dragging run on the viewport's render
thread (it has the camera, the pointer and the meshes), and the results come back as **events**
(`noor_viewport_poll_events`): `click` (the expanded entity under the pointer, or null) and
`transform` (a finished gizmo drag, as the entity's new local transform). The editor turns a click
into a selection and a transform into one `set_transform` command (with an optional undo `label`).
During a drag the viewport previews the change locally and keeps showing it until the document
publishes the result. The editor tells the viewport what to highlight and where the gizmo goes
(`noor_viewport_set_selection`, `noor_viewport_set_gizmo`); selection stays editor state.

## Consequences
- The C# side stays thin and testable; engine rules have exactly one implementation.
- Selection, camera, layout and preferences are editor state (C#), not document state.
- Process isolation (C) remains possible later: the JSON command/snapshot protocol would carry
  over unchanged.
- The API is still pre-1.0 (`NOOR_EDITOR_API_VERSION` bumps on change); ADR-007 (plugin ABI) will
  decide stability rules.

## Reviewed by
Sage.
