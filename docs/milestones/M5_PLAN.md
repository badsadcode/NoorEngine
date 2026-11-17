# Milestone 5 plan: PBR lighting, HDRI and materials

**Goal (spec):** make the engine visually credible and artist-friendly.

**Gate (spec):** a PBR sample displays predictable materials under different HDRIs; the importer
preserves colour spaces; changes serialize; render tests detect regressions.

**Where we start:** the renderer draws vertex colours with one fixed light straight into the sRGB
screen. No textures, materials, shadows, HDR or environment. Everything visual in this milestone
is new, so the first part is the foundation and the test harness, not pretty pictures.

## Decisions
ADR-012 (PBR model, forward path, photometric units, HDR + AgX, IBL, bindless), ADR-029
(textures), ADR-030 (materials). All **Proposed** until the part that implements them lands.

## Parts (each one ends tested, documented and pushed)

| Part | What | Proves |
|---|---|---|
| 1 | **Frame foundation**: HDR float target, depth pre-pass, named passes with GPU timings, exposure (EV100) and tonemapping (AgX / PBR Neutral), the HUD composited after; vertices gain UVs and tangents; a `light` component (directional sun) replacing the fixed light; **golden-image render tests** | Same scenes, now lit by a real sun through an HDR chain, and pictures that tests compare |
| 2 | **Textures** (ADR-029): PNG/JPG/TGA (8/16-bit), `.hdr`/`.exr` import, colour-space kinds, normal convention, linear-light mips, GLB embedded textures as sub-assets, derived-data cache, previews and memory in the editor; bindless texture table | A normal map read as sRGB is caught by a test |
| 3 | **Materials** (ADR-030): `.noormat` + instances, the standard GGX shader, GLB materials with their textures, **texture set → material** (NoorMaterial manifest or names), `material` on primitives, Inspector editing with undo, hot reload | A NoorMaterial brick set on a wall looks like it did in NoorMaterial |
| 4 | **Lights and shadows**: directional cascaded shadow maps (PCF), point and spot lights (small fixed count first), light gizmos and Create ▸ Light, shadow settings | Shadows that sit on the ground and don't swim when the camera moves |
| 5 | **HDRI environments and IBL**: environment asset (rotation, intensity, background on/off), cubemap conversion, SH9 diffuse, prefiltered specular, BRDF LUT, cached; a simple procedural sky as the default | The same metal sphere looks right under three HDRIs |
| 6 | **Material editor MVP**: a material panel with a live preview (sphere, cube, plane) under selectable HDRIs and exposure presets, parameter editing, save as instance; the graph is designed (data contract in ADR-030), not built | The spec's acceptance story, minus painting |
| 7 | **Quality and measurement**: MSAA, exposure presets (sunny, overcast, indoor), passes inspector (timings, draw counts, texture memory), first block compression (BC7/BC5) if the memory numbers call for it, shared GPU device for editor viewports if measured necessary | Numbers on the sample scene, not guesses |
| 8 | **PBR sample + review**: material ball grid (metal/roughness sweeps), textured walls from a NoorMaterial set, three HDRIs, render regression tests on it; NoorFPS-lite gets textured walls; Milestone 5 review | The gate |

## Highest risks (settle early)
1. **Bindless on every GPU we care about** (descriptor indexing): check capabilities in part 1 and
   keep a clear error if missing, not a crash.
2. **Render tests that are stable**: GPU and driver differences make exact pixel matches fragile;
   tests compare small images with a tolerance and a per-test threshold, and store references in
   the repo. Part 1 proves this before anything depends on it.
3. **Photometric units feeling wrong** to a game maker (black or blown-out scenes): exposure
   presets and good defaults from part 1.
4. **Texture memory** without compression, and **one Vulkan device per editor viewport** (ADR-003)
   doubling uploads: measured in part 7, fixed there if the numbers say so.
5. **Scope**: the spec's material workspace (graphs, layering, painting, decals) is far bigger than
   one milestone. Milestone 5 builds the contract and an MVP; graphs and painting are planned
   for later (painting with CSG in Milestone 6).

## Assets we'll need
- **HDRIs**: three CC0 environments (Poly Haven is CC0) for tests and the sample, small sizes
  (1k–2k). Sage asks before downloading.
- **Texture sets**: made with NoorMaterial (ours), or CC0 sets. A tiny hand-made set for tests.

## Milestone tradition
Alden discovers something at the end of Milestone 5. Proposal: *"Alden discovers colour: the
stones are rough, the buckle shines, and the sky lights the world."* (Tom and Noor make the picture.)
