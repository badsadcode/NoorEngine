# Game UI: `.noorui` documents

Menus, settings screens, inventories, dialogue boxes: pages laid out and styled like small web pages, drawn over the
game. ([ADR-041](../decisions/ADR-041-game-ui.md): RmlUi does the layout and styling, behind NoorEngine's own API.)

For a score in the corner or a health bar, the **HUD** is still the quick way (`noor.ui.text`, `noor.ui.rect` from a
script, redrawn every frame). Documents are for anything with layout: panels, lists, buttons, pictures.

**What works today (gameplay foundations parts 6a and 6b):** documents shown in a running game (noor_player and built
games), laid out and styled, with text, rounded boxes, borders, images and clipped areas; scripts (Lua and C#) open,
show, hide and close them, change what's in them, read and set their controls and react to clicks and changes; the
mouse, the keyboard (Tab, arrows, Enter) and typing in text boxes. **Coming:** gradients, shadows, blur, transforms
(6c); a UI builder in the editor, and documents in the editor's Play (6d); gamepads (6e); binding data to a page
(a later step).

## Showing a document

Put an entity in the scene with a `ui_document` component:

```json
"components": {
  "ui_document": { "document": "UI/pause.noorui", "visible": true }
}
```

The document opens when the game starts and is drawn over everything (the 3D or 2D scene and the HUD). `visible: false`
loads it hidden (a script shows it). Several documents can be open; later ones are drawn on top. Or open one from a
script (below).

## From scripts

A pause menu in Lua, on a script that keeps running while the game is paused (`"pause": "always"`):

```lua
function Menu:on_start()
    self.menu = noor.ui.open("UI/pause.noorui")   -- opened and shown; it belongs to this script's entity
    self.menu:hide()
    self.menu:on("click", "resume", function() self.menu:hide(); noor.set_paused(false) end)
    self.menu:on("change", "volume", function(e) noor.log("volume " .. e.value) end)
end

function Menu:on_update()
    if noor.input.pressed("P") then
        noor.set_paused(true)
        self.menu:show()
        self.menu:focus("resume")
    end
end
```

The same in C#: `var menu = Ui.Open("UI/pause.noorui", Entity);` and `On(menu, "click", "resume", e => ...)`. Every
call is listed in [Lua](../scripting/LUA_API.md#ui-documents-menus) and [C#](../scripting/CSHARP_API.md#ui-documents-menus).

- **Elements** are named by their `id` (`"resume"` or `"#resume"`). One that isn't there is a script error naming it,
  so a typo doesn't pass in silence.
- **Events:** `click`, `change` (a control's value changed: `e.value` is the new one), `submit` (a form: `e.value` holds
  its fields), `focus`, `blur`, `mouseover`, `mouseout`. A listener gets `{type, element, value}`; `element` is the id
  of the element it happened on, or of its nearest ancestor with one (a click on a button's icon names the button).
  Listen to one element (`on("click", "resume", fn)`) or to all (`on("change", fn)`).
- **When:** events from the mouse and keyboard reach scripts in the same frame; events from what scripts did (`set_value`,
  `click`) in the next. The order is the same in every run.
- **Values:** text boxes and selects give strings, ranges numbers, checkboxes and radio buttons `true` / `false`.
- **Text vs markup:** `set_text` shows text exactly as given (`<` and `&` included); `set_rml` puts markup in.
- **Who owns a document:** a document a script opens closes when the script's entity is destroyed; one from a
  `ui_document` component, when that entity is. `noor.ui.find(path)` gets a component's document.
- `click(element)` presses a button from a script: handy for tests, tutorials and gamepad shortcuts.

## Mouse and keyboard

The mouse hovers and clicks (not while a game has captured it for looking around). **Tab** moves focus through the
elements with `tab-index: auto`; the **arrow keys** move it to the nearest one in that direction when an element has
`nav: auto` (or `nav-up`, `nav-down`... for exact neighbours); **Enter** or **Space** clicks what has focus; a document
takes the keyboard when it's opened or shown. In a **text box**, typing goes in (accents and other languages through
Windows' own input), with Backspace, Delete, Home, End, the arrows and Ctrl+A / C / V / X (the Windows clipboard: paste a name copied
from anywhere). Make focus visible with a
`:focus` style. Keys also reach the game as usual: a pause menu pauses the game, so its keys don't move the player.

## Writing one

A `.noorui` file is RML: HTML-like markup. Its look is RCSS: CSS, inline in `<style>` or in a `.rcss` file linked from
the head.

```html
<rml>
<head>
    <title>Paused</title>
    <link type="text/rcss" href="pause.rcss"/>
</head>
<body>
    <div id="menu">
        <h1>Paused</h1>
        <div class="button">Resume</div>
        <div class="button">Quit</div>
    </div>
</body>
</rml>
```

```css
body { width: 100%; height: 100%; display: flex; justify-content: center; align-items: center;
       background-color: #0000008C; }
#menu { width: 360dp; padding: 32dp; background-color: #191623F2; border-radius: 12dp; }
h1 { font-size: 64dp; color: #FFC857; text-align: center; }
.button { margin-top: 16dp; padding: 12dp 24dp; background-color: #2A2140; border-radius: 8dp; }
.button:hover { background-color: #4B1AF2; }
```

What you'd expect from CSS mostly works: block and inline layout, flexbox, tables, margins, padding, borders and
rounded corners, `overflow: hidden` and scrolling, `:hover` and other pseudo-classes, `@font-face`, transitions and
animations. RCSS isn't CSS in every detail; RmlUi's documentation lists the properties
(<https://mikke89.github.io/RmlUiDoc/>).

**Units.** Use **`dp`**: 1 dp is 1/720 of the game picture's height (the HUD's unit), so a page looks the same at 720p,
1080p or 4K and in a small window. `px` are real pixels. `em` and `%` work as in CSS.

**Defaults.** Every document starts from a small default style sheet: `div`, `p`, `h1`…`h6` and friends are blocks, text
is the built-in font (Atkinson Hyperlegible) at 24 dp in a light colour. Anything in your own styles wins.

**Fonts.** The built-in font is always there (`font-family: "Atkinson Hyperlegible"`). Your own fonts (TrueType or
OpenType) come in with `@font-face { font-family: "My Font"; src: url("/Fonts/MyFont.ttf"); }`. Check that a font's
licence lets you ship it in a game.

**Images.** `<img src="logo.png"/>` or `decorator: image(logo.png)`: PNG, JPEG, TGA, BMP.

**Paths.** Relative to the document, or from the project folder with a leading `/` (`/Assets/Textures/logo.png`). A
document can't read files outside its project: such paths are refused with a warning in the log.

**Problems.** A document that can't be read fails with the reason in the log (`[Player]` and `[UI]`); a property RmlUi
doesn't know is a warning, and the rest of the page still shows.

## Where it's drawn

Inside the game's picture (the display settings' rectangle, [DISPLAY.md](../rendering/DISPLAY.md)): with bars, the page
stays inside the bars; with a 320 x 180 pixel-art game scaled up, the page is laid out for the picture's real height (dp
keep it the same size), so its text stays sharp instead of being scaled up with the pixels.

## Costs

A game without a `ui_document` doesn't start the UI at all: no memory, threads or frame time (checked by the
yardsticks). The player program is about 4 MB bigger for carrying it. With a page open: its textures (the font's
glyphs at each size used, images), and a few hundred triangles a frame. The menu in the render test: 17 draws, 436
vertices, 5 textures (0.5 MB).

## Licences

UI documents use RmlUi (MIT) and FreeType (the FreeType License). Every built game carries their licences and a credit
line in `licenses/` (`RmlUi-LICENSE.txt`, `FreeType-FTL.txt`, `CREDITS.txt`); keep them with the game. If your game has
its own credits screen, adding "Portions of this software are copyright © The FreeType Project (freetype.org)" there is
the friendly thing to do.

## For engine programmers

`engine/ui` (`noor::ui::Ui`, `ui.h`) wraps RmlUi: its render interface turns geometry into `render::UiFrame` (indexed
triangles, a texture, a translation and a clip rectangle per draw), drawn by the renderer's UI pass (`vulkan_ui.cpp`,
`ui.hlsl`: premultiplied sRGB colours blended in linear light, textures from the bindless table with a clamping
sampler); its file interface reads only inside the project; a plugin gives each document the default style sheet as it
loads. One `Ui` at a time (RmlUi has global state). `WorldUi` (`world_ui.h`) is a running World's
`runtime::UiDocuments`: it starts the `Ui` with the first document, opens `ui_document` components, and turns RmlUi's
events into messages queued on the World from each document's emitter id (`World::ui_emitter`). Scripts' calls all go
through one function, `runtime::api::ui(world, caller, {"op": ...})`, Lua's and C#'s (rt API v18) alike. Tests:
`tests/unit/test_ui.cpp` and `test_ui_scripts.cpp` (no GPU), `render.golden_UI_Menu` (the picture, under Vulkan
validation), `player.ui_lua_csharp` (a pause menu from Lua in a project, Lua and C# in a built game).
`tests/data/ui_project`'s `Scenes/Try` is a menu to play with by hand.
