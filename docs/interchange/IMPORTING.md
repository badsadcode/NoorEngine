# Importing Noor Interchange assets into NoorEngine

**Since:** Milestone 7, part 5 (2026-11-18). **Spec:** [Noor Interchange v0.4](NOOR_INTERCHANGE.md).

Anything a Noor tool exports (a NoorForm tree, a BadBuilding block, a CSG model exported from the
editor) arrives as a small set of files with one base name. Put the whole set anywhere under
`Assets/` and NoorEngine treats it as **one asset**:

```text
Assets/Trees/Oak_01.glb            the asset (LOD0, collision, markers)
Assets/Trees/Oak_01.lod1.glb       its levels of detail: part of Oak_01, never separate assets
Assets/Trees/Oak_01.lod2.glb
Assets/Trees/Oak_01.lod3.glb
Assets/Trees/Oak_01.noor.json      the manifest: what it is, where it came from
Assets/Trees/Oak_01.recipe.json    how to make it again (the generator's own format, with a seed)
```

## What the engine does with it

- **The id comes from the manifest.** The asset's GUID is the manifest's `id`, so scenes keep
  pointing at the tree when it's regenerated (same recipe and seed, same id) or moved. If the GLB is
  moved without its manifest, the id stays in its `.noormeta`.
- **Every level is checked.** Each level file is opened and compared with the manifest: the file is
  there and imports, its triangle count matches, its root node (`extras.noor`) says the same level,
  distance and billboard flag, and collision is only in level 0. Files with a SHA-256 in the
  manifest are checked against it (a changed file is a warning, not a refusal).
- **Collision is never drawn.** Nodes with `extras.noor.collision` become colliders when the model is
  placed in a scene: a `capsule`, `box` or `sphere` keeps its size (`radius`, `height`, `size`), a
  `mesh` uses its triangles, a `convex` piece (rocks) collides as the convex hull of its points (and,
  unlike a mesh, may move).
- **Everything else is kept and shown:** generator and version, recipe and seed (an unsigned 64-bit
  integer, never a float), family, tags, licence and attribution, validation, provenance, the
  tool's own block (e.g. `noorform`). **Wind data** (`wind.channels`) is kept but not used yet.
- **Mistakes are issues on their field**, never crashes or silent defaults: a seed written as a
  float, `+Z` up, a path that leaves the folder (`../`), levels with gaps or distances that don't
  grow, a newer major version. Errors make `noor_cli validate` fail; warnings are printed.

**Levels switch by distance** (Milestone 7, part 6, [ADR-032](../decisions/ADR-032-instancing-and-lod.md)):
a placed model is drawn whole at one level, chosen by its distance from the camera against the
manifest's `max_distance`s (which are for a 60° view: a narrower view, or a bigger model, keeps
detail further away). Level 0 is the model's own entities, so edits to its nodes show up close;
lower levels come from their files. In the editor and in games alike; picking uses level 0. No
cross-fade (levels pop) and no hysteresis yet. Changing only a `.lodN.glb` file doesn't trigger a
live reload yet.

## Where you see it

- **Editor:** select the asset in the Content Browser. The **Details** pane shows its kind, file,
  generator, recipe and seed, family, each level (triangles, size, the distance it's for, billboard),
  collision, wind, licence and any problems. (Editor API v22: `noor_doc_asset_details`.)
- **Command line:** `noor_cli info Assets/Trees/Oak_01.glb` prints the same story and checks every
  level (exit code 1 if something is wrong). `noor_cli validate <project>` checks every manifest.

```text
  manifest Oak_01.noor.json (Noor Interchange 0.4)
    id 719beb3e-327d-56ed-8d54-be8882f405fd, vegetation.tree "Oak_01"
    recipe Oak_01.recipe.json, seed 1
    family Oak, variant A of 9
    LOD0 Oak_01.glb                15268 triangles    3.06 MB  up to 29.0 m
    LOD3 Oak_01.lod3.glb               8 triangles    0.86 MB  no limit, billboard
```

## Code

- `engine/asset/include/noor/asset/interchange.h`: `read_manifest` (the checks above),
  `check_levels` (opens each level), JSON for the editor.
- `engine/asset/src/registry.cpp`: one asset per set (`*.lod<n>.glb` skipped), the id from the manifest.
- `engine/scene/src/scene.cpp` (`instantiate_model`): collision nodes become colliders with their size.
- Tests: `tests/unit/test_interchange.cpp`, `cli.info_interchange`, the API test "asset details
  tell a NoorForm oak's story", and the editor smoke test (the oak in the Content Browser and the scene).

## Trees from NoorMD before its export mode exists

NoorMD's **NoorEngine export mode** ([guide](NOORMD_NOORENGINE_EXPORT.md)) isn't written yet. Until
it is, `tools/interchange/noormd_engine_export.py` converts an ordinary NoorForm pack into the same
layout, following the guide's rules, so there are real files to import and test:

```bash
python tools/interchange/noormd_engine_export.py test_tree "Oak 01" tests/data/noorform_oak
```

It lives outside the engine on purpose: the engine reads only Noor Interchange and never learns
NoorForm's own pack format. When NoorMD writes the layout itself, its real export replaces the
converted fixture, and the converter can go.
