# Milestone 3 gate review: NoorEditor base application

**Date:** 2026-11-15 · **Reviewer:** Sage · **Verdict:** ✅ **Complete, with documented exceptions**

Spec reference: master spec section 29, Milestone 3 ("Goal: a professional editor that can
author, save, reopen and play simple scenes").

## Deliverables

| Deliverable (spec) | Status | Evidence |
|---|---|---|
| Selected UI toolkit | ✅ | Avalonia 12 + Dock (ADR-003, measured spike); NoorEditor in `editor/NoorEditor` |
| Dockable layout | ✅ | Dock panels (Scene, Inspector, Content Browser, Console, Viewport); float, re-dock; sizes and tabs remembered; Window ▸ Reset Layout |
| 3D viewport | ✅ | Engine render thread in a native child window; fly camera (WASD/QE), orbit, pan, zoom, framing; selection tint |
| Hierarchy | ✅ | Tree with filter; prefab contents read-only; double-click frames; Delete removes subtrees |
| Inspector | ✅ | Name, position, Euler rotation, scale; component fields (numbers, text, booleans, number lists), add/remove fields and components |
| Content browser | ✅ | Scenes and assets from the registry; double-click opens or places; rescan; **live reload** of models and prefabs changed on disk |
| Gizmos | ✅ | Move (axes + XZ plane), rotate (rings), scale (entity axes + uniform); snapping (grid, angle, scale steps; Ctrl inverts); live preview; one undo step per drag |
| Play / stop | ✅ | ADR-026: runtime copy, built-in `rotator` behaviour, edits refused while playing, Stop restores. Scripts are Milestone 4 |
| Commands / shortcuts | ✅ | Every edit is an engine command (ADR-025) with labelled undo; Ctrl+S/Z/Y/P, Q/W/E/R, F, Delete; keys work with the viewport focused |
| Preferences | ✅ | Edit ▸ Preferences: fly speed, look sensitivity, gizmo size, snap steps (saved per user) |
| Project hub | ✅ | Recent projects, New Project (creates folder + empty scene), Open Project |
| Theme foundation | ✅ | NoorTheme tokens over Fluent + Dock resources |

## Gate criteria

| Gate (spec) | Result | Evidence |
|---|---|---|
| A new project can be created and a scene edited without manually changing config files | ✅ | Hub ▸ New Project; the smoke test creates a project, places a model, edits, saves and reads the file back |
| Layout persists | ✅ (panel sizes, active tabs) | Smoke test: size saved, restored on rebuild, reset to default. Panels moved to another dock or floated are not remembered yet (exception 2) |
| HiDPI behaviour is checked | ✅ (by hand at 150 %; automated at 100 %) | The pixel maths is consistent by construction (child window, swapchain and pointer all in physical pixels; Avalonia's render scaling converts the layout) and the smoke test asserts swapchain size = control size × render scaling. **Hand check:** NoorEditor was open while Windows scaling was switched to 150 % and back; the editor followed the change live and looked right. The automated test has not yet run *at* 150 % (exception 1) |
| Multiple-window and undo cases are tested | ✅ | Floating viewport keeps rendering the document; floating Inspector edits and undoes; undo tests: after save, redo branch dropped by a new edit, transactions, history reset on scene switch, undo refused while playing |

## Exceptions (accepted, tracked)

1. **HiDPI, automated**: the smoke test (which also clicks and drags) has only run at 100 %; the
   150 % check was by eye. Run `ctest -L editor` once with the display at 150 % to close this.
   Also learned: changing the scaling *while* the smoke test runs makes its drag checks fail
   (its screen positions go stale), which is a test limitation, not an editor bug: reruns at a
   steady scale passed.
2. **Layout**: panels moved to a different dock, or floated, return home on the next start.
3. **One viewport per document per window**, each with its own Vulkan device (ADR-003
   consequence). A shared device for several viewports is still open.
4. **Play mode runs built-in behaviours only** (`rotator`); scripting is Milestone 4. Time is
   driven by a UI timer, not a runtime loop (ADR-026).
5. **New asset files need Rescan** to appear; files already in the registry are watched and
   reloaded automatically.
6. **Carried:** textures (Milestone 5), LOD loading, device-loss recovery, adding children inside
   prefab instances (see the M2 review).

## Measurements
- Editor smoke test (`ctest -L editor`): 61 checks in ~25 s, including real (posted) mouse and key
  input to the viewport: 3 m move drag → 3.019 m, 40° rotate drag → 39.6°, 2× scale drag → 2.01,
  snapped move → exactly x = 15, fly 0.6 s at 8 m/s → 4.8 m, rotator 30°/s for ~1 s → 29°.
  Zero Vulkan validation errors, zero engine errors.
- Startup: process start → first viewport frame ≈ 1.7 s (Debug, validation on, RTX 3080).
- Tests: 108/108 (Debug, incl. GPU and editor), 102/102 non-GPU (Release); CI builds NoorEditor.

## Carried into Milestone 4 (core scripts and gameplay)
- Play mode's tick moves into the runtime loop; scripts become behaviours (ADR-026).
- Editor API is at v4 and still pre-1.0; ADR-007 (plugin ABI) decides stability.
