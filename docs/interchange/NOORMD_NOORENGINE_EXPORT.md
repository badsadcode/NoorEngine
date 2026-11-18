# NoorMD: a "NoorEngine" export mode for NoorForm trees and rocks

**For:** whoever works on NoorMD (Tom, Noor, or a Sage session in the NoorMD repository).
**From:** NoorEngine engineering, 2026-11-18. **Follows:** [Noor Interchange v0.4](NOOR_INTERCHANGE.md)
(v0.4 adds the optional fields this mode needs; see the end).
**Checked against:** NoorMD `4905b22` (`TreeExport.cs`, `RockExport.cs`, `GlbWriter.cs`,
`GameProfiles.cs`) and a real export: the nine-oak variety pack `Oak.manifest.json` + `Oak_0N_LOD0..3.glb`.

## The idea

Add **NoorEngine** as a new choice in the export dialog's **For** list (beside the existing choices;
it's the value `GameProfile.Engine` carries). Today's exports stay exactly as they are. The new mode writes the
same trees and rocks laid out the way Noor Interchange says, so NoorEngine (and any Noor tool)
imports them with no special cases. NoorEngine will read **only** this layout; it won't learn
NoorForm's own manifest or `noor_*` extras. That's why this mode exists.

Everything below is plain glTF 2.0 and JSON. Nothing in it breaks Blender, Godot or three.js.

## What changes, at a glance

| | Today (any "For") | NoorEngine mode |
|---|---|---|
| Files per tree | `Oak_01_LOD0.glb` … `_LOD3.glb` | `Oak_01.glb` (LOD0) + `Oak_01.lod1.glb` … `Oak_01.lod3.glb` |
| Manifest | one `Oak.manifest.json` for the whole pack | one `Oak_01.noor.json` **per tree** (the pack file may stay, as an extra) |
| Recipe | `Oak_01.noortree.json` | `Oak_01.recipe.json` (same content; it already has `version` and `seed`) |
| Node extras | flat keys on the level root: `noor_lod`, `noor_visible_from`… | one block: `"extras": {"noor": {"v": 1, …}}` |
| Parts | `Trunk`, `Leaves` (names only) | the same names **plus** `"noor": {"v": 1, "part": "trunk" / "leaves"}` |
| Asset id | none | a UUID that stays the same when the tree is regenerated |
| Textures | PNG, identical copies in every level file | JPEG for opaque maps, PNG only where alpha is needed; smaller maps on far levels |
| Collision | trees: none; rocks: `-convcolonly` names | the same names **plus** `"noor": {"v": 1, "collision": …}` on those nodes; trees get a trunk capsule |

## 1. Files and names

For each tree or rock, one base name (the catalogue name if a game profile sets one, else the
recipe name with spaces turned into `_`, e.g. `Oak_01`):

```text
Oak_01.glb            LOD0, the closest level (plus the collision node)
Oak_01.lod1.glb       LOD1
Oak_01.lod2.glb       LOD2
Oak_01.lod3.glb       LOD3 (the billboard, when the recipe has one)
Oak_01.noor.json      the manifest (section 3)
Oak_01.recipe.json    the recipe (today's .noortree.json, renamed)
Oak_01.validation.json   optional: the Khronos validator's report
Oak_01.preview.png       optional: a 256–512 px thumbnail
```

A variety pack is simply several of these sets in one folder (`Oak_01.*` … `Oak_09.*`). Keeping
`Oak.manifest.json` next to them is fine: NoorEngine ignores files it doesn't know.

Why `.lod1.glb` and not `_LOD1.glb`: the spec settled on it (v0.2) so that LOD files are never
mistaken for separate assets, and NoorEngine's asset scanner already skips `*.lod<n>.glb` when it
looks for models. (Unity's `_LOD0` grouping still works in the other modes.)

## 2. Inside each GLB

**Level root node.** Replace today's flat keys with one `noor` block (the values you already compute
in `TreeExport.Tag` and `TreeExport.About`):

```json
"name": "Oak_01_LOD0",
"extras": { "noor": {
  "v": 1,
  "lod": 0,
  "max_distance": 29.01,
  "billboard": false,
  "kind": "vegetation.tree"
} }
```

- `lod`: the level. `max_distance`: today's `noor_visible_to` (metres; `0` or missing on the last
  level means "no limit"). `billboard`: today's `noor_billboard`. The lower bound isn't needed (it's
  the previous level's `max_distance`).
- Everything else NoorForm wants to keep (`noor_style`, `noor_sharp`, `noor_height`…) goes into the
  manifest's tool block (section 3), not the GLB.
- Node names: no spaces (`Oak_01_LOD0`, not `Oak 01_LOD0`), and stable across regenerations.

**Parts.** Tag the mesh nodes:

```json
{ "name": "Trunk",  "mesh": 0, "extras": { "noor": { "v": 1, "part": "trunk" } } }
{ "name": "Leaves", "mesh": 1, "extras": { "noor": { "v": 1, "part": "leaves" } } }
```

Separate branch meshes (`Branch_..`) get `"part": "branch"`; a billboard gets `"part": "leaves"`;
rocks get `"part": "detail"` unless a better word is added to the spec's list first.

**Collision.**
- **Rocks** already write collision pieces named `…-convcolonly`. Add
  `"noor": {"v": 1, "collision": "convex"}` to each of those nodes. The spec makes the flag the rule
  and the name the hint, and NoorEngine never draws a node with the flag.
- **Trees:** add one node to LOD0 only, a capsule (or a thin box) around the trunk's lowest 2–3 m,
  named `Trunk-colonly` with `"noor": {"v": 1, "collision": "capsule", "radius": r, "height": h}`. A
  mesh is fine too (`"collision": "mesh"`). Without one, NoorEngine makes its own from the bounds,
  and that's never as good.

**Vertex colours (`COLOR_0`).** glTF says `COLOR_0` multiplies the base colour. Write it in this
mode only if it really is a colour tint (and say so in the manifest). When the recipe puts wind in
vertex colours (`WindData == "color"`), use the UV channels instead in this mode (they're already
the default), so no engine tints the tree with wind data.

**Materials.** As today: `Bark` opaque; `Leaves` alpha `MASK` at 0.5 (single-sided cards with real
back faces is right; keep `doubleSided` false). `KHR_materials_diffuse_transmission` may stay: it's
in `extensionsUsed`, not `extensionsRequired`, which is exactly what the spec asks. NoorEngine
ignores it for now.

## 3. The manifest: `Oak_01.noor.json`

One per tree, following the schema (`noor.asset.schema.json`). The real Oak 01 would look like this:

```json
{
  "noor_interchange": "0.4",
  "id": "719beb3e-327d-56ed-8d54-be8882f405fd",
  "name": "Oak_01",
  "kind": "vegetation.tree",
  "generator": { "tool": "NoorForm", "version": "<NoorMD version>", "commit": "<NoorMD commit>", "determinism": "same_platform" },
  "recipe": { "file": "Oak_01.recipe.json", "seed": 1, "sha256": "<sha256 of the recipe file>" },
  "created": "2026-11-18T11:09:00Z",
  "space": { "up": "+Y", "forward": "+Z", "handedness": "right", "units": "meters" },
  "bounds": { "min": [-4.987, -1.871, -6.86], "max": [6.945, 11.603, 5.63] },
  "files": [
    { "path": "Oak_01.glb",      "role": "model", "sha256": "…" },
    { "path": "Oak_01.lod1.glb", "role": "lod", "lod": 1, "sha256": "…" },
    { "path": "Oak_01.lod2.glb", "role": "lod", "lod": 2, "sha256": "…" },
    { "path": "Oak_01.lod3.glb", "role": "lod", "lod": 3, "sha256": "…" },
    { "path": "Oak_01.recipe.json", "role": "recipe" }
  ],
  "lods": [
    { "level": 0, "triangles": 15268, "max_distance": 29.01 },
    { "level": 1, "triangles": 12256, "max_distance": 72.52 },
    { "level": 2, "triangles": 5178,  "max_distance": 181.29 },
    { "level": 3, "triangles": 8,     "billboard": true }
  ],
  "collision": "capsule",
  "family": { "name": "Oak", "variant": "A", "count": 9 },
  "wind": { "channels": { "bend": "TEXCOORD_1.x", "flutter": "TEXCOORD_1.y", "phase": "TEXCOORD_2.x", "sway": "TEXCOORD_2.y" } },
  "tags": ["oak", "broadleaf", "realistic"],
  "license": { "spdx": "MIT", "author": "Bad Ideas Works", "attribution": "" },
  "validation": { "gltf_validator": "pass", "errors": 0, "warnings": 0 },
  "noorform": { "style": "realistic", "sharp": false, "height": 11.6, "species": "oak", "texture": { "size": 1024 } }
}
```

Notes, field by field:
- **`id`**: must not change when the same tree is regenerated (NoorEngine keeps scene references by
  it). Use a **UUID v5** of `noorform:<kind>:<recipe name>:<seed>` (for example
  `noorform:tree:Oak 01:1`) under NoorForm's fixed namespace
  **`1f6b9f73-1acd-4a74-8358-4dba96ea8782`** (written here so every machine gets the same ids).
  `noorform:tree:Oak 01:1` gives `719beb3e-327d-56ed-8d54-be8882f405fd`. If a recipe already stores
  an `id`, that one wins.
- **`kind`**: `vegetation.tree`, `vegetation.plant` (grass, bushes) or `rock`.
- **`generator.version` / `commit`**: NoorMD's real version and commit. **`determinism`**:
  `same_platform` unless the generator avoids `sin`/`cos`/`pow` (and `MathF.Pow` in
  `SwitchDistances`) as section 5 of the spec describes. Then it may say `cross_platform`.
- **`recipe.seed`**: unsigned 64-bit; write it as a JSON integer, never a float.
- **`bounds`**: as today (Y up). The pivot stays at the base of the trunk on the ground; roots may go
  below y = 0 (v0.4 says so for vegetation).
- **`lods[].max_distance`** and **`billboard`**, **`family`**, **`wind`**: new optional fields in v0.4
  (below). They carry what NoorForm already knows, so engines don't have to guess.
- **`validation`**: NoorForm already runs the Khronos validator; put its result here (`"not_run"` if a
  particular export skipped it).
- **`noorform`**: anything else NoorForm wants to keep. Readers ignore blocks they don't know.

## 4. Textures: the big win

In the oak pack, each LOD0–LOD2 file is 5–6 MB, and **4.5 MB of that is the same six PNG
textures, copied into every level file**. The nine oaks are 170 MB; about three quarters of that is
duplicated texture data. In NoorEngine mode:

1. **JPEG for opaque maps** (`image/jpeg` is core glTF): bark colour, both ORM maps, and the normal
   maps at quality 92–95. PNG only where alpha matters: the leaves' colour (coverage), the billboard.
2. **Smaller maps on far levels:** LOD1 at half size, LOD2 at a quarter, unless the profile says
   otherwise. They're seen from 29 m and 73 m away.
3. Keep textures **embedded** (the spec requires it, so each file opens on its own in Blender and Godot).

Expected result: LOD0 about 2 MB, LOD1 under 1 MB, LOD2 a few hundred KB. A whole oak would drop
from about 19 MB to about 4 MB, so a nine-oak pack fits in a repository.

## 5. Rocks

The same rules: `Rock_01.glb` + `.lodN.glb`, `Rock_01.noor.json` (`"kind": "rock"`), the recipe as
`Rock_01.recipe.json`, the level roots' `noor_*` keys moved into `extras.noor`, and the collision
pieces flagged (`"collision": "convex"`). `noor_collision_pieces` can go into the `noorform` block.

## 6. How we'll check it (NoorEngine side)

When an export like this exists, NoorEngine's tests will:
1. validate each `.noor.json` against the schema;
2. import the tree as **one asset with four levels** (LOD0 from `Oak_01.glb`, the rest from the
   `.lodN` files), with its id, recipe, seed, bounds and `max_distance`s visible in the editor;
3. never draw the collision node, and use it for physics;
4. check that re-exporting the same recipe gives the same id (and, with `cross_platform`, the same bytes).

A small fixture (one tree, four levels, in this layout) committed to NoorMD's tests would let both
sides test against the same files.

## 7. Noor Interchange v0.4: what was added for this

All optional, so v0.3 files stay valid (details in [NOOR_INTERCHANGE.md](NOOR_INTERCHANGE.md), section 8):
- `lods[].max_distance` (metres, the camera distance up to which a level is used, for a 60° vertical
  field of view; engines scale it for theirs) and `lods[].billboard`;
- the same two on a level's root node in `extras.noor` (`lod`, `max_distance`, `billboard`);
- `family` (`name`, `variant`, `count`): variants that belong together;
- `wind` (`channels`): where wind data lives in the vertex attributes;
- vegetation pivots: the base of the trunk on the ground; roots may go below y = 0.

## Checklist for the implementation

- [ ] "NoorEngine" in the export dialog's For list (`GameProfile.Engine = "noorengine"`).
- [ ] File names: `<name>.glb`, `<name>.lod<n>.glb`, `<name>.recipe.json`, `<name>.noor.json`.
- [ ] `extras.noor` blocks on level roots (`lod`, `max_distance`, `billboard`) and parts (`part`).
- [ ] Collision: flag on rock pieces; a trunk capsule for trees.
- [ ] Manifest per asset with a stable UUID v5 id (namespace above), real generator version, recipe hash.
- [ ] JPEG for opaque maps, smaller maps on far levels.
- [ ] No wind data in `COLOR_0` in this mode.
- [ ] Validator result in the manifest; a fixture in NoorMD's tests.
- [ ] The hand-off note (`GameProfiles.Handoff`) lists the new file names in this mode.
