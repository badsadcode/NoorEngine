# Avalonia viewport spike (ADR-003)

**Spike code.** Its job was to prove or disprove Avalonia + Dock as NoorEditor's GUI stack.
Verdict and measurements: [ADR-003](../../docs/decisions/ADR-003-editor-gui-framework.md).
Ideas that survive move to `editor/`; this folder is not the editor.

> **Superseded** by [`editor/NoorEditor`](../../editor/NoorEditor) in Milestone 3. Kept as the
> record of the ADR-003 measurements; it still builds and runs against API v9.

What it shows: a themed editor shell (menu, workspace tabs, docked Scene / Inspector /
Console / Content panels, status bar) with a **real NoorEngine Vulkan viewport** in the
center, rendered by the engine on its own thread through `noor_editor_api.dll`.

- **Inspector** and **status bar**: live data from the engine (GPU, FPS, frames, validation errors).
- **Console**: the engine's real log.
- **Scene tree** and **Content Browser**: labeled sample data (no scene/asset systems yet).
- **Viewport input demo**: hover to shift the hue with the pointer, hold the left button to brighten.

## Run

From the repository root:

```bash
cmake --build --preset debug
```

```bash
dotnet run --project spikes/avalonia_viewport
```

Automated checks (startup, FPS, CPU, resize, floating, zero errors), writing
`spike_results.json` and screenshots of the app's own windows to `build/spike_results`:

```bash
spikes/avalonia_viewport/bin/Debug/net10.0/NoorEditorSpike.exe --smoke 5 --out build/spike_results
```

Exit code 0 means every check passed.
