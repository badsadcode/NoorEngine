# FBX import: the research (Milestone 8C, part 7)

**Decision: go.** NoorEngine imports FBX through [ufbx](https://github.com/ufbx/ufbx) into the same skeletons, skins,
clips and meshes as glTF. glTF stays the main path (Noor Interchange is GLB plus a manifest); an FBX is simply
another model file in a project.

## The licence

ufbx 0.23.1 (commit `26a482ae`, 25 September 2026), two files (`ufbx.c`, `ufbx.h`), fetched by hash
(`cmake/NoorDependencies.cmake`) and compiled into `noor_asset`. It's offered under **MIT or public domain (the
Unlicense)**, whichever we prefer; we take MIT, like cgltf. No Autodesk FBX SDK is involved (its binaries are closed and
come with their own licence terms). Recorded in `third_party/manifest.json`.

## How it was judged

Every file was read twice, by two independent readers, and the two readings compared (`asset/model_compare.h`,
`noor_cli compare-models`):

- **Our characters** (Ward, Mast, Bollard, the CC0 robot): Blender imports the GLB and, from that one scene, exports
  an FBX and a glTF. NoorEngine's FBX import of the one is compared with its glTF import of the other.
- **Files we didn't make:** Blender's own FBX importer reads the FBX and writes it out as a GLB, and that is the
  comparison (`tools/samples/fbx_research.py`).

What's compared is what shows: the skeleton (nodes by name, their parents), every joint that moves something drawn
(skin joints, nodes carrying a rigid mesh, animated bones), at rest and 30 times a second through every clip, and the
drawn shape (skinned meshes posed, every vertex against the nearest of the other's), at rest and at three moments of
each clip. Not compared: bone orientations, vertex order and counts, which exporters are free to differ on.

## The files

| Files | Written by | Count | Result |
|---|---|---|---|
| Ward, Mast, Bollard and the robot (ours, CC0), exported by Blender | Blender 5.2's FBX exporter (and 3.3's for Ward and the robot) | 6 | Skeleton, skin and rest pose exact (under 0.01 mm). Clips within 1.6 mm (Ward) and 30 mm (the robot, with its x100 armature) with Blender's default curve simplification; **within 0.001 mm (Ward) and 0.008 mm (the robot) with it off** |
| Creatures from Tom's deep-sea project, in Unreal (eight fish, a hammerhead, a sea turtle, a nautilus, a mantis shrimp, a horseshoe crab; store-bought Fab packs, not included): rigged meshes and 79 clip files | Autodesk FBX SDK 2020.1 to 2020.3.4 | 97 | Every file imported. Joints within 0.14 mm, shapes within 0.002 mm, triangles equal, all 79 clips compared. Every creature plays its own clip files (copied by bone name) |
| Synty's locomotion clips and a character pack (paid, not included) | FBX SDK 2020.2.3 from Maya | 18 | Every file imported. Joints within 0.014 mm, shapes within 0.001 mm, 17 of 18 clips compared (Blender skips an empty take) |
| The same project's own Blender exports and Meshy-made rocks (static) | Blender 5.1's FBX exporter, Meshy | 12 | Every file imported, up to 1.2 million triangles (73 MB) in about a second. Shapes within 0.48 mm; two files 2 and 8 triangles apart (degenerate polygons, not looked into) |

**Not tried:** Mixamo's files (their licence doesn't allow redistributing them and none were on hand) and FBX written
by Unreal itself (no Unreal on the machine). Unreal and Mixamo both write FBX through the same Autodesk FBX SDK as the
Maya and store-pack files above, so the risk is small, but it is untested. Also untested: ASCII FBX and FBX older than
7.x (none in the set; ufbx reads both).

## What the research changed in the importer

- **The rest pose is where the skin was bound**: each skinned bone's cluster bind matrix, then the file's bind pose,
  then the node's own transform. Blender writes whatever pose the armature happened to be in as the nodes' own
  transforms, and a file's bind pose can leave bones out (the mantis shrimp's body was 24 mm off until the clusters were
  used; now 0.002 mm).
- **A bone a take doesn't animate stays at rest**, as in glTF; ufbx's baker would hand back its stored transform.
- **A take is played over its own time span**, starting at 0 (as FBX SDK tools and Unreal play them). Blender keeps a
  take's absolute start, so its clip holds still until the take begins (a fish's 4-second swim was 15.9 s in Blender).
  Keys outside the take are reported.
- **Names:** Blender's "Armature|Walk" takes are called "Walk".
- **A texture on the colour replaces the colour's value** (found rendering the creatures: their exporter left the value
  black, and the creatures came out black).
- **Clips from a file of their own** (an armature and its takes, no mesh: how FBX packs and Mixamo ship them) play on a
  character with the same skeleton, on any rig: `"retarget_from"` copies them by bone name when the clip file's bones
  sit at the same offsets under the same unanimated parents (and, if it's skinned, with the same rest rotations);
  otherwise it retargets, as for humanoids with other names.

## What Blender does (worth knowing when exporting)

- **"Key All Bones"** (on by default, for Unreal) gives every take a key on every bone. A bone an action doesn't move
  gets the pose it was last left in, not its rest: 13 mm at Ward's hand. Clear the pose before exporting.
- **Curve simplification** (on by default) thins the keys: about 1.5 mm on Ward's walk, 30 mm on the robot (it works in
  Blender units, and the robot's armature is scaled 100 times). Set it to 0 for exact clips.
- **The armature carries a x100 scale and a quarter turn** in Blender's FBX (centimetres, Z up, undone above the
  bones); its glTF doesn't. Imports land in the same place, but the bones inside are in different units, so an FBX's
  clips aren't copied onto the glTF's skeleton (they're retargeted).
- **Blender 3.3** names its glTF-imported actions "Walk_Ward", exports some of Ward's clips wrongly to glTF, writes a
  glTF of the robot that fails validation, and turns Ward's leg bones half round on import. 5.2 does none of that.
- Blender's glTF importer reads our generated Mast (bones in centimetres under a 0.01 armature) with joints up to 1.09 m
  from where NoorEngine puts them. Its FBX and glTF exports of what it read agree with each other exactly; which reading
  of Mast.glb is right wasn't looked into in part 7.

## Not imported

Blend shapes (morph targets), cameras and lights, constraints, animated properties other than node transforms.
Textures are used when embedded or found at the path the file gives or next to it (store packs often keep them in
another folder: the import says which are missing). Materials take the base colour, its texture, the normal map,
roughness, metalness, opacity and emission.
