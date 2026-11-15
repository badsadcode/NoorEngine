# NoorEngine Devlog #001 — The Terrible Idea

**Studio:** Bad Ideas Works  
**Date:** 2026-11-15  
**Status:** Planning / architecture  
**Real engine features completed:** Not claimed  
**Features requested:** Yes.

---

Today, we came to a difficult realization: apparently, there are not enough game engines.

We looked at Unreal Engine. We looked at Godot. We looked at Unity. We considered the extraordinary amount of engineering involved in building an independent engine.

Then we decided to build **NoorEngine** anyway.

The plan is very reasonable, provided nobody reads it aloud in one sitting. A complete renderer, an elegant editor, native and managed plugins, C# and Lua, physics, audio, networking, scripting, animation, beautiful materials, sculptable terrain, voxels, procedural trees, rocks, roads, buildings, cities, simulations and a full code editor.

We would also like pleasant error messages, safe projects, a solid build pipeline, a few playable demo games and the option to open a file without something exploding.

The last requirement is considered especially ambitious.

## What we actually accomplished

- Defined the vision for NoorEngine and NoorEditor.
- Produced visual concepts for the editor and the engine's branding.
- Wrote a substantial master specification and an initial architecture handoff.
- Agreed that other open-source solutions can be studied and suitable libraries reused responsibly.
- Agreed that the main editor should look like a professional creative application, not a debug window left in a basement since 2007.
- Agreed on the principle of **real, tested, maintainable foundations** instead of rushing public novelty builds.

**Important:** None of these planning achievements means the renderer, editor or sample games already exist.

## What broke

Nothing yet. This is deeply suspicious.

## Sage vs. Reality

Sage woke up.

Within a remarkably short time, we had planned an entire engine, a procedural creation ecosystem, a built-in IDE, template projects, an FPS example, a third-person adventure, a 2D platformer and a benchmark lab.

In Sage's defense, he did not ask for this.

## Our engineering philosophy

We want to build NoorEngine slowly, with decent architecture, tests, logs, documentation and a clear migration story. We can learn from other engines. We can use existing mature libraries. But the project must remain genuinely independent, rather than being a fragile collection of borrowed internals we don't understand.

We will avoid publicly presenting half-working experiments as completed engine milestones. Internal tests are still mandatory. We are reckless, not *that* reckless.

## Next objective

Create the real repository foundation, evaluate technical choices, decide how the runtime and editor are separated, and make the first minimal, tested part of the engine exist.

## Bad Ideas Works Award for Questionable Decisions

**This week's winner:** "What if we made the engine itself?"

**Award status:** Lifetime achievement.

---

*Bad Ideas Works — Code shall remain understandable to whichever AI inherits this mess.*
