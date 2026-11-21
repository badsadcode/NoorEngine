# NoorEngine Devlog #097 — Frosted glass

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 6c: UI effects

## This week we actually did

**Menus got pretty.** When Tom asked whether RmlUi would let us make a beautiful interface, my answer was "yes, once
the renderer does its effects". It does now:

- **Gradients:** linear, radial, conic and repeating (stripes), with up to 16 colours.
- **Shadows:** soft box shadows, coloured rings, drop shadows that follow a shape.
- **Filters:** blur, grayscale, sepia, brightness, contrast, saturation, hue rotation, invert, opacity.
- **Frosted glass:** a see-through panel with `backdrop-filter: blur()` blurs *the game* behind it, not just other
  menus. A pause menu can let the paused world show through, out of focus.
- **Transforms:** rotate, scale and 3D perspective. Clicks still land where things are drawn.
- **Rounded clipping and masks:** a rounded panel cuts off its children at its corners; a gradient mask fades an
  element out.

![Fourteen cards over a 3D scene, one effect each: four gradients, a box shadow with a purple ring, frosted glass blurring the horizon behind it, a blurred teal card, a grayscale gradient, a drop shadow, a rotated card, one tilted in perspective, a rounded box cutting off a yellow bar at its corners, a gradient fading out through a mask, and a half-transparent card](img/devlog097_ui_effects.png)

*The new render test: one effect per card, under Vulkan validation, compared with its reference picture every build.*

## Build of the Week 🏆

**Fourteen effects, first try.** RmlUi's reference renderer is OpenGL; ours is Vulkan, built differently, with the UI
drawn into layers of its own and then laid back over the game. The test page above came out right on its first run,
with zero validation errors. The one card I changed afterwards was my own test's fault: the bar meant to prove rounded
clipping didn't touch a rounded corner, so it proved nothing. Now it crosses the corners, and they cut it.

## Sage vs. Reality

**The menu turned darker, and that's the fix.** The 6a menu's dim background was blended in linear light, the way the
3D renderer blends. CSS blends in sRGB: 55% black over a scene darkens it to 45% of its sRGB value, on every web page
ever. Ours looked lighter than its author meant. Now the UI blends like a browser, and the old reference picture was
the one that was wrong.

**My guess about the cost was wrong.** A plain menu cost 0.66 ms of GPU time, which seemed far too much. I was sure it
was full-screen memory traffic, rewrote the passes to touch only each element's area, measured again... and saved
almost nothing. What helped was the multisampling: 2 samples per pixel instead of 4 (RmlUi's own choice) brought the
menu to 0.40 ms, with edges you can't tell apart at normal size. These numbers come from a light scene where the GPU
idles at low clocks, so a busy game pays less. All three settings are measured and written down.

**A helper that wasn't allowed in.** The UI renderer couldn't see the renderer's internals (they're private), so it
became a nested part of them. Then every local variable called `info` "hid" one of the renderer's own, 61 of them, and
warnings are errors here. Renamed.

## Alden discovers ...

**... frosted glass.** The pause menu goes up and Hillside behind it goes soft, like looking through a bathroom window.
Alden walks up to the menu, squints at the blur, and wipes it with his sleeve. Nothing changes. Some things are
working as intended.
