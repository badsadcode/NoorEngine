# UI test

Gameplay foundations, part 6b: a script pauses the game with a menu (`UI/pause.noorui`), fills it in, reads it back,
clicks Resume and plays on, while a HUD page (`UI/hud.noorui`, opened by a `ui_document` component) shows a moving
box's position. Lua (`Scenes/Main`, `Scripts/Menu.luau`) and C# (`Scenes/Main_CSharp`, `Scripts/CsMenu.cs`) must end
the same: `tests/ui_check.cmake`.

`Scenes/Try` is for playing with by hand: a pause menu with a name box, a volume slider, a checkbox and buttons
(mouse, Tab and arrows, Enter, typing); P pauses again (`Scripts/Try.luau`, `UI/try.noorui`).

Gamepads (part 6e, `tests/pad_check.cmake`): `Scenes/Pad` (and `Pad_CSharp`) is a menu driven by the simulated pad in
`pad_script.json` (focus moved by the d-pad and the stick, South pressing, the pad unplugged and back, a second pad as
player 2); `Scenes/Rebind` (and `Rebind_CSharp`) rebinds jump to J and saves it, and finds it again in the next run.
