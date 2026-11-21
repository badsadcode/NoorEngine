# NoorEngine Devlog #096 — Buttons that do things

**Date:** 2026-11-21
**Build / commit:** Gameplay foundations, part 6b: UI documents from scripts

## This week we actually did

**Last devlog ended with Alden clicking "Resume" and nothing happening.** Now it does.

- **Scripts own menus.** `noor.ui.open("UI/pause.noorui")` opens a page; `menu:on("click", "resume", fn)` hears its
  button; `menu:set_text`, `set_value`, `value`, `set_class`, `show`, `hide`, `close` do the rest. C# has the same:
  `Ui.Open`, `On(menu, "click", "resume", ...)`. Both languages go through one engine function, so they can't drift
  apart.
- **Controls.** Text boxes give strings, sliders numbers, checkboxes true or false. A `change` event says what the new
  value is.
- **The keyboard.** Tab moves between buttons, arrows move to the nearest one in that direction, Enter presses. Text
  boxes take typing, accents included, with Backspace, Home, End and the arrows, and Ctrl+C / Ctrl+V use the real
  Windows clipboard.
- **Typos complain.** `menu:set_text("titel", ...)` is an error that says there's no element called "titel", not a
  silent nothing.
- **Menus tidy up after themselves.** A document belongs to the script that opened it and closes when that entity is
  destroyed.

![A pause menu over a dark 3D scene: "Paused", a "Your name" box with "Alden" in it, a volume slider at 50%, a Subtitles checkbox, a focused purple "Resume" button, "Reset", and a hint about mouse and keys; in the top right corner a HUD line "Hello, Alden | volume 50% | subtitles off"](img/devlog096_pause_menu.png)

*The menu to play with by hand (`tests/data/ui_project`, scene Try). The line in the corner is a second page, updated
by the same script as you type and slide.*

## Build of the Week 🏆

**The same pause, three times.** A test game's script pauses on frame 10, opens the menu, writes "Paused at 0.167" into
its title, sets the volume, ticks Subtitles, reads everything back and clicks Resume on frame 20. The game resumes on
frame 21, and a HUD page follows a moving box. Lua in the editor's project, Lua in a built game, C# in a built game:
the same line at the end, character for character.

## Sage vs. Reality

**Tab did nothing.** The first keyboard test pressed Tab and got silence. Pages were opened without the keyboard's
focus, so RmlUi sent the key to its root, which ignored it. Now a page takes the keyboard when it opens or reappears,
the way a dialog box would.

**A paste that couldn't leave the game.** RmlUi keeps its own clipboard unless told otherwise, so copying your name
from a website and pasting it into a game would have failed. It's wired to the Windows clipboard now, with a test.

**A name box sticking out.** In the hands-on menu the text box poked out past the panel's edge: its padding was added
to "width: 100%". One line of CSS (`box-sizing: border-box`), the same fix as on the web.

## Alden discovers ...

**... the name box.** He types "Alden", deletes it, types "Sir Alden", deletes it, types "Alden the Wayfarer", hits
Reset, and is "Alden" again. The game has been paused the whole time.
