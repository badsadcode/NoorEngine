# NoorSky | NoorEngine environment authoring package

> **In the repository since 2026-11-18** (Milestone 8 plan, group 8A). The brief is as received; the 12
> mood references, the overview poster and the contact sheet are concept art (not skyboxes or engine
> screenshots) and stay outside the repository (`NoorSky_Sage_Package.zip`), so their names below are
> plain text. How NoorEngine builds it: [M8_PLAN.md](../milestones/M8_PLAN.md).

**For:** Sage / NoorEngine  
**From:** Tomek and Noor, Bad Ideas Works  
**Date:** 18 November 2026  
**Status:** Design / feature brief, **not** a claim that these features are implemented

## Vision

Make a new NoorEngine scene look beautiful **straight out of the box**, without imposing any artistic ceiling on creators. Include carefully tuned, realistic-looking environment presets, plus a complete toolset for **building brand-new skies and environments from scratch**.

**Product rule:** *Beautiful defaults for beginners. Full creative freedom for everyone else.*

A preset must be more than a sky texture: it should establish a cohesive mood across sky, atmosphere, distant background, light, fog and exposure. A creator can then modify every part, build something completely different, and save the result as a reusable asset.

## Visual reference library

The included PNGs are **visual mood and lighting references**, not import-ready skyboxes, HDRIs, cube maps, texture sources, or production game art. They illustrate the colour, contrast, horizon character, cloud forms and mood Sage should target with real rendering features.

| ID | Preset | Reference | Look and behaviour |
|---|---|---|---|
| 01 | **Clear Noon** | `01_Clear_Noon.png` | Natural blue sky, white cumulus, clean sunlight, high visibility, subtle aerial perspective. |
| 02 | **Golden Evening** | `02_Golden_Evening.png` | Warm low sun, softly glowing cloud edges, long shadows, amber horizon and gentle haze. |
| 03 | **Overcast** | `03_Overcast.png` | Full grey-blue cloud deck, broad diffuse illumination, suppressed hard shadows, cool mood. |
| 04 | **Foggy Dawn** | `04_Foggy_Dawn.png` | Pale pink sunrise, layered ground mist, distant silhouettes, low visibility, subdued contrast. |
| 05 | **Stormy Coast** | `05_Stormy_Coast.png` | Dark heavy weather, moving cloud layers, moody coastline, intermittent sunlight and optional lightning. |
| 06 | **Moonlit Night** | `06_Moonlit_Night.png` | Dark blue night, visible moon and stars, subtle cool ambient lighting, gameplay-readable exposure. |
| 07 | **Fantasy Crisp** | `07_Fantasy_Crisp.png` | A heightened but grounded mountain sky, clear saturated air, luminous clouds, elegant silhouettes. |
| 08 | **Cold Morning** | `08_Cold_Morning.png` | Pale winter daylight, faint warmth near horizon, crisp air, soft cold illumination. |
| 09 | **Autumn Afternoon** | `09_Autumn_Afternoon.png` | Rich warm afternoon light, clear distance, seasonal colour and longish shadows. |
| 10 | **Rainy Grey** | `10_Rainy_Grey.png` | Layered low rain clouds, cool flat light, moisture haze, rainy scene character. |
| 11 | **Desert Haze** | `11_Desert_Haze.png` | Dusty air, bleached sunlight, sandy horizon, high-distance atmospheric extinction. |
| 12 | **Alien World** | `12_Alien_World.png` | Unusual palettes, giant celestial bodies, exotic haze and an unmistakably non-Earth sky. |

The overview poster provides a one-page visual target and suggested UX. The four-mood contact sheet has larger examples of the first four realistic presets. The overview includes mock UI and is a design concept, not an engine screenshot.

## User experience

### Beginner: one-click polish

1. Create a new scene or select the scene's Environment.
2. Browse a **thumbnail gallery** of built-in sky/environment presets.
3. Apply a preset once to update sky, lighting, fog and atmospheric mood as a coherent whole.
4. Optionally adjust simple controls for time of day, cloud coverage, fog, sunlight and overall exposure.
5. Save the scene; it retains the environment when reopened and when built as a game.

**Target:** From an empty outdoor scene to a convincing and attractive atmosphere in a few clicks.

### Advanced: genuine authoring freedom

- Create new environments **without** beginning with a built-in preset.
- Duplicate and modify any built-in preset without changing the original.
- Tune and animate sky, atmospheric scattering/gradients, sun/moon, stars, clouds, fog, lighting and backgrounds independently.
- Import HDRI/equirectangular panorama/cubemap resources where practical, alongside procedural skies.
- Allow fully custom palettes, multiple celestial objects, nonstandard skies and stylized worlds, without forcing Earth-like rules.
- Save, import, export and reuse environment assets between NoorEngine projects.
- Later, connect environment parameters to graph outputs and Lua scripting, without making a graph editor necessary for ordinary use.

## Suggested system structure

Keep the system modular. The following are responsibilities rather than mandated source files or class names:

1. **Environment asset** (working extension: `.noorenv`): complete, versioned, serializable preset with references to resources.
2. **Sky / atmosphere renderer:** procedural atmospheric colour, horizon, scattering or a performant approximation, optional HDRI/panorama mode.
3. **Celestial system:** sun and moon(s), starfield, optional custom sky objects, physically coherent light direction in realistic presets.
4. **Cloud subsystem:** layers, coverage, altitude, density, motion, lighting; simple performant fallback first, advanced volumetric options later.
5. **Fog / aerial perspective:** distance, altitude, colour, extinction; coherent with scene lighting and sky horizon.
6. **Scene lighting link:** directional light, ambient / environment illumination, reflections if supported, exposure and colour grading.
7. **Horizon/background system:** distant mountains/forest/sea silhouettes, imported or generated panoramas; separate from the sky so creators can swap one without losing the other.
8. **Time / weather controller:** optional, drives changes or blends between authored environments, not hardwired into every preset.
9. **Editor authoring surface:** preset thumbnails, category filters, inspector controls, interactive viewport preview, save/duplicate/reset.

**Important:** A completely custom sky shader or imported background should be possible without breaking independent clouds, fog or illumination. Avoid a tightly coupled all-or-nothing monolith.

## Authoring controls to expose

| Category | Example controls |
|---|---|
| Sky / air | procedural or HDRI source, zenith/horizon colours, turbidity, atmospheric density, scattering and sun glow |
| Sun & moon | direction, colour, intensity, angular size; one or more optional bodies; enable/disable |
| Clouds | layers, shape, coverage, thickness, altitude, velocity, lighting response, shadow influence |
| Fog | density, vertical falloff, start/end visibility, tint, distance haze and optional local volumes |
| Horizon | gradient, distant silhouettes, panorama rotation, blending, sea horizon |
| Stars / space | night visibility, star density/brightness, celestial textures and optional exotic objects |
| Lighting | sun/moon/environment illumination, exposure, white balance, colour grading and optional bloom |
| Timing | static or live time, animation curves, weather states and blending durations |
| Asset | create, duplicate, edit, version, save, import/export, reset, preview |

Only expose controls that are actually implemented; avoid a decorative inspector full of dead knobs. Provide sensible presets and tooltips for complex parameters.

## Implementation progression

### Phase A: beautiful, solid defaults

- Scene environment asset, saving/loading and built-game serialization.
- Procedural sky plus one practical skybox/HDRI import path if compatible with the renderer.
- Sun, moon, stars and coherent light/fog/exposure settings.
- Lightweight layered clouds with simple motion.
- Six polished presets initially: Clear Noon, Golden Evening, Overcast, Foggy Dawn, Moonlit Night, Stormy Coast.
- Simple editor UI: presets, thumbnails, apply, duplicate, edit, save, reset.
- No hard requirement for volumetric clouds, seasonal weather or an environment node graph in this phase.

### Phase B: richer atmosphere and reusable environments

- Distant-horizon layers (mountains, trees, coast/sea or panorama).
- Better clouds and lighting, improved atmospheric depth, useful fog detail.
- Time-of-day blending, cloud transitions and optional weather mood changes.
- Expand built-in library to Cold Morning, Autumn Afternoon, Rainy Grey, Desert Haze, Fantasy Crisp.
- Authoring/export workflow for creator-made `.noorenv` assets.

### Phase C: unrestricted worlds

- Advanced clouds / volumetrics where performance allows, storm effects and cloud shadows.
- Dynamic weather, seasonal environment setups, local fog/weather volumes.
- Multi-sun/moon and non-Earth sky modes, custom celestial artwork and shader hooks.
- Optional procedural graph and Lua drivers for sky/cloud/weather parameters.
- Alien World showcase preset.

Do not let later features postpone the core goal of attractive presets and working custom authoring.

## Practical integration notes for NoorEngine

- Respect the current rendering architecture. Reuse existing shader, material, scene, build/cooking and serialization capabilities rather than inventing a parallel pipeline without reason.
- Support Editor **and** standalone Player. Environment resources must cook into the game and look materially consistent there.
- Keep settings deterministic where appropriate. Seeds and controlled time/weather should reproduce the same initial state.
- Make content robust across a low-quality setting and a high-quality setting. Clouds and fog should degrade gracefully without disappearing unexpectedly.
- Do not make weather or atmosphere change gameplay lighting unpredictably unless opted into an explicit dynamic system.
- Keep horizon/fog/sky colours mutually compatible. The far-ground transition is crucial to avoiding a visible seam.
- Default showcase scene should show how each preset behaves with a simple hill, trees or geometry, not just a bare sphere.
- Night preset should retain readable silhouettes without fake noon lighting. Realistic and stylized modes can use different exposure assumptions.
- For the eventual NoorMD / NoorForm asset bridge, sky lighting and foliage wind may share weather inputs, but the systems should stay independently usable.

## Quality & performance gates

- [ ] Applying a built-in preset changes the live viewport coherently, without requiring manual tweaking of unrelated systems.
- [ ] User can create a custom environment from scratch and save/reload it.
- [ ] Duplicating a built-in preset never mutates the installed preset.
- [ ] Both editor and built game show a consistent result.
- [ ] Scene save/reopen preserves asset links and all authored parameters.
- [ ] Sky/horizon/fog blend plausibly, including across changing time-of-day.
- [ ] Presets remain visually useful with low graphics settings.
- [ ] Clouds, fog and sky do not dominate GPU time; measure using same scene/settings before and after, rather than claiming unsupported comparisons.
- [ ] No unexplained colour shift or large exposure jump when changing presets.
- [ ] Weather/time animation does not unnecessarily rebuild static resources each frame.
- [ ] Procedural randomness, when enabled, can be replayed from a seed.
- [ ] Runtime failures show clear errors and safe fallbacks instead of crashing the scene.

## Scope and decision freedom

This is the desired **experience and capability**, not an instruction to implement all phases immediately or to rewrite working systems. Sage may choose rendering techniques, internal formats, material/shader integration, performance tiers and development order that fit NoorEngine. Prioritize testable small vertical slices, no regressions, and visible results in the editor.

**The end state:** Open an empty scene, choose **Golden Evening**, and it already looks beautiful. Then create a truly original alien sunrise, save it, and use it in another game. All without fighting the tools.

---
*Bad Ideas Works · Questionable decisions, excellent sunsets.*
