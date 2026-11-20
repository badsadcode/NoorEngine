# Skeletal animation

Milestone 8C ([plan](../milestones/M8_PLAN.md#8c-skeletal-animation),
[ADR-036](../decisions/ADR-036-skeletal-animation.md)). What works so far: **part 1, skinned meshes**, **part 2, clips and the player**, and **part 3, blending and the controller**.

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

## Clips and the player (part 2)

A **clip** is an animation inside a glTF/GLB file ("Walk", "Wave"...): keys that move nodes' translation, rotation and
scale over time. NoorEngine reads all three kinds of glTF interpolation: **step** (hold each key), **linear** (rotations
turn evenly, the shorter way round) and **cubic spline** (glTF's smooth curve through keys and tangents). Before its
first key a clip holds its first pose, after its last its last.

**Playing one:** an `animator` component on the model's root (the entity that has `"model"`):

```json
"animator": { "animation": "Walk", "loop": true, "speed": 1, "time_offset": 0.3 }
```

| Field | Meaning |
|---|---|
| `animation` | the clip's name, as in the file |
| `loop` | true (default): wraps round. false: holds the last pose; played by a script, it sends `on_animation_end` |
| `speed` | 1 = as authored |
| `time_offset` | seconds; so a crowd on the same clip isn't in step |
| `started`, `paused_at` | written by scripts (below) |

Without a script the clip plays on the scene's clock: an idle loop needs nothing else. The same fields, and the same
script functions, as sprites' animations (8B).

**From scripts** (Lua and C#; on an entity with an `animator`, or else a `sprite`):

| Lua | C# | |
|---|---|---|
| `entity:play_animation("Wave", 2)` | `Entity.PlayAnimation("Wave", 2)` | from its start, at a speed (default 1) |
| `entity:stop_animation()` | `Entity.StopAnimation()` | freezes the pose where it is |
| `entity:set_animation_speed(0.5)` | `Entity.SetAnimationSpeed(0.5f)` | from here on, without a jump (0 freezes) |
| `entity:animation()` | `Entity.Animation` | the clip it's playing |
| `function S:on_animation_end(name)` | `override void OnAnimationEnd(string name)` | a non-looping clip a script played has finished (once) |

**What moves:** each frame, every bone that any of the model's clips moves is set from the playing clip (so changing
clip puts the last clip's bones back to rest); the model's other entities are left alone. Bones are ordinary entities,
so the pose is their transforms, at runtime only: the saved scene keeps the rest pose.

**Deterministic:** a pose is a pure function of the clip and the time (no state carried from frame to frame), sampled
at the moment the frame ends, the same moment sprites show. The same game time gives the same pose in the editor, in
a built game, from Lua or C#, bit for bit (a test compares a fingerprint of every bone). One trap for scripts we fell
into ourselves: summing `dt` gives different totals in Lua (doubles) and C# (floats), so "after one second" can land a
frame apart. Count frames, or compare against the game's own clock, when two scripts must agree exactly.

**Placed before part 2?** A model's root now lists the entity each of its file's nodes became (`"model": {"nodes":
[...]}`), so clips find their bones even when they're renamed. A model placed before that has no list: its bones are
matched by name under the root, and the Console says so once. Placing it again fixes it.

**Measured** (RTX 3080): 100 walking characters (8 bones each moved) cost +0.43 ms of game update a frame and
+0.2 ms of GPU; gathering the scene stays at about 1.2 ms (`render.animation_cost`).

## Blending and the controller (part 3)

**Cross-fades without a controller.** `entity:cross_fade("Run", 0.25)` (C#: `Entity.CrossFade("Run", 0.25f)`) blends
from what's playing to the new clip over a quarter of a second, each clip on its own clock. The component remembers
what it's leaving (`"from"`, `"fade"`) until the fade is over: still a pure function of time.

**Controllers** (`.nooranim`, an asset like any other) are for characters with more than one thing going on. Give the
animator one, with the parameters it starts with:

```json
"animator": { "controller": "<Ward.nooranim's id>", "parameters": { "speed": 1.4, "waving": true } }
```

A controller has **parameters** (float, bool, trigger) that scripts set, and **layers**, each a little state machine:

- **States** play one clip, a **blend space** (1D: clips along one parameter, such as Idle at 0, Walk at 1.4 and Run
  at 4 by speed; 2D: clips on a plane of two parameters), or nothing (an empty state: fading to it fades the layer
  out). The clips of a blend space play **in step**: a walk and a run blended halfway put their feet down together.
- **Transitions** go from a state (or any, `"*"`) to another when all their conditions hold (a trigger or bool is
  set; a number is greater, less, equal...), and optionally not before the state has played through (`exit_time`: 1 =
  once). They cross-fade over `fade` seconds. Checked every frame before time moves on, so a fade of F seconds takes
  exactly F x 60 frames at 60 frames a second, and a trigger set in `on_update` changes the state that same frame.
- **Layers** stack on the first (the base, the whole body): an **override** layer replaces the bones in its **mask**
  (bone names, each with everything below it: `["DEF-upper_arm.R"]` is the whole right arm), by its weight; an
  **additive** layer adds how its clip differs from the clip's own first moment (a lean, a breath, a flinch) on top
  of whatever is playing, by its weight. A weight can be a parameter.

Ward's controller (`tests/data/render_scenes/Assets/Characters/Ward.nooranim`) is a complete small example: walk-run
by speed, a nod on a trigger that returns by itself, a waving arm on a masked layer, a lean on an additive one. The
file format is described at the top of `engine/asset/include/noor/asset/anim_controller.h`; until the Animation panel
(part 6) it's edited as text, and its mistakes are reported in plain words ("layer 'Arm', transition 'Off' -> 'Wav':
no such "to" state").

From scripts: `set_animation_parameter("speed", 3)`, `set_animation_trigger("nod")`, `play_animation("Nod")` (go to a
state now), `cross_fade("Nod", 0.2)`, `animation()` (the base layer's state), `set_animation_speed` and
`stop_animation` (the controller's time). `on_animation_end(name)` hears any layer's non-looping state finishing.

**Deterministic:** blending uses normalised linear interpolation (no trigonometry), mixes several poses in a fixed
order, and a controller's state moves only by the steps it's given: the same frames and parameters give the same
pose, bit for bit, in the project and in built games, from Lua and from C# (tested).

**Measured** (RTX 3080): 100 characters on Ward's controller (a three-clip blend space, half of them waving on a masked
layer, all leaning on an additive one) cost about 1.0 ms of game update a frame; GPU unchanged.

## Not yet

- **Animation events** (named moments in a clip, such as footsteps): with root motion, part 4.
- **Root motion**, sockets and bone transforms for scripts: part 4. Clips that move the hips move them in place.
- **Previewing clips in the editor** (a timeline to scrub): part 6, the Animation panel. Clips play when you press
  Play (and in games); in edit mode characters stand in their rest pose.
- Morph-target (face shape) channels are skipped, with a warning.
- The editor's gizmo doesn't bend a skinned mesh while dragging (it follows when you let go); levels of detail don't
  switch skinned models; morph targets (face shapes) are skipped.
