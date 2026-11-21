# NoorEngine Devlog #095 — Menus

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 6a: game UI documents

## This week we actually did

**We picked a UI library by building the same menu twice.** Until now a game's UI was the HUD: a script draws text
and rectangles every frame. Fine for a score, not for a settings screen. Part 6 started with a spike: one pause menu
made with our own small widget system, and the same menu made with **RmlUi**, an open-source (MIT) UI library that
reads HTML-like pages styled with CSS. Our own looked close and took 204 lines for one menu. RmlUi's took 40 lines of
page, came with rounded corners, flexbox, text boxes, scrolling, animation and gamepad navigation, and costs less a
frame. Tom accepted it ([ADR-041](../decisions/ADR-041-game-ui.md)), with RmlUi hidden behind our own API, so games
talk to NoorEngine, not to a library we might swap one day.

Then the real thing:

- **`.noorui` pages in games.** Put a `ui_document` component on an entity, point it at `UI/pause.noorui`, and it's drawn
  over the game in noor_player and in built games. Pages are laid out in `dp`, the HUD's unit (1/720 of the picture's
  height), so a menu is the same size at 720p, 4K or in a small window.
- **A UI pass in the renderer.** RmlUi hands us triangles, textures and clip rectangles; a new Vulkan pass draws them
  after the HUD, reading textures from the same table the 3D scene uses. CSS colours are sRGB, the renderer works in
  linear light: the shader converts, so `#4B1AF2` on screen is `#4B1AF2`.
- **Every page starts sensible.** RmlUi has no default styles at all (a `div` isn't even a block). Ours adds a tiny
  default sheet: blocks where you expect them, the built-in font, readable text.
- **Pages can only read their own project.** A page asking for `../../Windows/win.ini` gets a warning, not the file.
- **Credits where due.** Built games now carry RmlUi's and FreeType's licences and a credits file in `licenses/`.

![A pause menu ("Paused", Resume, Settings highlighted with a white border, Quit) and a Settings panel with an orange checker picture and a list (Music volume, Effects volume, Subtitles, and "Invert look" cut off by its box), on dark rounded panels over a dimmed 3D scene of grey boxes and a floor](img/devlog095_ui_menu.png)

*The render test's page, straight from the test run: a dimmed scene, rounded panels, three text sizes, a border, an
image, and a list cut off by its box.*

## Build of the Week 🏆

**The menu test runs under Vulkan validation.** The page above is drawn by the real player at 640 x 360, compared with
its reference picture, and the GPU's validation layer watches every call: zero errors. A second set of tests runs the
UI with no GPU at all, so its layout (where boxes go, what's clipped, what happens at half the resolution) is checked in
milliseconds.

## Sage vs. Reality

**"No font face defined", 35 times.** The first run looked perfect and filled the log with warnings. RmlUi styles a page
once while loading it, before I'd attached the default style sheet, so for that moment nothing had a font. RmlUi tells
plugins about a new page just before that first styling, so a small plugin now hands it the default sheet in time.

**The yardsticks said the player got slower.** Time to the first frame went from 0.65 s to 1.6 s, then the GPU time
jumped instead. Suspicious: a different number each run. I ran the player from before this part and the new one side by
side on the same games: the same times, to a few hundredths. Blender and a browser were busy on the GPU. What did
change for real: the player is 4.2 MB bigger, UI or not. That's written down.

**A golden picture at 320 x 180.** Our render tests shrink every reference to 320 x 180, which suits lighting tests
and is useless for text. UI references now keep their window's size.

## Alden discovers ...

**... the pause menu.** Alden freezes mid-stride as the screen dims, eyes the big purple "Settings" button, and clicks
"Resume". Nothing happens. Buttons that do things are part 6b. He's still standing there.
