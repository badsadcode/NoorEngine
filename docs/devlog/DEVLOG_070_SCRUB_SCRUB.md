# NoorEngine Devlog #070 — Scrub, Scrub

**Date:** 2026-11-20
**Build / commit:** Milestone 8C, part 6: the Animation panel

## This week we actually did

**Animation you can look at without pressing Play.** Until now, a character in the editor stood in its rest pose like
a mannequin in a shop window; its clips only moved when the game ran. Part 6 adds the editor's **Animation panel**, a
tab beside the Content Browser, so the viewport stays in view while you work.

![The editor: Ward in the viewport with his skeleton drawn over him in thin lavender lines, his right arm raised in a wave. Below, the Animation panel's Controller page: a layer called Arm drawn as two boxes, Off and Wave, joined by arrows, the arrow from Off to Wave highlighted; on the right the transition's fields (From Off, To Wave, Fade 0.3); along the bottom a timeline with the fade shaded in purple](img/devlog070_animation_panel.jpg)

*Ward's waving arm, previewed: the Off → Wave transition of his controller, the fade shaded on the timeline. A screen
grab from the editor's own smoke test.*

- **Clips:** pick one and it plays on the character in the viewport. Drag the timeline to scrub; Space plays and
  pauses; a speed, a loop, and *in place* to hold the hips still as root motion would.
- **The skeleton**, drawn over the model: a line per bone, a dot per joint.
- **The controller**, as a graph: states as boxes, transitions as arrows. Click a state and it plays from its start;
  click an arrow and you see that transition: the "from" state, then the cross-fade, shaded on the timeline. *Run*
  plays the whole controller with parameter values you set, triggers included. Every field of a state or transition
  can be edited, and the preview follows as you type.
- **The bone map** from part 5, finally visible: the preset it detected, its warnings, and all 22 humanoid bones.
  Change one and the bone lights up orange in the viewport; the correction is saved in the model's `.noormeta`.

All of it is a **preview**: the character is posed in what the viewport draws, never in the scene. Scrub as much as you
like: nothing to undo, nothing half-posed in a saved file.

![The Bone map page: the detected preset "names (edited)", two warnings that DEF-forearm.L is mapped as both LeftLowerArm and LeftHand, and two columns of bone rows (Hips, Spine, Chest, Neck, Head...) each with a drop-down of Ward's bones; in the viewport the forearm bone is drawn orange](img/devlog070_bone_map.jpg)

*The smoke test maps Ward's left hand to his forearm on purpose: the panel warns about it in plain words, and the
bone lights up in the viewport. Then it puts it back.*

## Build of the Week 🏆

**The preview is the game.** The animator's loading code (models, retargeting, controllers) moved into one shared
class, which the editor's preview now uses too. What you scrub in the panel is posed by exactly the code a built game
runs, so the editor can't show one walk while the game plays another.

## Sage vs. Reality

**The test that wasn't.** The editor API tests for this part failed with "test cases: 0" after the first full run. The
code was fine. Two test names had a semicolon in them, and CMake treats a semicolon as a list separator, so each name
was cut in two and neither half matched a test. Renamed, with commas.

**The panel that didn't fit.** In the first screenshot, the state graph and the bone list were squeezed out of sight:
the bottom dock is short, and the tab headers were the size of headlines. The tabs became a small switch in the
panel's header, and showing the panel now makes the bottom dock taller (once; it can still be resized).

**The test that looked at the hips.** The first scrubbing test checked that Ward's hips move between two moments of
his walk. They don't: it's an in-place walk. It checks his swinging hand now.

## Next engineering target

**Part 7: FBX, research first.** ufbx (MIT) tried against FBX files from Blender, Mixamo and Unreal. If it holds up,
an importer into the same skeletons and clips as glTF; if not, a written note on why.

## Bad Ideas Works Award for Questionable Decisions

**Winner:** A preview that refuses to touch the scene, in an editor whose whole job is touching the scene.
**Defence:** Ward has been posed and unposed a few thousand times today and his saved file still looks as it did
when he left it.
