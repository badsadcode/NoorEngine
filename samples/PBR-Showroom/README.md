# PBR-Showroom

The Milestone 5 sample: the same set of materials in four kinds of light, to see that materials
look predictable wherever they are (the milestone's gate). Every scene is also a render test.

**Open it:** NoorEditor ▸ Open project ▸ this folder, then open a scene from the Content Browser.
Double-click any material to tune it in the material editor; the showroom updates as you go.

| Scene | Light | Exposure |
|---|---|---|
| `Showroom_Sun` | the default procedural sky and a 100,000 lux sun, with shadows | EV 14 |
| `Showroom_Studio` | the Studio Small 09 HDRI | EV 12.5 |
| `Showroom_Evening` | the Kloppenheim 06 HDRI (open land, evening) | EV 12 |
| `Showroom_Venice` | the Venice Sunset HDRI | EV 13 |

## What's in it

- **Back row: gold, roughness 0 → 1.** Mirror on the left, matte on the right. Metals have no
  diffuse colour: what you see is the environment, tinted gold.
- **Middle row: white plastic, roughness 0 → 1.** Always the same white underneath; only the
  reflection changes from a sharp highlight to a soft sheen.
- **Front row, named materials** (`Assets/Materials`): copper, chrome, red plastic, rubber,
  glass (see-through, `"alpha": "blend"`), a glowing ball (emissive; there's no bloom yet, so it
  reads as a flat bright disc).
- **Textured surfaces**, each made with **Make material** from a texture set: a castle brick wall,
  a worn concrete floor (both as material *instances* with tiling: `floor.noormat`,
  `wall.noormat`), mossy wooden planks on the crates, and a blue metal plate on the pedestal.
  Textures are BC7/BC5-compressed on import (12 maps of 1024²: 16 MB on the GPU).

The ball colours are measured values for real metals, in linear numbers (gold
`[1.0, 0.766, 0.336]`, copper `[0.955, 0.638, 0.538]`).

## Tests

`ctest -R golden_Showroom` draws all four scenes with Vulkan validation on and compares them with
`tests/golden/Showroom_*.png`. The first run compresses the textures (about 15 s in a Debug
build); later runs use `.noor/cache`.

## Assets and licences

All CC0 1.0 (public domain) from [Poly Haven](https://polyhaven.com), downloaded at 1K:

- Textures: Castle Brick 07, Concrete Floor Worn 001, Wooden Rough Planks, Blue Metal Plate
  (colour, OpenGL normal and ARM maps as JPEG, unchanged).
- HDRIs: Studio Small 09, Kloppenheim 06, Venice Sunset (`.hdr`, unchanged).

The scenes and materials are MIT like the rest of the repository.
