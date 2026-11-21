# Game UI: `.noorui` documents

Menus, settings screens, inventories, dialogue boxes: pages laid out and styled like small web pages, drawn over the
game. ([ADR-041](../decisions/ADR-041-game-ui.md): RmlUi does the layout and styling, behind NoorEngine's own API.)

For a score in the corner or a health bar, the **HUD** is still the quick way (`noor.ui.text`, `noor.ui.rect` from a
script, redrawn every frame). Documents are for anything with layout: panels, lists, buttons, pictures.

**What works today (gameplay foundations part 6a):** documents shown in a running game (noor_player and built games),
laid out and styled, with text, rounded boxes, borders, images and clipped areas; the mouse hovers. **Coming:** scripts
opening and closing documents and reacting to clicks (6b); gradients, shadows, blur, transforms (6c); a UI builder in
the editor, and documents in the editor's Play (6d); keyboard and gamepad navigation (6b, 6e).

## Showing a document

Put an entity in the scene with a `ui_document` component:

```json
"components": {
  "ui_document": { "document": "UI/pause.noorui", "visible": true }
}
```

The document opens when the game starts and is drawn over everything (the 3D or 2D scene and the HUD). `visible: false`
loads it hidden (scripts will show it, part 6b). Several documents can be open; later ones are drawn on top.

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
loads. One `Ui` at a time (RmlUi has global state). Tests: `tests/unit/test_ui.cpp` (no GPU), `render.golden_UI_Menu`
(the picture, under Vulkan validation).
