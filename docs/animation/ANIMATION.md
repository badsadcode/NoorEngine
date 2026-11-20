# Skeletal animation

Milestone 8C ([plan](../milestones/M8_PLAN.md#8c-skeletal-animation),
[ADR-036](../decisions/ADR-036-skeletal-animation.md)). What works so far: **part 1, skinned meshes**, **part 2, clips and the player**, **part 3, blending and the controller**, **part 4, root motion and characters**, **part 5, retargeting**, **part 6, the Animation panel**, and **part 7, FBX**.

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
file format is described at the top of `engine/asset/include/noor/asset/anim_controller.h`. The editor's Animation
panel edits its states and transitions (below); its mistakes are reported in plain words ("layer 'Arm', transition
'Off' -> 'Wav': no such "to" state"), and a version with one isn't saved.

From scripts: `set_animation_parameter("speed", 3)`, `set_animation_trigger("nod")`, `play_animation("Nod")` (go to a
state now), `cross_fade("Nod", 0.2)`, `animation()` (the base layer's state), `set_animation_speed` and
`stop_animation` (the controller's time). `on_animation_end(name)` hears any layer's non-looping state finishing.

**Deterministic:** blending uses normalised linear interpolation (no trigonometry), mixes several poses in a fixed
order, and a controller's state moves only by the steps it's given: the same frames and parameters give the same
pose, bit for bit, in the project and in built games, from Lua and from C# (tested).

**Measured** (RTX 3080): 100 characters on Ward's controller (a three-clip blend space, half of them waving on a masked
layer, all leaning on an additive one) cost about 1.0 ms of game update a frame; GPU unchanged.

## Root motion, events and characters (part 4)

**Root motion** lets a clip move the character instead of only its hips: `"root_motion": true` on the animator. The
root bone (the topmost bone a clip moves: Ward's hips, a Mixamo or Rigify hips bone, a "root" bone) is held in place
horizontally, and how far the clip would have moved it each frame moves the character:

- inside a **character controller** (on the model or any entity above it: Create ▸ Player (third person) makes one),
  as the controller's velocity, so walls stop the walk and slopes and steps work;
- otherwise the model itself moves.

A looping walk covers its clip's distance exactly, every cycle (tested: within a centimetre after several cycles, in
any direction the character faces, and through a character controller until a wall). Up and down stays in the pose
(the bob of a walk), and turning is the script's. It works with plain clips, cross-fades (both clips move it, by the
fade), and controllers (the base layer; blend spaces mix their clips' movement by weight).

**Animation events** name moments: `"events": [{"clip": "Walk", "at": 0.25, "name": "left"}]` on the animator (`at`:
how far through the clip, 0 to 1), or `"events": [{"at": 0.25, "name": "left"}]` on a controller state. Scripts hear
`on_animation_event(name)` (C#: `OnAnimationEvent`) each time one is passed: footsteps, a sword's swing landing.

**Bones for scripts:** `entity:bone("DEF-hand.R")` (C#: `Entity.Bone`) finds a bone anywhere below the model. Bones are
entities, so a prop parented to one follows it (a sword in a hand: in the `Root_Motion` golden), and its world position
is where the hand is.

**Create ▸ Player (third person)** makes a playable character: the player (a character controller and
`Scripts/ThirdPersonPlayer.luau`), a camera rig that orbits with the mouse, and Ward on his controller, turning to face
where he walks and blending idle, walk and run by speed (Shift runs). Ward's model and controller are copied into
`Assets/Characters/Ward/` the first time. (The template moves him by script, which feels responsive; set
`"root_motion": true` and drive him by his clips instead if you prefer.)

## Retargeting (part 5)

One character's clips can play on another humanoid: a different height, different proportions, bones named by a
different tool, built in a T-pose or an A-pose. Put `"retarget_from": "<model id>"` on the animator:

```json
"animator": {"controller": "<Ward.nooranim>", "retarget_from": "<Ward_Anim.glb>", "parameters": {"speed": 1.4}}
```

The other model's clips then are this model's own: play them by name, run a controller written for them (its masks
may keep naming the other model's bones), use root motion and events as usual.

**Bone maps.** Both models get a humanoid bone map (22 bones: hips, spine, chest, upper chest, neck, head, and each
side's shoulder, upper arm, lower arm, hand, upper leg, lower leg, foot and toes; fingers aren't mapped yet). It's
detected from the bone names:

| Preset | Recognised by | For example |
|---|---|---|
| Rigify | its deform bones | `DEF-spine` (hips), `DEF-spine.001`... `.006` (head), `DEF-upper_arm.L`, `DEF-shin.R` |
| Mixamo | any `prefix:` (or `mixamorig_`) | `mixamorig:Hips`, `mixamorig1:LeftArm`, `LeftUpLeg`, `RightToeBase` |
| Unreal mannequin | | `pelvis`, `spine_01`, `clavicle_l`, `upperarm_l`, `calf_r`, `ball_r` |
| Blender metarig | | `spine`, `spine.001`, `upper_arm.L`, `shin.R` |
| names | common words, either side's spelling | `Hips`/`pelvis`, `UpperArm_L`, `L_Hand`, `Calf.R`, `DEF-upper_arm.L` |

The 15 bones a body needs (hips, spine, head, both arms' upper, lower and hand, both legs' upper, lower and foot) must
all be found; the rest are optional. When something's wrong (a bone missing, a forearm that isn't below its upper
arm, one bone used twice), the log says so in those words. Correct a map in the model's `.noormeta`:

```json
"settings": {"bone_map": {"LeftHand": "Hand_L_custom", "LeftToes": null}}
```

**How it moves the bones.** Directions, relative to each rig's rest pose: each mapped bone is first turned so it points
where the source's bone points at rest (an A-pose arm is lifted to a T-pose source's), then gets the same turn, in the
model's space, as the source bone makes from its rest. So at every moment the target's bones point where the
source's point (tested: within two degrees, at keys and between them); twist around a bone is the target's own. Head,
hands and toes (nothing mapped to point at) take their parent's correction. The hips also move: by the source's
movement scaled by leg length (hips height above the feet), so a shorter character takes shorter steps and root
motion carries him less far.

The clips are retargeted once, when a character first needs them, into ordinary clips of the target (keys at every
source key and at least 60 a second). Only bones a clip moves, or whose parents it turns, are written: Ward's Raise
lifts Bollard's right arm and leaves his left in his own A-pose.

Controllers' masks may name humanoid bones (`"mask": ["RightUpperArm"]`) on any model with a bone map, and, when
retargeting, the clips' model's bones (Ward.nooranim's `DEF-upper_arm.R` means Mast's `mixamorig:RightArm`).

The test bodies: **Mast** (2.0 m, Mixamo names, T-pose, bones in centimetres under an armature scaled 0.01) and
**Bollard** (1.45 m, Rigify's names with twist bones, A-pose, Blender's rolled bone frames), made by
`tools/samples/make_test_characters.py`; the `Retargeting` golden shows them beside Ward on his controller.

## The Animation panel (part 6)

A tab beside the Content Browser (so the viewport stays in view; it makes the bottom dock taller the first time). It
shows one character of the open scene: select one in the scene, or pick it at the top. Everything it shows is a
**preview**: the character is posed in the viewport only, and the scene isn't changed (no edit, nothing to undo). The
preview ends when the panel is hidden, and while playing the game shows the game.

- **Clips:** pick one and it plays; the timeline under the panel scrubs (drag it), with play and pause (Space), back to
  the start, a speed (a quarter to double) and looping. *In place* holds the hips horizontally, as root motion shows a
  walk. *Skeleton* draws the bones over the model: a line from each joint to the next, a dot on each joint.
- **Controller** (the character's animator's `.nooranim`): each layer's states as a graph (the entry state has a green
  edge; the state the preview is in is filled, with its fade's progress), its transitions as arrows. Click a state to
  preview it from its start, an arrow to preview that transition: its "from" state plays for its exit time (or half a
  time through, at most a second), then it fades to "to", the fade shaded on the timeline. *Run* plays the whole
  controller from its start with the preview's parameter values (floats, bools, and *Fire* for a trigger), as a game
  would (stepped at 60 a second). Fields edit the picked state (name, clip, loop, speed, entry) or transition (from,
  to, fade, exit time, conditions); new states and transitions, and deleting them. Every change is saved at once, and
  the preview follows. A blend space's samples are shown but are still edited in the file.
- **Bone map:** the humanoid map retargeting uses, for the character's model or the model its clips come from: the
  detected preset, the warnings, and each of the 22 bones (the 15 required in bold) with the node it's on. Change one
  and the correction goes into the model's `.noormeta` (`settings.bone_map`), retargeted clips are baked again, and the
  bone lights up orange in the viewport; ↺ puts a bone back to the detected one, *Detect all again* forgets every
  correction.

The engine side is `noor_doc_animation` (editor API v27): `model`, `set_bone`, `controller`, `save_controller`,
`characters` and `preview`. The preview poses the character with the same code a game uses (`game::AnimationAssets`,
shared with `AnimatorSystem`), so what you scrub is what plays.

## FBX (part 7)

FBX files are models like GLBs: put one in `Assets/`, and its skeleton, skin, clips, meshes and materials import the
same way (through ufbx, MIT; the research and the files tried are in [FBX_RESEARCH.md](FBX_RESEARCH.md)). Converted on
the way in to right-handed, +Y up, metres. Each take is a clip, played over the take's own span; Blender's
"Armature|Walk" is called "Walk". The rest pose is where the skin was bound.

**A character's clips in files of their own**, as FBX packs and Mixamo ship them (an armature with its takes, no mesh):
put each in the project and give the character's animator `"retarget_from"` naming the clip file. If the skeleton is the
same (the clip file's bones by name, at the same offsets under the same unanimated parents), the clips are copied as
they are, on any rig: a fish's, a turtle's. If it isn't, humanoids are retargeted (part 5).

**Exporting from Blender:** its FBX exporter keys every bone in every take by default, with the pose it was left in;
clear the pose (Alt+G, Alt+R, Alt+S on all bones) before exporting, or bones an action doesn't move come out bent. Its
curve simplification moves keys by a millimetre or two; set it to 0 for exact clips.

`noor_cli compare-models a.fbx b.glb` compares two imports of the same model (skeleton, joints through every clip,
shapes), and `tools/samples/fbx_research.py` runs that against Blender's reading of any folder of FBX files.

## Not yet
- FBX: blend shapes, cameras, lights; textures kept in another folder than the FBX names (the import lists them).
- Retargeting fingers, and correcting feet to the ground (IK): later. A target much stockier than the source may see
  hands pass through hips.
- The Animation panel: blend-space samples, layers (adding, masks, weights) and parameters (adding) are edited in the
  `.nooranim` itself; controller edits are saved at once, not undone with Ctrl+Z (the file can be put back by hand or
  from version control); the graph lays states out by itself (positions aren't saved); no curves or key editing.
- Morph-target (face shape) channels are skipped, with a warning.
- The editor's gizmo doesn't bend a skinned mesh while dragging (it follows when you let go); levels of detail don't
  switch skinned models; morph targets (face shapes) are skipped.
