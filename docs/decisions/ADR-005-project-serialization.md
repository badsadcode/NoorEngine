# ADR-005: Project serialization

**Status:** Accepted
**Date:** 2026-11-15

## Context
Projects, scenes and asset metadata must be diffable in Git, survive crashes, open in a text
editor when something goes wrong, and never be silently corrupted (spec 2.1 rules 7 and 12,
section 8.4, 9.2).

## Options
- **A: JSON text** for everything authored; binary only for derived/cooked data.
- **B: YAML.** Nicer to read, but a large ambiguous spec and slower parsers.
- **C: Custom binary.** Fast, but not diffable or hand-repairable.

## Decision
**JSON (A)**, with **nlohmann/json 3.12** (MIT) as the reader/writer, and these rules:
- Every file starts with a format-version key (`noor_project`, `noor_scene`, `noor_meta`).
- **Deterministic output:** keys in a fixed order (`ordered_json`), arrays in authoring order,
  2-space indent, `\n` line endings, trailing newline, shortest round-trip float formatting.
  Saving an unchanged document reproduces it byte for byte (tested).
- **Atomic saves:** write `<file>.tmp`, flush, then replace the original; the previous version
  is kept as `<file>.bak`. A crash mid-save leaves either the old or the new file, never half.
- **Paths inside a project** are relative, use `/`, and are rejected if they escape the project
  folder (`..`, absolute paths, drive letters).
- **Unknown data is preserved:** unknown keys and component types survive load/save untouched.
- Integers that can exceed 2^63 (recipe seeds) are read as **unsigned 64-bit**.
- Derived data (imported mesh caches, thumbnails) goes in `.noor/` and may be binary; it is never
  the source of truth and can always be rebuilt.

## Consequences
- Large worlds will need chunked scene files (one file per streaming cell) instead of one huge
  JSON; that's a later ADR (world/streaming).
- JSON is slower to load than binary; cooked runtime builds will use binary.

## Reviewed by
Sage.
