# Project templates

Clean starting projects (spec 24.1; the first three came with the 2D foundations, part 7). A template is a small game
that already works, made only of features any game can use, so everything in it can be read, changed or thrown away.

| Template | What you get |
|---|---|
| **Empty 2D** | An orthographic camera, pixel-perfect display settings (320 × 180, whole pixels), one sprite and one script |
| **2D Platformer** | A pixel-art tilemap level (solid ground, one-way planks), the built-in `platformer` character, a follow camera, a sky |
| **Top-Down 2D** | A tilemap meadow where water, trees and rocks block, a character steered by its own script (`"motion": "script"`) in every direction with a 4-view sprite, a follow camera |

## Making a project from one

- **The editor:** the Project Hub's **New Project…** asks for a name and what to start from (an empty project or a
  template), then where to put it, and opens the template's `Scenes/Main.noorscene`.
- **The command line:** `noor_cli templates` lists them; `noor_cli new <folder> <name> --template Platformer2D` makes
  one (a template's id, or the path of any template folder).

The new project gets its own id, name and date; the template's files are copied in (its `template.json` stays behind).

## Making your own

A template is a folder with a project's files (Assets, Scenes, Scripts, Settings; no `.noorproj`) and a `template.json`:

```json
{"noor_template": 1, "name": "My Shooter", "description": "What it starts you with.", "order": 50}
```

`noor_cli new <folder> <name> --template <that folder>` uses it. NoorEngine's own live in the repository's `templates/`
(written by `tools/samples/make_templates.py`) and are copied next to `noor_cli` and the editor when they're built.
Saving an open project as a template from the editor ("Custom" in spec 24.1) isn't there yet.

## Not yet

Empty 3D, First-Person, Third-Person, Vehicle and the rest of spec 24.1 (each with the group that makes it worth
having); choosing C# or Lua and other options when creating; your own templates in the editor's list.
