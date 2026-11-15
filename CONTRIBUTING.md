# Contributing to NoorEngine

Contributions are accepted under the [MIT license](LICENSE).

## Ground rules
- **Small, tested changes.** Every change keeps `ctest --preset debug` green.
- **No fake progress.** Docs, README status table and devlog describe what actually works.
- **Big decisions get an ADR** in `docs/decisions` (copy `ADR-000-template.md`).
- **New dependency?** Add it to `third_party/manifest.json` with license and pinned commit
  (policy: ADR-021, license rules: ADR-019).
- **New idea?** `IDEAS.md` inbox. Ideas don't skip the queue.

## Code conventions (C++)
- C++23, the subset in ADR-001. `snake_case` for functions/variables, `PascalCase` for types,
  `kPascalCase` for constants, namespaces `noor::<module>`.
- Recoverable errors return `noor::Result<T>`; asserts are only for broken internal invariants.
- Log through `NOOR_LOG_*` with a category; never `printf`/`std::cout` in engine code.
- Headers live in `engine/<module>/include/noor/<module>/`; only those are public.
- A module may depend only on modules below it: `foundation` ← `platform` ← `render` ← apps.
- Warnings are errors. Fix them; don't silence them without a comment explaining why.

## Commits
Imperative subject line ≤ 72 chars, body explains *why*.
