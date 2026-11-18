# Test data

Files here are **generator output, stored byte for byte** (`.gitattributes`: `tests/data/** -text`),
because Noor Interchange manifests hold SHA-256 checksums of their sibling files.

## `badbuilding/`
One building from [BadBuilding](https://github.com/badsadcode/BadBuilding) (Bad Ideas Works),
commit `0903142`, Noor Interchange v0.2:

```bash
dotnet run --project src/BadBuilding.Cli -- generate --case l_block --out out
```

- `osm_w117821721.glb`: LOD0 with markers and the collision mass (7,432 + 28 triangles)
- `osm_w117821721.noor.json`: the manifest (id `4529993a-ec0c-5a66-bc58-462a7823fbc6`)
- `osm_w117821721.recipe.json`: the recipe

Deliberately **not** included, to keep the repository small: `.lod1.glb`, `.lod2.glb`,
`.validation.json`, `.metadata.json`. The manifest still lists them; tests don't rely on them.

The footprint comes from OpenStreetMap (© OpenStreetMap contributors, ODbL); see BadBuilding's
`samples/warsaw.json` for attribution.

## `noorform_oak/`
One oak from NoorMD's NoorForm (Bad Ideas Works, MIT), "Oak 01" (seed 1) from a nine-oak NoorForm variety pack,
**converted** into the NoorEngine export layout ([guide](../../docs/interchange/NOORMD_NOORENGINE_EXPORT.md))
because NoorMD doesn't write that layout yet:

```bash
python tools/interchange/noormd_engine_export.py test_tree "Oak 01" tests/data/noorform_oak
```

- `Oak_01.glb` + `Oak_01.lod1.glb` … `.lod3.glb`: four levels (15,268 / 12,256 / 5,178 / 8 triangles;
  level 3 is a billboard), JPEG textures (PNG where alpha matters), a trunk capsule in level 0
- `Oak_01.noor.json`: the v0.4 manifest (id `719beb3e-327d-56ed-8d54-be8882f405fd`, the UUID v5 the guide predicts)
- `Oak_01.recipe.json`: NoorForm's recipe, unchanged

6.5 MB in all. When NoorMD's export mode exists, its own export of this oak replaces these files.

## `forest_project/`
120 copies of the oak above on a 260 m square, with a sun and a camera at the edge (Milestone 7, part 6:
instancing and levels of detail), written by

```bash
python tools/samples/build_forest.py tests/data/forest_project --trees 120 --area 260 --name Forest
```

The oak's files are the same bytes as `noorform_oak/` (git stores them once). Each oak is placed the way
the editor places a model (a root with a `model` component and an entity per node), deterministically
(seed 7). Used by `render.golden_Forest` and `render.forest_instancing_lod`.
