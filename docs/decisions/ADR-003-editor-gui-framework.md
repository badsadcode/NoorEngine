# ADR-003: Editor GUI framework

**Status:** Proposed (direction chosen, needs a spike before Accepted)
**Date:** 2026-11-15

## Context
NoorEditor must look and feel like Unity / Godot / Unreal, not like an immediate-mode debug
overlay. Tom's earlier ImGui-based experiment confirmed that ImGui is not the right look for
the main editor (it stays fine for in-game debug overlays). NoorCode needs C# language
services, and Roslyn (the C# compiler platform) is a .NET library.

Requirements: docking, multi-window, high-DPI, theming, accessibility, a fast embedded
native Vulkan viewport, permissive license compatible with MIT.

## Options
- **A: Avalonia (C#/.NET, MIT).** Modern styled UI, XAML, good theming, cross-platform;
  Roslyn and AvaloniaEdit run in-process, which makes NoorCode realistic. Native viewport
  via `NativeControlHost` (needs proving). Docking via Dock (MIT) library.
- **B: Qt 6 Widgets/QML (LGPLv3 or commercial).** Very mature for big desktop apps (Maya,
  Substance use Qt). Strong docking. But C# tooling must run out of process, two UI
  languages (C++ + QML), LGPL obligations on redistribution.
- **C: Our own UI on our renderer.** Total control, and runtime game UI needs one anyway,
  but it's years of work before the editor is pleasant (text input, accessibility, IME...).
- **D: ImGui.** Rejected for the main editor on look and UX grounds.

## Decision (proposed)
**Avalonia** for NoorEditor, written in C#. The engine stays C++; the editor talks to it
through a small versioned C API (`noor_editor_api`), which we need anyway for plugins.

## What the spike must prove before "Accepted"
1. A Vulkan viewport embedded in an Avalonia docked panel renders at monitor refresh with
   correct resizing, DPI scaling and input focus.
2. Docking with detachable panels across two monitors.
3. A themed panel set that can match Noor's mockups.
4. Editor startup time and idle CPU are reasonable (measure, then set budgets).

If (1) fails, Qt 6 is the fallback and this ADR is rejected in favour of a new one.

## Consequences
- Editor code is C#; contributors need .NET 10 SDK (already on the dev machine).
- Shipped games never include Avalonia: runtime UI is a separate engine system.

## Evidence / benchmark
None yet. The spike is scheduled after Milestone 0.

## Migration or reversal strategy
Keep editor logic (commands, undo, selection, documents) in UI-independent C# classes so a
UI toolkit swap would not rewrite the editor's brain.

## Reviewed by
Sage. Tom confirmed "Unity/Godot/Unreal quality, not ImGui".
