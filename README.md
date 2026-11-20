# NoorEngine

[![CI](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/badsadcode/NoorEngine/actions/workflows/ci.yml)

**Bad Ideas Works** · an independent, open-source (MIT) game engine, editor and toolset.

> We are building our own engine. We have excellent reasons. They remain classified.

**Website:** [badsadcode.github.io/NoorEngine](https://badsadcode.github.io/NoorEngine/), the devlog and
this status table, generated from `docs/` (source in [`website/`](website/)).

## Honest status

**Milestones 0–8 done** ([M1](docs/milestones/M1_REVIEW.md), [M2](docs/milestones/M2_REVIEW.md), [M3](docs/milestones/M3_REVIEW.md), [M4](docs/milestones/M4_REVIEW.md), [M5](docs/milestones/M5_REVIEW.md), [M6](docs/milestones/M6_REVIEW.md), [M7](docs/milestones/M7_REVIEW.md) reviews, each with its accepted exceptions); Milestone 8 ([plan](docs/milestones/M8_PLAN.md)) in four groups, each with its review: **8A, sky and environments, done** ([review](docs/milestones/M8A_REVIEW.md): the atmosphere sky, time of day, fog, aerial perspective, clouds (flat and volumetric), night, space, environment assets and presets, weather zones); **8B, particles and sprites, done** ([review](docs/milestones/M8B_REVIEW.md): particles simulated and drawn, rain and snow, the Effects panel, sprites, 8-direction characters and script control, collisions, sub-emitters and budgets; the sample: a stormy evening on Hillside); **8C, skeletal animation, done** ([review](docs/milestones/M8C_REVIEW.md): skinned meshes, clips, blending and controllers, root motion, retargeting between humanoids, the Animation panel, FBX import; the sample: Alden Wayfarer walking about Hillside); **8D, water, done** ([review](docs/milestones/M8D_REVIEW.md), [ADR-037](docs/decisions/ADR-037-water.md): lakes, seas and rivers, waves, shorelines and reflections, underwater, floating and swimming, the Water panel; the sample: Alden swimming across the Hillside lake in the rain). Next: Milestone 9 (deep procedural world building, [plan](docs/milestones/M9_PLAN.md)), all of it optional: NoorEngine stays a general-purpose engine; 9A part 1 done (orthographic cameras, NoorPong, the yardsticks); 2D and 2.5D foundations under way ([plan](docs/milestones/M9_2D_PLAN.md), parts 1-3 done: 2D physics, a platformer character, your own controllers, sprites for 2D), then gameplay foundations (spawning, UI, saving, navigation), then the rest of 9A ([why this order](docs/milestones/M9_PLAN.md#the-order-from-here-sage-2026-11-20)). What exists today:

| Piece | Status |
|---|---|
| Build (CMake + MSVC), pinned dependencies | Works on Windows 10/11 |
| `foundation`: logging, errors (`Result<T>`), asserts, GUIDs, math | Works, unit-tested |
| `platform`: SDL3 window, events, window control (minimize/restore/resize/move to display), live redraw while resizing | Works; automated lifecycle test |
| `platform`: input (keyboard, mouse, relative mouse mode) | Works; used by the fly camera |
| `render`: Vulkan 1.3 device, swapchain, depth buffer, meshes, lit mesh pipeline, camera, fly camera, GPU timing, frame capture; instanced drawing with camera culling, shadows batched per cascade ([ADR-032](docs/decisions/ADR-032-instancing-and-lod.md)) | Works; smoke, GPU pixel, 2,500-cube stress, window lifecycle and forest tests with validation; debug labels for RenderDoc. Device loss reported, not recovered; high-DPI >100% untested |
| `noor_diag`: diagnostics app (fly camera, `--cubes N` stress grid, timings, `--verify-render`, `--screenshot`) | Works |
| `editor_api`: C API DLL (v24): engine viewports in a foreign window + editable documents ([ADR-025](docs/decisions/ADR-025-editor-engine-boundary.md)) | Works; API tests run in CI. Not a stable plugin ABI yet (ADR-007) |
| **NoorEditor** ([editor/NoorEditor](editor/NoorEditor)): project hub, docked viewport (click to select, fly camera, move/rotate/scale gizmos with snapping, highlight), scene tree, inspector (name/transform/components), content browser with live reload, console, preferences, play/stop, undo/redo, save, remembered layout | **Milestone 3 complete.** Works; 61-check end-to-end smoke test drives real clicks, drags and keys (GPU, local). HiDPI checked by hand at 150%; no multi-select |
| `asset`: projects, `.noormeta` GUIDs, registry (survives renames/moves), GLB import with Noor Interchange data, and FBX import (8C part 7, ufbx); manifests read and checked, levels of detail checked, shown in the Content Browser's Details ([docs](docs/interchange/IMPORTING.md)) | Works, tested with a real BadBuilding block and a NoorForm oak (converted to the export layout until NoorMD writes it). Levels switch by distance per placed model (Milestone 7, part 6: 2,000 oaks at 60 FPS); no cross-fades |
| `scene`: entities + components (layers, tags), prefabs (overrides, nesting, variants), deterministic save/load, format migrations, undo/redo, validation | Works, unit-tested (incl. randomized undo/redo invariant) |
| `noor_cli`: headless project tool (`new`, `scan`, `info`, `add-model`, `make-prefab`, `add-prefab`, `validate`, `migrate`, `build`) | Works; runs in CI |
| `noor_player` + `noor_cli build` + editor **File ▸ Build Game**: a game as a folder with its own .exe, no editor needed ([ADR-028](docs/decisions/ADR-028-player-and-builds.md)) | **Milestone 4, part 6.** Works, with Lua and C# scripts; tested from a project and from builds (GPU, local). Needs the Visual C++ and .NET 10 runtimes on the PC |
| `runtime`: game world, frame order, fixed 60 Hz step, input actions/axes, built-in behaviours; the editor's play mode runs it with keyboard input from the viewport | **Milestone 4, part 1.** Works, unit-tested; physics and audio plug in next |
| `scripting`: **Lua (Luau)** and **C#** scripts with one API: entities, transforms, input, components, physics, character, sound, HUD, logging ([Lua](docs/scripting/LUA_API.md), [C#](docs/scripting/CSHARP_API.md)) | **Milestone 4, parts 2 and 4.** Works; C# compiles on Play and reloads after edits |
| `physics`: **Jolt** rigid bodies, colliders (box, sphere, capsule, mesh from GLBs), raycasts, forces, collision/trigger events; **character controller**; game camera; mouse capture; primitive shapes | **Milestone 4, part 3.** Works; unit-tested; a first-person player walks and jumps in the editor |
| Environments: HDRI or procedural sky, image-based lighting (prefiltered cubemap, SH9, BRDF table), sky background | **Milestone 5, part 5.** Works; `.hdr` only, no reflection probes |
| Atmosphere sky: a physically based sky from the sun (Rayleigh, Mie, ozone), the sun coloured by the air (red at sunset), the sun's disc, haze and altitude ([ADR-033](docs/decisions/ADR-033-environments-and-atmosphere.md), [docs](docs/rendering/PBR.md#the-atmosphere-sky-milestone-8a-adr-033)) | **Milestone 8A, parts 1–7b.** Works, tested against physics; built on the GPU every frame the sun moves (0.16 ms), `time_of_day` runs a day at 60 FPS; height fog and aerial perspective (far hills fade into the sky); two drifting cloud layers with cloud shadows, flat or volumetric (1.5 ms); the moon, stars, space skies with planets and rings, extra suns, automatic exposure; `.noorenv` environment assets with nine presets and an editor gallery; weather zones that blend environments as the camera walks in (60 FPS, scriptable); bodies untextured |
| Particles (NoorFX): `emitter` components and `.noorfx` effects, shapes, bursts, gravity, drag and wind, curves over life ([ADR-035](docs/decisions/ADR-035-particles-and-sprites.md), [docs](docs/fx/PARTICLES.md)) | **Milestone 8B, parts 1-4 and 7.** Simulated and drawn (lit, fogged, sorted with glass), deterministic (Debug = Release = built game); 10,000 particles: 0.02 ms to simulate, 0.58 ms to draw; **rain, snow and dust** from the environment (200,000 drops in 0.18 ms); an **Effects panel** with six presets and a live preview in the editor; **collisions** with the terrain and planes, **sub-emitters** on hits and deaths, and a scene **budget** (50 emitters asking for 150,000 hold at 100,000 in 1.0 ms of CPU) |
| Sprites: `.noorsprite` sheets (grid, pivot, animations) and the `sprite` component ([docs](docs/fx/SPRITES.md)) | **Milestone 8B, parts 5-6.** Animated cutout cards, lit and shadow-casting, upright or camera-facing, crisp pixel art (nearest filtering); a sheet editor with an animated preview; **8-direction characters** (the view from the camera angle) and animations played from Lua and C# with end events, the same in built games |
| 2D physics ([ADR-040](docs/decisions/ADR-040-2d-and-isometric.md), [plan](docs/milestones/M9_2D_PLAN.md)) | **2D foundations, part 1.** Bodies locked to the XY plane (`"plane": "xy"`, exactly, never drifting), any axes locked in 3D too, `rectangle`, `circle` and `polygon` colliders; circles roll like discs (within 0.002% of the formula). **Part 2:** a 2D character controller with platformer feel (acceleration, coyote time, jump buffering, shorter taps), one-way platforms, and a built-in `platformer` component; a held jump peaks within 0.06% of the height asked. **Part 2b:** your own controllers, Godot-style (`"motion": "script"`, `is_on_floor` / `is_on_wall` / `is_on_ceiling`, shape casts and overlaps), and a [guide for Godot users](docs/scripting/COMING_FROM_GODOT.md). **Part 3:** sprites for 2D: drawing order by layer and order, sorting by height on screen (top-down, isometric), soft-edged and unlit sprites, frames that never bleed into each other |
| Orthographic cameras (2D, top-down, isometric) and the yardsticks ([docs](docs/rendering/PBR.md#orthographic-cameras-2d-top-down-isometric), [numbers](docs/milestones/M9_YARDSTICKS.md)) | **Milestone 9A, part 1.** `"projection": "orthographic"` on any camera: shadows, culling, LOD, sprites, the sky and editor picking follow it; **NoorPong**, a 2D sample in Lua, identical in a built game; four yardstick projects (a cube, Pong, NoorFPS-lite, Hillside) whose CPU, GPU, memory, threads and load time are checked against a baseline after every part |
| Skeletal animation: skinned meshes and clips from glTF (any Blender rig), joints as entities, the `animator` component ([ADR-036](docs/decisions/ADR-036-skeletal-animation.md), [docs](docs/animation/ANIMATION.md)) | **Milestone 8C, done.** Skinned in the vertex shader with shadows; clips (step, linear, cubic) played deterministically, from the scene's clock or from Lua and C# with end events; cross-fades; `.nooranim` controllers (parameters, blend spaces, transitions, masked and additive layers); root motion through the character controller (walls stop walks), animation events, props on bones, Create ▸ Player (third person); retargeting between humanoids (Rigify, Mixamo, Unreal mannequin, metarig; T- or A-pose; any proportions); identical in built games; 100 characters on a controller about 1 ms of game update; the editor's **Animation panel** (clips scrubbed on a timeline, the skeleton in the viewport, controllers' states and transitions previewed and edited, bone maps corrected) |
| Water: the `water` component (a lake's box or an endless sea) and `.noorwater` looks ([ADR-037](docs/decisions/ADR-037-water.md), [docs](docs/water/WATER.md)) | **Milestone 8D** ([review](docs/milestones/M8D_REVIEW.md)). Colour by depth (light absorbed per colour by the water's thickness), what's under bent by ripples, the sky and the sun's glint reflected, fog over it, a soft shoreline; the scene copied once a frame when water is in view (0.02 ms; a lake 0.035 ms); the Hillside lake; Create ▸ Water in the editor; **Gerstner waves** raised by the wind, the same formula drawn and read by gameplay (`noor.water_height`, C# `Water.Height`: within 6 mm of the drawn surface, identical in built games), the sea's grid following the camera to the horizon; waves calmed in the shallows by each body's depth map over its terrains (still within 7.4 mm of gameplay's height), foam at the shore and on steep crests, wet sand, screen-space reflections of nearby things; **underwater**: light dimmed by depth with caustics, the water's fog with light shafts, Snell's window and the mirror beyond it, the waterline across the screen, a Deep sea preset (a camera walked through the surface: no pop); **floating** bodies by density (within a micrometre of the textbook depth, identical in built games), wading and swimming characters with `on_water` events, splashes, rain rings; **rivers** along smooth courses, flowing (ripples, white water, floating things carried at the current's speed). the **Water panel** (seven looks with pictures, live editing, debug views of depth, flow, waves, normals and cost). The Hillside sample and the 8D review are next |
| Shadows and lights: cascaded sun shadows (stable, PCF), point and spot lights in lumens | **Milestone 5, part 4.** Works; no shadows from lamps yet |
| Materials: `.noormat` (glTF channels, instances), texture set → material, GLB materials and embedded textures, GPU material table, hot reload | **Milestone 5, part 3.** Works; no see-through blending yet |
| Rendering quality: 4× MSAA, see-through (blend) materials, per-cascade shadow culling, exposure presets, Render stats panel, measurements | **Milestone 5, part 7.** Works ([measurements](docs/rendering/MEASUREMENTS.md)) |
| Texture compression: BC7 (colour, data), BC5 (normals), import settings tab in the editor | **Milestone 5.** Works; 4× less texture memory; no BC6H/BC4, not cooked into built games yet |
| PBR-Showroom sample (sun + 3 HDRIs, roughness sweeps, textured sets) as render tests; NoorFPS-lite textured; [Milestone 5 review](docs/milestones/M5_REVIEW.md) | **Milestone 5 complete** (with documented exceptions: no material graph, bloom, reflection probes) |
| Terrain: heightfield tiles from a seed (deterministic noise) or a 16-bit heightmap, Create ▸ Terrain, drawn and collided in the editor and built games; sculpting (raise, lower, smooth, flatten) on a layer that survives regenerating the base; painting up to four materials per terrain (splat shader), the same Paint tool on CSG faces ([docs](docs/terrain/TERRAIN.md)) | **Milestone 6, parts 5–7.** Works; no LOD yet; painted edges are about a cell wide; strokes are slow in Debug builds on big terrains |
| Procedural graph: `.noorgraph` files, typed nodes with units and versions, validation on the right node, deterministic evaluation, a results cache, worker threads with progress and Cancel, `noor_cli generate` (same bytes as the editor), tiles that recompute alone, terrain nodes, terrains built on graphs (cooked for games), a graph editor (nodes, wires, previews, runs after each change), scatter (forests on terrains as instance sets, trees planted and removed by hand, trunks that collide), masks from paint ([docs](docs/procgraph/PROCGRAPH.md)) | **Milestone 7 complete** ([review](docs/milestones/M7_REVIEW.md)). Works (15 nodes); no zoom, subgraphs or comments in the editor; no cache size limit; grass ends at its draw distance without a fade |
| Sample: **Hillside** (`samples/Hillside`), a house on a sculpted, painted landscape, built with the editor's tools (through its API), and since Milestone 7 a forest of NoorMD oaks, rocks and a grass meadow placed by a graph (off the path, on the painted meadow and rock); golden render, a built game walks into the house; [Milestone 6 review](docs/milestones/M6_REVIEW.md), [Milestone 7 review](docs/milestones/M7_REVIEW.md) | **Milestone 6 complete** (with documented exceptions: the sample was scripted, not built by hand; exports not opened in Blender/Godot by tests; terrain LOD) |
| CSG brushes: box, wedge, cylinder, cone, sphere, stairs, arch; add/subtract/intersect (Manifold); Create ▸ Brush, reorder, see-through cutters; materials and UVs per face (click a face); cooked for games with mesh collision; Noor Interchange GLB export | **Milestone 6, parts 1–4.** Works in the editor and in built games |
| Material editor: live preview (shapes, HDRIs, exposure), every field, instances with overrides, saves as you edit, undo | **Milestone 5, part 6.** Works; no thumbnails or node graph |
| Textures: PNG/JPG/TGA/BMP/HDR import with colour-space kinds, linear-light mipmaps, cache; bindless texture table; base colour and normal maps on primitives | **Milestone 5, part 2.** Works; uncompressed, no GLB textures yet |
| Lighting: HDR frame, physical units (lux, nits, EV100), GGX PBR core, sun `light` component, Khronos PBR Neutral / ACES tonemapping, golden-image render tests | **Milestone 5, part 1.** Works; no materials, textures or shadows yet |
| Sample: **NoorFPS-lite** (`samples/NoorFPS-lite`), a target shooter in C# with a Lua twin, public API only | **Milestone 4, part 8.** Works; built and its demo played by ctest |
| NoorCode MVP: built-in script editor (tabs, Lua/C# highlighting, find, error lines marked and linked from the Console); AvaloniaEdit | **Milestone 4, part 7b.** Works; editor smoke checks. No completion or debugger yet |
| HUD: scripts draw text and rectangles over the game (`noor.ui` / `Ui`); overlay pass, distance-field font (Atkinson Hyperlegible, OFL) | **Milestone 4, part 7a.** Works; pixel-checked in the player tests |
| `audio`: **miniaudio** sound sources, listener, buses (master/music/sfx/ui/voice), 3D attenuation, one-shots; WAV/FLAC/MP3 assets; script API | **Milestone 4, part 5.** Works; tested by measuring the mixed samples (no device needed) |
| Textures, standalone builds, everything else | **Not started.** See the [master spec](docs/NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md) |

Editor images in `docs/NoorEngine_Design_Package` are **concept art**. Screenshots in
`docs/devlog/img` are real captures of the engine and the editor spike.

## Build

Prerequisites: Windows 10/11, Visual Studio 2022 (Desktop C++ workload), CMake ≥ 3.28,
[Vulkan SDK](https://vulkan.lunarg.com/) 1.3+ (sets `VULKAN_SDK`), Git.

```bash
cmake --preset windows
cmake --build --preset debug
ctest --preset debug
```

Run the diagnostics app:

```bash
build/windows/tools/noor_diag/Debug/noor_diag.exe
```

Controls: hold right mouse to look, WASD to move, Q/E down/up, Shift faster, wheel for speed, Esc quits.
Options: `--frames N` (exit after N frames), `--cubes N`, `--no-validation`, `--verify-render`,
`--lifecycle-test`, `--glb <file.glb>` or `--project <dir> --scene <Scenes/x.noorscene>` (both hot-reload), `--screenshot <file.bmp>`, `--log <file>`.

Run a project without the editor, or build it into a folder you can share:

```bash
build/ninja/tools/noor_player/Debug/noor_player.exe tests/data/player_project
```

```bash
build/ninja/tools/noor_cli/RelWithDebInfo/noor_cli.exe build tests/data/player_project build/player_test_game
```

Tests labelled `gpu` need a Vulkan 1.3 GPU; skip them with `ctest --preset debug -LE gpu`.
CI (GitHub Actions) builds Debug and Release on every push and pull request and runs everything
except the `gpu` tests, because GitHub's runners have no GPU. Run those locally before pushing
renderer changes. CI uses the `windows-ninja` preset (Ninja + whatever MSVC the runner has).

## Repository map

| Path | What |
|---|---|
| `engine/foundation` | Logging, errors, asserts, IDs. Depends on nothing. |
| `engine/platform` | OS layer (windows, events) on SDL3. |
| `engine/render` | Vulkan backend. |
| `engine/editor_api` | C ABI DLL the C# editor uses: viewports and documents. |
| `editor/NoorEditor` | The editor (C#, Avalonia). |
| `engine/asset`, `engine/scene` | Projects, asset registry, GLB and FBX import; scene documents. |
| `engine/runtime` | The running game: world, systems, input, the scripting API. |
| `engine/physics` | Jolt physics as a runtime system. |
| `engine/scripting` | Script languages as runtime systems: Lua (Luau), and C# (`dotnet/NoorEngine.Scripting`). |
| `tools/noor_diag` | Diagnostics app; permanent developer utility. |
| `tools/noor_cli` | Headless project tool; `build` packs a game. |
| `tools/noor_player` | Runs a game outside the editor (what a built game's .exe is). |
| `engine/game` | What every running game needs (world setup, what to draw), shared by Play and the player. |
| `spikes/` | Technology experiments that decide ADRs. Not product code. |
| `tests/` | Unit and smoke tests (doctest + CTest). |
| `docs/decisions` | Architecture Decision Records. Read these before arguing. 😉 |
| `docs/devlog` | The public devlog. Funny, never fake. |
| `IDEAS.md` | The idea parking lot. Throw ideas here. |

## License

Code: [MIT](LICENSE). Third-party components: see [`third_party/manifest.json`](third_party/manifest.json).
Concept art and logos in `docs/` are not covered by the MIT license until decided otherwise.

*Code shall remain understandable to whichever AI inherits this mess.*
