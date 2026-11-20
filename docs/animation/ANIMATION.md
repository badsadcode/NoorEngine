# Skeletal animation

Milestone 8C ([plan](../milestones/M8_PLAN.md#8c-skeletal-animation),
[ADR-036](../decisions/ADR-036-skeletal-animation.md)). What works so far: **part 1, skinned meshes**.

## Skinned meshes (part 1)

A **skinned mesh** bends with a skeleton: each vertex follows up to four joints, weighted. NoorEngine reads skins from
glTF/GLB files, as Blender exports them (Rigify with "Deform bones only" on, Mixamo characters through Blender, any
armature):

- **Joints are entities.** Placing a model makes an entity for every node of the file, joints included (as it always
  has for rigid models). A skinned mesh's entity lists its skin's joints by entity id (`"mesh": {"skin": 0, "joints":
  [...]}`), so moving, turning or scaling a joint entity bends the mesh: in the Inspector, from a script, and (part 2)
  from animations. A prop parented to a hand's joint entity follows the hand.
- **The pose comes from the joints' transforms.** A file whose joints are posed differently from how the mesh was bound
  draws posed straight away. As glTF says, a skinned mesh's own node transform is ignored: its joints put it in the
  world.
- **Rigid parts on bones work too.** Many characters (the CC0 robot in the tests) are separate rigid pieces parented to
  bones rather than one skinned mesh; they follow their bones as any child entity does.
- **Shadows** are cast by the posed shape.

What the importer fixes, and says so in plain words (Console, Import): vertices with more than four influences keep
the strongest four; weights that don't add up to 1 are scaled so they do; vertices without weights follow the first
joint; a skin with more than 256 joints, or with joints outside the file's scene, is drawn rigid, as bound.

### How it's drawn

The CPU works out each skinned mesh's palette every frame (for each joint: its world matrix times the inverse bind
matrix the file gives); the vertex shader blends a vertex's four joints' matrices by their weights (linear blend
skinning) and draws it there. The skin data (joints and weights, 24 bytes a vertex) and the palettes are read through
GPU buffer addresses, so no new pipelines or descriptor sets were needed. The same maths runs on the CPU
(`asset/skinning.h`) for tests, and for the editor's picking and framing of posed characters.

**Measured** (RTX 3080, 1280×720): 100 posed characters (17 joints, 2,856 triangles each, casting shadows) cost
0.2-0.4 ms of GPU; gathering that scene each frame (1,900 entities, bones included) about 1.2 ms of CPU
(`render.skinning_cost`).

### Test characters

`tests/data/animation/` (all CC0 or ours): **Ward** (`make_test_characters.py`: a smoothly skinned humanoid with
Rigify-style deform bones, in a bind and a posed version), **RobotExpressive** (Quaternius, CC0: 14 clips, rigid
parts on bones, skinned hands, an armature scaled ×100) and the glTF tutorial's **SimpleSkin**.

## Not yet

- **Animations** (clips, playback, blending, controllers): parts 2 and 3. Clips in files are read in part 2.
- The editor's gizmo doesn't bend a skinned mesh while dragging (it follows when you let go); levels of detail don't
  switch skinned models; morph targets (face shapes) are skipped.
