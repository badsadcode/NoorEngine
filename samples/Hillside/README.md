# Hillside

The Milestone 6 sample: a small brick house on a landscape, made only with NoorEngine's own
creation tools (the milestone's gate).

**Open it:** NoorEditor ▸ Open project ▸ this folder, then `Scenes/Hillside.noorscene`. Press Play:
the yellow walker follows the dirt path north, through the doorway, into the house.

## What's in it

- **The land** (`Hillside` entity, one 256 m terrain tile): gentle seeded hills (seed 2026), then
  sculpted: a level pad for the house, a long ridge behind it, a rocky knoll by the path, smoothed
  edges (`Assets/Terrain/Hillside.noorsculpt`). Change the seed in the Inspector: the hills
  underneath change and the pad, ridge and knoll stay.
- **Painted** with three materials (`Assets/Terrain/Hillside.noorpaint`): meadow grass everywhere,
  rock on the ridge and the knoll, a dirt path up to the door. Select the terrain and press Paint
  to add more.
- **The house** (`House`, a CSG model): four brick walls, a plank floor and roof, a chimney, an
  arched doorway and two windows cut out (subtract brushes). It collides as a mesh, so the walker
  can go in through the door and not through the walls. Select it and use Export GLB… to get a
  Noor Interchange asset.
- **Light:** a 100,000 lux sun and the Kloppenheim 06 sky (EV 14.5).
- **The forest** (Milestone 7: `Forest`, a scatter): 142 oaks of three kinds from NoorMD's NoorForm
  (`Assets/Trees`: Oak 01, 04 and 07, each with four levels of detail), placed by the procedural graph
  `Assets/Graphs/forest.noorgraph` (open it in the graph editor): only where the meadow was painted, kept
  off the path, out of the house's yard and out of the camera's view of the house. They stand on the
  sculpted ground and their trunks collide. Select `Forest` and use **Plant** / **Remove** to change it by
  hand; change the graph's seed for a different forest (your planted and removed trees stay).

## How it was made

`tools/samples/build_hillside.py` built it through the editor API, the same C functions NoorEditor
calls when you click: Make material for each texture set, Create ▸ Terrain, sculpt and paint
strokes (`noor_doc_sculpt`), brushes as `create` commands, Save. Run it again to rebuild the sample
from scratch, then `tools/samples/add_hillside_forest.py` for the forest (a graph file and a scatter entity,
saved). Doing it by hand in the editor takes the same steps, with a mouse.

The meadow is `Assets/Materials/meadow.noormat`, a material instance of Leafy Grass with a greener
base colour (Poly Haven's Leafy Grass is autumn-coloured).

## Tests

- `render.golden_Hillside`: the camera's view against `tests/golden/Hillside.png` (after `cli.cook_hillside`
  cooks the forest's graph).
- `cli.generate_hillside_forest`: the forest's graph gives the pinned fingerprint (Debug = Release), and
  `gate.hillside_forest_generate_equals_cook`: the same bytes as the editor's cook.
- `cli.build_hillside` + `player.hillside_walk`: a built game; the walker must end inside the house,
  standing on its floor (it does: it walks until it meets the inside of the north wall).
- `api: …the Hillside sample opens, and its house exports…`: the house exports as a GLB with a
  collision node, and reads back with the same triangles.

## Assets and licences

CC0 1.0 (public domain) from [Poly Haven](https://polyhaven.com), at 1K, unchanged:

- Textures: Leafy Grass, Rock Face, Dry Ground 01 (downloaded for this sample), Castle Brick 07 and
  Wooden Rough Planks (also in PBR-Showroom): colour, OpenGL normal and ARM maps as JPEG.
- HDRI: Kloppenheim 06 (`.hdr`).

The oaks: NoorMD's NoorForm (Bad Ideas Works, MIT), converted to the NoorEngine export layout by
`tools/interchange/noormd_engine_export.py` (about 6.8 MB each with their four levels).

The scene, materials, terrain layers, graph and script are MIT like the rest of the repository.
