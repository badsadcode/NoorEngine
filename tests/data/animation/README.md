# Animation test data (Milestone 8C)

Characters and rigs for skeletal animation tests. Everything here is CC0 (public domain) or made by NoorEngine's
own scripts; nothing here may need credit or permission.

| Folder | What | Source | Licence |
|---|---|---|---|
| `RobotExpressive/` | A small humanoid robot with 14 clips (Idle, Walking, Running, Jump, Wave, Punch, Death, Dance...). Mostly rigid parts riding on bones (only the hands are skinned), an armature scaled ×100, face morph targets: a good "real file" test | [three.js examples](https://github.com/mrdoob/three.js/tree/r160/examples/models/gltf/RobotExpressive) (r160). Model by Tomás Laulhé (Quaternius); morph targets and glTF conversion by Don McCurdy | CC0 1.0 (see its README) |
| `SimpleSkin/` | The glTF tutorial's two-bone skin: a strip of 10 vertices bent by one animated joint. Exact expected values for skinning maths | [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SimpleSkin) | The model files CC0 1.0; its metadata CC-BY 4.0 (we keep only the model and LICENSE.md) |

`Ward/` and `Retarget/` (Mast, Bollard) are made by `tools/samples/make_test_characters.py` (ours, MIT like the
repository).

Downloaded 2026-11-20, unchanged. Not used: the Khronos Fox (its rigging and animation are CC-BY 4.0), CesiumMan,
RiggedSimple and RiggedFigure (CC-BY 4.0), and Mixamo's characters and clips (not redistributable).
