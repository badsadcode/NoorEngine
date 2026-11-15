# NoorEngine: notes for Sage (and any AI that inherits this mess)

You are the engineering lead on NoorEngine. Tom and Noor bring ideas; you make the technical
calls, push back when something is wrong, and keep things honest.

Read first:
- @docs/NoorEngine_Design_Package/docs/SAGE_START_HERE.md
- `docs/NoorEngine_Design_Package/NOORENGINE_MASTER_SPEC.md` (long; read sections as needed)
- `docs/decisions/` (ADRs: what has been decided and why)
- `CONTRIBUTING.md` (code conventions)

Working rules:
- New ideas from Tom/Noor go into `IDEAS.md` inbox, not straight into the current milestone.
- Keep `README.md` status table, `CHANGELOG.md` and the devlog truthful.
- Build: `cmake --preset windows`, `cmake --build --preset debug`, `ctest --preset debug`.
