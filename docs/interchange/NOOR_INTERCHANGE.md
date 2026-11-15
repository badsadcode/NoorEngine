# Noor Interchange v0.2

**Status:** Draft (v0.2, 2026-11-15). Owner: Bad Ideas Works. Decision record: [ADR-023](../decisions/ADR-023-noor-interchange.md).
Changes since v0.1: [section 8](#8-changes).

The shared rules for everything a Noor tool **exports** (NoorMD benches, Noor generators such as
NoorShip, NoorLevel, NoorArchitect, NoorVegetation, NoorRoads, NoorBridges) and everything
NoorEngine and other engines **import**.

The projects stay separate and move at their own pace. They connect through **files that follow
this document**, never through each other's code. If a tool follows these rules, its output works
in NoorEngine, opens in Blender and NoorMD, and degrades gracefully in Godot, Unity and Unreal.

**Golden rule: standard glTF first, `extras` second, readable names as the last resort.**

---

## 1. What an export contains

Every exported asset is a small set of files sharing one base name:

| File | Required | What |
|---|---|---|
| `<name>.glb` | ✅ | Geometry (LOD0), PBR materials, textures (embedded), lights, markers, collision |
| `<name>.lod<n>.glb` | optional | Lower levels of detail, one file per level (section 3.5) |
| `<name>.noor.json` | ✅ | **Manifest**: metadata about the asset (section 4). Schema: [`noor.asset.schema.json`](noor.asset.schema.json) |
| `<name>.recipe.json` | ✅ for generated assets | Everything needed to regenerate it: tool-specific format, but must contain `seed` and a `version` |
| `<name>.validation.json` | optional | The tool's own checks and the Khronos glTF validator report |
| `<name>.preview.png` | optional | Thumbnail, 256–512 px |

Several assets may share one folder. A level or a building with many parts is **one asset** (one
GLB with many nodes), not many files, unless the parts are meant to be reused separately.

## 2. Space, units and pivots

Same as [ADR-022](../decisions/ADR-022-math-and-coordinate-conventions.md) and glTF itself:

- **Right-handed, +Y up, metres, kilograms, seconds.** Angles in data are radians (degrees only in UI).
- **The front of an asset faces +Z** (the glTF convention).
- **Pivot (origin):**
  - props, vegetation, rocks, buildings, vehicles, ships: **bottom centre** of the footprint, ground at y = 0;
  - levels and worlds: the level's own origin (usually the player start area or a corner); markers carry absolute positions;
  - materials: no geometry; a preview mesh is optional.
- **Scale 1.0 on the root node.** Never bake a unit conversion into a node scale.

## 3. Inside the GLB

### 3.1 Geometry and materials
- Standard glTF 2.0 meshes, `pbrMetallicRoughness` materials, textures embedded in the GLB.
- Normals always present. Tangents present when a normal map is used.
- Color textures are sRGB; data textures (normal, roughness/metallic, AO, height) are linear.
- Extensions allowed without asking: `KHR_lights_punctual`, `KHR_texture_transform`,
  `KHR_materials_emissive_strength`, `KHR_mesh_quantization`. Anything else: mark it optional
  (`extensionsUsed`, not `extensionsRequired`) so other engines still load the file.

### 3.2 Lights
Use `KHR_lights_punctual` (point, spot, directional). Intensities in its physical units
(candela for point/spot, lux for directional). Blender, Godot and three.js read them natively.

### 3.3 Noor data in `extras`
All Noor-specific data on a glTF node lives under one key, `noor`, in that node's `extras`:

```json
"extras": { "noor": { "v": 1, "marker": "spawn.enemy", "class": "grunt", "id": "e07" } }
```

- `v` is the version of the `noor` block (currently `1`). Readers **ignore fields they don't know**.
- Blender shows `extras` as custom properties; Godot imports them as node metadata.

### 3.4 Markers (placeholders for engines to interpret)
A marker is an **empty node** (no mesh) placed and oriented where the thing should be.
Its forward direction is the node's +Z.

| `marker` | Meaning | Common fields |
|---|---|---|
| `spawn.player` | Player start | `team` |
| `spawn.enemy` | Enemy spawn | `class`, `difficulty`, `group` |
| `spawn.npc` | Non-hostile character | `class`, `role` |
| `pickup` | Item, ammo, health, key | `class`, `amount` |
| `entrance` | A place where you get **into** a building/ship/area: semantic, for navigation, AI, addresses and spawning. Usually sits in a doorway | `kind` (`main`, `service`, `courtyard`, `shop`, `gate`, `wicket`, `hatch`, `roof`) |
| `door` | The **physical, openable thing** (door leaf, gate wing, hatch cover) that an engine can animate, lock or break. A shop door is both: an `entrance` (kind `shop`) and, if the tool models the leaf, a `door` | `locked_by`, `width`, `height`, `opens` (`in`, `out`, `slide`, `up`) |
| `passage` | One **end** of a walkable way through something (a gate passage through a ground floor, a tunnel, an arcade). Ends come in pairs | `pair` (the other end's `id`), `kind` (`gate`, `tunnel`, `arcade`, `corridor`), `width`, `height` |
| `trigger` | Volume that fires an event | `size` [x,y,z], `event` |
| `waypoint` | Path / patrol point | `path`, `index` |
| `socket` | Attachment point for other assets | `accepts` (asset `kind`s) |
| `audio.emitter` | Sound source | `sound`, `radius` |
| `camera` | Suggested camera | `fov` (radians) |
| `light` | Light hint when `KHR_lights_punctual` can't express it | `kind`, `color`, `intensity` |
| `nav.link` | Jump, ladder, drop between walkable areas | `to` (marker `id`), `kind` |

Common fields on every marker: `id` (stable within the asset), optional `tags` (array of strings)
and optional `props` (free-form object for tool-specific data). New marker types: add them to this
table first (one PR), then use them. Never invent a marker silently.

### 3.5 Parts, LODs and collision
- **Semantic parts:** mesh nodes may carry `"noor": {"v": 1, "part": "wall"}`. Vocabulary:
  `wall`, `floor`, `ceiling`, `roof`, `window`, `door`, `stairs`, `railing`, `pillar`, `trunk`,
  `branch`, `leaves`, `hull`, `deck`, `detail`, `terrain`.
- **LODs: one file per level.** `<name>.glb` holds LOD0 (plus markers and collision);
  `<name>.lod1.glb`, `<name>.lod2.glb`, … hold the lower levels, each listed in the manifest with
  role `lod` and its `lod` number. Why files and not nodes: Blender, Godot and most viewers show
  every node, so LODs packed in one GLB appear stacked on top of each other; separate files open
  cleanly everywhere and let an engine load only the levels it needs.
  *Allowed alternative* (for tools that need one file): top-level wrapper nodes `<base>_LOD0`,
  `<base>_LOD1`, … each with `"noor": {"v": 1, "lod": n}`. Noor importers accept both.
- **Collision: the `extras` flag is the rule; the name is the hint.** Collision nodes carry
  `"noor": {"v": 1, "collision": "mesh"}` (or `convex`, `box`, `sphere`, `capsule`).
  **Importers must never render a node that has `extras.noor.collision`.** For engines that only
  read names, collision nodes are also named with Godot's *collision-only* suffixes:
  `-colonly` (for `mesh`, `box`, `sphere`, `capsule`) or `-convcolonly` (for `convex`), which
  Godot turns into a physics body without drawing the mesh. Do **not** use plain `-col`: Godot
  keeps that mesh visible. If an asset has no collision node, engines may generate one.

### 3.6 Names
Node names are readable and stable across regenerations with the same recipe:
`Marker_SpawnEnemy_e07`, `Light_Spot_03`, `Wall_North`, `Hull-colonly`. They are the fallback for
engines that ignore `extras`.

## 4. The manifest (`<name>.noor.json`)

Small, human-readable JSON next to the GLB. It lets an engine or a gallery understand an asset
**without opening the GLB**, and lets anyone see where it came from.

```json
{
  "noor_interchange": "0.2",
  "id": "4c1f7b2e-9a3d-4e51-8c0b-2f6d1e9a7b33",
  "name": "Pine_Tall_01",
  "kind": "vegetation.tree",
  "generator": { "tool": "NoorForm", "version": "2.0.0-preview.161", "commit": "a1b2c3d" },
  "recipe": { "file": "pine_tall_01.recipe.json", "seed": 42, "sha256": "…" },
  "created": "2026-11-15T16:20:00Z",
  "space": { "up": "+Y", "forward": "+Z", "handedness": "right", "units": "meters" },
  "bounds": { "min": [-2.1, 0.0, -2.0], "max": [2.2, 14.8, 2.1] },
  "files": [
    { "path": "pine_tall_01.glb", "role": "model", "sha256": "…" },
    { "path": "pine_tall_01.lod1.glb", "role": "lod", "lod": 1 },
    { "path": "pine_tall_01.lod2.glb", "role": "lod", "lod": 2 },
    { "path": "pine_tall_01.preview.png", "role": "preview" }
  ],
  "lods": [
    { "level": 0, "triangles": 18432 },
    { "level": 1, "triangles": 4210 },
    { "level": 2, "triangles": 512 }
  ],
  "collision": "capsule",
  "markers": { "spawn.enemy": 0 },
  "tags": ["conifer", "pine", "forest"],
  "license": { "spdx": "MIT", "author": "Bad Ideas Works" },
  "validation": { "gltf_validator": "pass", "errors": 0, "warnings": 0 }
}
```

Field rules (full detail in the schema):
- **`id`**: a UUID that stays the same when the asset is regenerated. Engines use it to keep
  references when files move. Two ways to get one, both fine:
  - *derived* (preferred when the asset has a natural key): a **UUID v5** of that key, e.g. an
    OSM id `osm:w123456`, under a namespace UUID that is fixed per tool and written in the tool's
    docs. Same input, same id, on any machine, with nothing to store;
  - *stored*: a random UUID v4 created on first generation and **kept in the recipe**.
  A request or an existing recipe that already names an id always wins.
- **`kind`**: dotted category: `building`, `ship`, `vehicle`, `level`, `prop`, `rock`,
  `vegetation.tree`, `vegetation.plant`, `vegetation.scatter`, `road`, `bridge`, `material`,
  `terrain`, `character`. Add new kinds to this list before using them.
- **`generator.version`**: the tool's real version; `commit` if known.
- **`recipe.seed`** + `recipe.sha256`: same tool version + same recipe ⇒ same asset (determinism).
- **`markers`**: counts per marker type, for galleries and quick checks.
- **`provenance`** (optional): which values were measured vs guessed, e.g.
  `{ "osm": ["levels"], "inferred": ["roof"] }` as NoorArchitect/BadBuilding already does.
- **Tool-specific data** goes in one top-level block named after the tool in lower case, e.g.
  `"noorarchitect": { … }` (or `"badbuilding"` until the rename). Readers ignore blocks they don't know.
- **Unknown fields are allowed**; readers ignore what they don't know.

## 5. Quality bar for every exporter
1. The GLB passes the **Khronos glTF Validator** with zero errors. Until a tool has the validator
   wired in, it writes `"gltf_validator": "not_run"` (honest beats green).
2. The manifest validates against `noor.asset.schema.json`.
3. Same tool version + recipe + seed ⇒ same geometry (byte-identical GLB is the goal).
4. Exporter tests open the result in at least one other program in CI where practical (NoorForm
   already imports trees into headless Godot).

## 6. Versioning
- This document is versioned (`noor_interchange` in the manifest, `v` in `extras.noor`).
- Adding optional fields or new marker types/kinds: minor version, no migration.
- Renaming or removing anything: major version, with a migration note here and support for the
  previous major in NoorEngine's importer.

## 7. Tool names (Noor family)
| Today | Becomes | Kind of output |
|---|---|---|
| BadShip | **NoorShip** | `ship` (spaceships, submarines; interiors later) |
| BadLevel | **NoorLevel** | `level` (blockouts with markers) |
| BadBuilding | **NoorArchitect** | `building` (from footprints; interiors later) |
| BadVegetation (planned) | **NoorVegetation** | `vegetation.*` |
| BadRoads (planned) | **NoorRoads** | `road` |
| BadBridges (planned) | **NoorBridges** | `bridge` |
| NoorMD benches | NoorForm, NoorMaterial, NoorMesh, NoorForge… | trees/rocks, materials, models |

*The mapping is a proposal; final names are Tom's call.*

## 8. Changes

### v0.2 (2026-11-15), from the first real exporter (BadBuilding, commit 6d59174)
- **Collision:** `extras.noor.collision` is the authoritative rule and importers must never render
  such nodes. The name suffix changes from `-col` to Godot's collision-only `-colonly` /
  `-convcolonly`, because `-col` keeps the mesh visible in Godot.
- **LODs:** one file per level (`<name>.lod<n>.glb`, manifest role `lod` with a `lod` number) is the
  canonical form; LOD wrapper nodes in one GLB remain allowed.
- **Markers:** new `passage` (paired ends of a walkway through something); `entrance` vs `door`
  clarified (place to get in vs. physical openable object); `entrance` kinds gain `courtyard`,
  `shop`, `wicket`, `roof`; `door` gains `opens`.
- **Asset id:** a UUID v5 derived from a natural key (per-tool namespace) is allowed alongside a
  stored UUID v4.
- **Tool-specific manifest block** named after the tool, lower case.
- Validation may be `not_run` until a tool wires in the Khronos validator.

v0.1 files stay readable: the only breaking change is the collision suffix, and importers rely on
the `extras` flag, which v0.1 already required.
