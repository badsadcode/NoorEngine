# ADR-003: Editor GUI framework

**Status:** Accepted (2026-11-15, after spike), with open verification items listed below
**Date:** 2026-11-15

## Context
NoorEditor must look and feel like Unity / Godot / Unreal, not like an immediate-mode debug
overlay. Tom's earlier ImGui-based experiment confirmed that ImGui is not the right look for
the main editor (it stays fine for in-game debug overlays). NoorCode needs C# language
services, and Roslyn (the C# compiler platform) is a .NET library.

Requirements: docking, multi-window, high-DPI, theming, accessibility, a fast embedded
native Vulkan viewport, permissive license compatible with MIT.

## Options
- **A: Avalonia (C#/.NET, MIT).** Modern styled UI, good theming, cross-platform; Roslyn and
  AvaloniaEdit run in-process, which makes NoorCode realistic. Native viewport via
  `NativeControlHost`. Docking via the Dock library (MIT).
- **B: Qt 6 Widgets/QML (LGPLv3 or commercial).** Very mature for big desktop apps. Strong
  docking. But C# tooling must run out of process, two UI languages (C++ + QML), LGPL
  obligations on redistribution.
- **C: Our own UI on our renderer.** Total control, but years of work before the editor is
  pleasant (text input, accessibility, IME...).
- **D: ImGui.** Rejected for the main editor on look and UX grounds.

## Decision
**Avalonia** for NoorEditor, written in C#, with the **Dock** library for docking. The engine
stays C++; the editor talks to it through a C API DLL (`noor_editor_api`, v0 today; the
stable versioned ABI is ADR-007).

## Evidence (spike, `spikes/avalonia_viewport/`)
Automated run (`NoorEditorSpike --smoke 5`), Debug build, Vulkan validation on, Windows 10,
RTX 3080, 60 Hz monitor, 100% display scale. Avalonia 12.1.4, Dock 12.1.0.13.

| Check | Result |
|---|---|
| Vulkan viewport embedded in a docked Avalonia panel | ✅ renders, 60 FPS (vsync) |
| Viewport creation → first frame | 236 ms (includes Vulkan instance + device creation) |
| Process start → main window shown → first viewport frame | 931 ms → 1,606 ms |
| Window resize reaches the swapchain | ✅ 1001×514 → 749×413 |
| Float viewport panel into its own window | ✅ re-created and rendering 363 ms after the float |
| Vulkan validation errors / engine errors | 0 / 0 |
| CPU while rendering at 60 FPS | 0.74% of a 32-thread machine (≈24% of one core), Debug + validation |
| Working set | 247 MB (Debug, validation layers loaded) |
| Theme matches the mockup direction | ✅ dark/purple shell, docked panels (screenshot in devlog #003) |

Architecture proven: the engine owns a native child window and a **dedicated render thread**,
so rendering is independent of the UI thread. Floating a panel re-parents the native control;
Avalonia destroys and re-creates it, and the engine viewport follows.

## Open verification items (not yet tested)
1. **High-DPI** at 125%/150%/200% and moving between monitors with different scales.
2. **Drag-and-drop docking by mouse** with the native viewport visible ("airspace": a native
   child window always draws above Avalonia content, so dock drop indicators could be hidden
   behind the viewport). The automated test floated the panel programmatically.
3. **Live window drag-resize** smoothness and **keyboard focus** moving in/out of the viewport.
4. **Hover/click input** reaching the viewport (demo exists: hue follows pointer, click brightens;
   needs a human).
5. Release-build CPU/memory numbers.

If (2) turns out unfixable, mitigation is to hide the native viewport during dock drags and
show a snapshot, which is a common technique, before considering Qt.

## Consequences
- Editor code is C#; contributors need the .NET 10 SDK.
- Shipped games never include Avalonia: runtime UI is a separate engine system.
- Each viewport currently creates its own Vulkan instance and device, which is why floating
  costs ~360 ms. The real editor needs **one shared device** with per-viewport swapchains.
- The floating window's tab used Windows' blue accent instead of the theme purple: theme
  resources must be applied to Dock host windows too.
- Dock 12.1.0.13 quirk: fluent `AppendTool`/`AppendDocument` left docks empty; the spike adds
  dockables through `factory.AddDockable` instead.

## Migration or reversal strategy
Keep editor logic (commands, undo, selection, documents) in UI-independent C# classes so a
UI toolkit swap would not rewrite the editor's brain. The engine side (`noor_editor_api`,
native child window, render thread) is toolkit-agnostic and would serve Qt equally well.

## Reviewed by
Sage. Tom confirmed "Unity/Godot/Unreal quality, not ImGui".
