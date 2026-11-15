# ADR-019: Engine license

**Status:** Accepted
**Date:** 2026-11-15

## Context
Tom: "I believe in open source. I'm all for sharing knowledge and resources." We want people
to learn from, use and contribute to NoorEngine, including in commercial games.

## Options
- **A: MIT.** Shortest, most widely understood, used by Godot. Compatible with all planned
  dependencies.
- **B: Apache-2.0.** Adds an explicit patent grant; slightly more paperwork (NOTICE files).
- **C: GPL/LGPL.** Forces games or engine forks to share source; scares off game developers
  and complicates console ports.

## Decision
**MIT**, copyright Bad Ideas Works. Every dependency must be MIT-compatible for
redistribution (MIT, BSD, zlib, Apache-2.0, MIT-0, public domain, Boost). LGPL is allowed only
for optional, dynamically linked tools and requires a new ADR. Fonts under the SIL Open Font
License 1.1 may be bundled (e.g. Inter in the editor), since OFL explicitly permits bundling
with software; the font itself stays under OFL. GPL code is not allowed in
shipped engine, editor or runtime binaries.

## Consequences
- `third_party/manifest.json` records license per dependency; CI will check it later.
- Contributions are accepted under MIT (stated in CONTRIBUTING.md).
- Art assets (logos, mockups) are **not** automatically MIT; their license is decided separately.

## Migration or reversal strategy
Relicensing later requires agreement from all contributors, so this is effectively permanent.

## Reviewed by
Sage. Tom approved open source in principle.
