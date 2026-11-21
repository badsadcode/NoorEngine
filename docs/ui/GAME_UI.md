# Game UI: `.noorui` documents

Menus, settings screens, inventories, dialogue boxes: pages laid out and styled like small web pages, drawn over the
game. ([ADR-041](../decisions/ADR-041-game-ui.md): RmlUi does the layout and styling, behind NoorEngine's own API.)

For a score in the corner or a health bar, the **HUD** is still the quick way (`noor.ui.text`, `noor.ui.rect` from a
script, redrawn every frame). Documents are for anything with layout: panels, lists, buttons, pictures.

**What works today (gameplay foundations parts 6a to 6d):** pages built in the editor's **UI tab** without writing
markup ([below](#building-pages-in-the-editor)) or by hand; shown in a running game (the editor's Play, noor_player and
built games), laid out and styled, with text, rounded boxes, borders, images and clipped areas; gradients, shadows, blur
and other filters, frosted glass over the game, 2D and 3D transforms, masks ([Effects](#effects)); scripts (Lua and C#)
open, show, hide and close them, change what's in them, read and set their controls and react to clicks and changes;
the mouse, the keyboard (Tab, arrows, Enter) and typing in text boxes. **Coming:** gamepads (6e); binding data to a page
(a later step).

## Building pages in the editor

The **UI tab** (in the bottom row, beside Timeline) builds pages without markup:

![The editor with the UI tab open: a pause menu ("Paused", Resume, Quit) shown over the scene in the viewport with Quit outlined in orange; below, the page's elements as a tree (body, the panel, the title, two buttons) and the selected button's Id, Class and Text](../devlog/img/devlog098_ui_tab.png)

1. **A page:** pick one, or choose a template (Blank, Pause menu, HUD, Settings), type a name (`UI/menu.noorui`) and
   press **New page**. The viewport shows the page over your scene while you work.
2. **Select** an element by clicking it in the viewport or in the tree. It's outlined in orange.
3. **Change it:** its id (scripts find it by that), classes, text, and common styles (width, height, margin, padding,
   colours, font size, rounded corners, alignment). Enter or leaving the field applies it.
4. **Add inside it** from the palette: Panel, Title, Label, Button, Image, Slider, Checkbox, Text box. Each gets an id of
   its own (`button`, `button-2`...), ready for scripts. **Move up**, **Move down** and **Delete** arrange them.
5. **Undo** and **Redo** step through the page's changes.
6. **Put in the scene** adds an entity with the page as its `ui_document`, and **Play** shows it in the game. (Or use
   the Inspector: **+ Add component ▸ UI document…**.)

Every change is written to the page's file at once, and only its own part of the file changes: comments, formatting and
the style sheet you wrote stay as they were. For anything the tab doesn't offer, **Edit text in NoorCode** opens the
page itself; the viewport and the tab follow the file whichever way it changes (the tab reads it again within a
second, and an edit started from its old tree is refused rather than made in the wrong place).

**The built-in theme.** The palette's widgets look right on any page: the default style sheet has `noor-panel` (a
dark rounded panel), `noor-button` (with hover, focus and pressed looks), and a look for sliders, checkboxes, radio
buttons and text boxes (RmlUi's own inputs have none: a slider without styles is invisible). Your own styles win over
all of it.

## Showing a document

Put an entity in the scene with a `ui_document` component (the UI tab's **Put in the scene** does it):

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

## Effects

![Fourteen cards over a 3D scene, one effect each: linear, radial, conic and repeating gradients, a box shadow with a coloured ring, frosted glass blurring the scene behind it, a blurred card, a grayscale gradient, a drop shadow, a rotated card, one tilted in perspective, a rounded box cutting off a bar, a gradient fading out through a mask, and a half-transparent card](../devlog/img/devlog097_ui_effects.png)

*The render test's page (`tests/data/render_scenes/UI/effects.noorui`), straight from the test.*

| What | RCSS | Notes |
|---|---|---|
| Gradients | `decorator: linear-gradient(120deg, #FF5E62, #FFC857)`; `radial-gradient`, `conic-gradient`, `repeating-linear-gradient` and friends | Up to 16 colour stops |
| Shadows | `box-shadow: #00000099 0dp 12dp 24dp 0dp, #4B1AF2 0dp 0dp 0dp 4dp` | Several at once, `inset` too; drawn once and kept until the box changes |
| Filters | `filter: blur(3dp)`, `drop-shadow(#000C 6dp 6dp 4dp)`, `opacity(0.5)`, `brightness()`, `contrast()`, `grayscale()`, `sepia()`, `hue-rotate()`, `saturate()`, `invert()` | On the element and everything in it; several in a row |
| Frosted glass | `backdrop-filter: blur(10dp)` with a see-through background | Blurs what's behind, **the game included** |
| Transforms | `transform: rotate(12deg)`, `scale()`, `translate()`, `perspective(400dp) rotateY(35deg)` | Hit-testing follows: a rotated button is clicked where it's drawn |
| Clipping | `overflow: hidden` on a box with `border-radius` | Children are cut to the rounded shape |
| Masks | `mask-image: linear-gradient(0deg, #0000, #000F)` (or an image) | The element shows where the mask is opaque |
| Opacity | `opacity: 0.5` (each colour faded), or `filter: opacity(0.5)` (the element and its children as one picture) | |

Not supported: RmlUi's `shader` decorator (custom shaders: its own demo), and `mix-blend-mode`. A filter or shader the
renderer doesn't know is a warning in the log, and the element draws without it.

**Colours blend like a browser.** Since 6c the UI is blended in sRGB, as CSS is everywhere: `background-color: #0000008C`
darkens what's behind exactly as it would on a web page. (6a blended in linear light, which made dims look lighter
than their authors meant.)

## Mouse and keyboard

The mouse hovers and clicks (not while a game has captured it for looking around). **Tab** moves focus through the
elements with `tab-index: auto`; the **arrow keys** move it to the nearest one in that direction when an element has
`nav: auto` (or `nav-up`, `nav-down`... for exact neighbours); **Enter** or **Space** clicks what has focus; a document
takes the keyboard when it's opened or shown. In a **text box**, typing goes in (accents and other languages through
Windows' own input), with Backspace, Delete, Home, End, the arrows and Ctrl+A / C / V / X (the Windows clipboard: paste a name copied
from anywhere). Make focus visible with a
`:focus` style. Keys also reach the game as usual: a pause menu pauses the game, so its keys don't move the player.

**In the editor's Play** pages work with the mouse and the keys that move focus and press (Tab, arrows, Enter, Space,
Escape), but typing into a text box doesn't yet: the viewport doesn't pass typed letters on. In noor_player and built
games it does.

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
is the built-in font (Atkinson Hyperlegible) at 24 dp in a light colour, and the [built-in theme](#building-pages-in-the-editor)
styles `noor-panel`, `noor-button` and inputs. Anything in your own styles wins.

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
glyphs at each size used, images), a few hundred triangles a frame, and the UI's layers (made with the first page:
at 1280 x 720 about 26 MB of GPU memory, 40 MB with the effects page's nested layers).

GPU time of the UI a frame, measured at 1280 x 720 on an RTX 3080 over the light Daylight scene, where the GPU idles at
low clocks (so these are on the high side; a busy game's GPU runs faster): the pause menu **0.40 ms**, the fourteen-card
effects page **1.7 ms**. Every effect costs a few passes over the element's own area, so a menu with one frosted panel
costs much less than the test page. The UI is multisampled 2x (smooth rounded and rotated edges); the environment
variable `NOOR_UI_MSAA` (1, 2 or 4) changes that, for comparing: 4x cost the menu 0.67 ms, 1x 0.30 ms.

## Licences

UI documents use RmlUi (MIT) and FreeType (the FreeType License). Every built game carries their licences and a credit
line in `licenses/` (`RmlUi-LICENSE.txt`, `FreeType-FTL.txt`, `CREDITS.txt`); keep them with the game. If your game has
its own credits screen, adding "Portions of this software are copyright © The FreeType Project (freetype.org)" there is
the friendly thing to do.

## For engine programmers

`engine/ui` (`noor::ui::Ui`, `ui.h`) wraps RmlUi: its render interface records RmlUi's calls as a `render::UiFrame`, a
list of commands (draw, gradient, clip mask, push / composite / pop layer, save a layer as a texture or a mask) with
the geometry, transforms, gradients and filters they use, each call meaning what it does in RmlUi's reference renderer
(`RmlUi_Renderer_GL3.cpp`). The renderer runs them after the screen pass (`vulkan_ui.cpp`, `ui.hlsl`): the screen is
copied, the UI is drawn into layers of its own (8-bit, sRGB premultiplied, multisampled, a shared stencil for clip
masks), filters ping-pong between three single-sample images read through the bindless table (blur: halved, a 7-tap
Gaussian down and across, scaled back up), and the finished base layer is laid over the screen's copy in sRGB and
written back in linear light. A backdrop filter sees the screen under the base layer. Resolves and filters touch only
the element's area. All of it is made with the first frame that has UI. Its file interface reads only inside the
project; a plugin gives each document the default style sheet as it loads. One `Ui` at a time (RmlUi has global state). `WorldUi` (`world_ui.h`) is a running World's
`runtime::UiDocuments`: it starts the `Ui` with the first document, opens `ui_document` components, and turns RmlUi's
events into messages queued on the World from each document's emitter id (`World::ui_emitter`). Scripts' calls all go
through one function, `runtime::api::ui(world, caller, {"op": ...})`, Lua's and C#'s (rt API v18) alike. Tests:
`tests/unit/test_ui.cpp` and `test_ui_scripts.cpp` (no GPU), `render.golden_UI_Menu` and `render.golden_UI_Effects`
(the pictures, under Vulkan validation), `player.ui_lua_csharp` (a pause menu from Lua in a project, Lua and C# in a built game).
`tests/data/ui_project`'s `Scenes/Try` is a menu to play with by hand.

The UI tab (6d) works on files: `noor::ui::PageSource` (`page_source.h`) reads a page's `<body>` with where each element
is in its text, and makes each edit as a small splice of that text (re-read afterwards: an edit that would break the page
is refused), plus the palette and templates. The editor API's `noor_doc_ui` (v33) lists, makes, reads and edits pages,
with an undo per page; `noor_viewport_set_ui` shows a page over the scene, reloads it when its file changes, turns
clicks into `ui_pick` events (an element's path, counted the same way in RmlUi's document: `Ui::element_at`) and outlines
the selection (`Ui::element_box`). The viewport owns the editor's one UI: the preview while editing, the game's
documents (`WorldUi`, attached to the playing world) in Play; `noor_viewport_set_play(null)` detaches it from the world
before the document stops it. Tests: `test_page_source.cpp`, `api/test_ui_api.cpp`, and the editor smoke test builds a
menu, picks a button in the viewport, undoes, and plays the page.
