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
