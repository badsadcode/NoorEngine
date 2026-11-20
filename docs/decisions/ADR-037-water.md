# ADR-037: Water

**Status:** Accepted (Milestone 8D, 2026-11-20, with its [plan](../milestones/M8_PLAN.md#8d-water); Tom settled the open questions)

## Context
NoorEngine has no water. The spec's NoorWater (17.1) asks for distinct bodies (ocean, lake, river, pool, underwater
volumes), shorelines, waves for open water, shallows and rivers, reflection, refraction, underwater fog and light
falloff, foam, ripples, wind and weather, a query API for boats, swimmers and physics, and debug views; it accepts an
art-directed approximation over a physically exact fluid, if consistent and disclosed. Hillside wants a lake, Thalassa
(Tom's deep-sea game) the deep sea, Thornwake rivers. The renderer draws see-through things after the opaque scene but
reads nothing behind them; everything else in the engine is deterministic and checked in built games, and water
gameplay (floating, swimming) must be too.

## Options
1. **A simulated fluid** (heightfield shallow-water or particles): water that flows into holes and piles up. Rich,
   but costly, hard to keep deterministic, and far beyond what lakes, seas and rivers in a game need.
2. **A sea and a lake as plain transparent planes** with a sky reflection: cheap, but no depth (no shore, no colour
   by depth, nothing seen bent underneath), no underwater, waves only in a texture.
3. **Surfaces with a formula and the scene behind them:** water bodies as components, waves as a sum of Gerstner
   waves (a pure function of position and time), the opaque scene's colour and depth copied once a frame so the
   surface knows how deep it is at every pixel, the environment for underwater, buoyancy worked out on the CPU.

## Decision
Option 3.
- **The `water` component** with a shape: a box (lakes, ponds, pools: a level surface over a footprint, the volume
  under it), an endless sea (to the horizon, following the camera), a river (a spline, part 6). Its look in a
  `.noorwater` asset (presets, overrides on the component), like `.noorenv` and `.noorfx`.
- **One colour-and-depth copy per frame** after the opaque pass, only when water is in view: absorption by thickness
  (per channel), refraction by an offset lookup, a soft edge and foam where it's shallow.
- **Reflections:** the sky and sun always (8A's environment), nearby things by screen-space reflection, falling back
  to the sky. Planar reflections not in 8D.
- **Waves:** Gerstner waves from the wind and the preset plus detail normals; the same formula in the shader and on
  the CPU (`water_height(x, z)`); gameplay uses only the CPU's answer.
- **Underwater in the environment:** an `underwater` block used inside water volumes, so weather zones and presets
  work as for everything else; the deep sea is a volume whose surface is out of sight.
- **Buoyancy and swimming:** computed by NoorEngine from sample points against the water's height at the fixed step,
  applied through Jolt; characters wade and swim.

## Consequences
- The frame gains a resolve-and-copy when water is in view (nothing when it isn't). Measured in part 1 (2026-11-20,
  1280×720, 4× MSAA): the copy 0.02 ms, a lake 0.035 ms, the split pass about 0.03 ms more. It also gives later effects
  (soft particles, decals) the scene's depth.
- Waves are art-directed, deterministic and cheap; they don't flow round obstacles or pile up. Disclosed in the docs.
- Screen-space reflections miss what's off screen; the sky stands in for it. Measured in part 3 (2026-11-20, 1280×720):
  about 0.14 ms on a wavy sea filling most of the screen, the dominant part of water's cost.
- Underwater (part 4): the light under a body is dimmed in the mesh shader for the first eight bodies; from inside,
  two full-screen passes (about 0.3 ms at 1280×720, measured 2026-11-20). The `underwater` block is flat `underwater_*`
  keys on the environment, so weather zones blend it like fog.
- Water bodies are data (component and asset), so procedural graphs can place them later (lakes in hollows, rivers
  from drainage).

## Settled with Tom (2026-11-20)
Reflections: the sky plus screen-space (no planar reflections in 8D). The calm Hillside lake is tuned first. Rivers: a
spline surface in 8D, carving later with NoorRoads. Alden gets a generated swim clip.
