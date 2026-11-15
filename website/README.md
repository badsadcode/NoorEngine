# NoorEngine website

The public site and devlog, published to GitHub Pages by
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml) on every push to `main`
that touches the site or its sources.

Nothing here is hand-copied. At build time the script reads:

| Source | Becomes |
|---|---|
| `docs/devlog/DEVLOG_NNN_SOME_TITLE.md` | `/devlog/NNN-some-title/`, the devlog index, `feed.xml` |
| `README.md`, section `## Honest status` | the status table on the home page (build fails if the section is missing) |
| `docs/decisions/README.md` | `/decisions/` (ADR links go to GitHub) |
| `website/assets/` | CSS, JS and web-sized images |

## Adding a devlog entry

Add `docs/devlog/DEVLOG_003_WHATEVER.md` with a `# Title — Subtitle` heading and a
`**Date:** YYYY-MM-DD` line, push to `main`. That is all. Relative links to other repo files are
rewritten to GitHub; links to other devlogs become site links; relative images are copied in.

## Local build

Needs Node 20+ (CI uses 24).

```bash
cd website
npm ci
npm test
npm run build
npm run serve
```

Then open http://localhost:4173/NoorEngine/ (same path as on GitHub Pages).

## Rules

- **Never invent progress.** Status comes from `README.md`; devlogs come from `docs/devlog/`.
- **Concept art is labelled CONCEPT ART** (master spec 34.4). `assets/img/concept-editor-clean.jpg`
  is a resized copy of `docs/NoorEngine_Design_Package/art/editor/NoorEditor_Mockup_02_Clean_Workspace.png`;
  the `icon-*.png` files are resized from `art/brand/NoorEngine_Icon_01_Transparent.png`.
  Concept art and logos are not MIT licensed.
- One dependency (`marked`, MIT), pinned exactly. Think twice before adding a second.

Code layout: `build.mjs` (file I/O, orchestration), `src/lib.mjs` (pure helpers, unit-tested in
`test/`), `src/templates.mjs` (HTML), `assets/` (static files), `serve.mjs` (local preview).
