// Builds the NoorEngine website into website/dist/.
//
// Sources (read at build time, never copied by hand):
//   README.md                  -> "Honest status" section on the home page
//   docs/devlog/DEVLOG_*.md    -> devlog index, one page per entry, Atom feed
//   docs/decisions/README.md   -> decisions page (ADR table, links go to GitHub)
//   website/assets/            -> CSS, JS, images (copied as-is)
//
// Usage: npm run build            (from website/)
// Env:   SITE_URL   absolute site URL for the feed (default: GitHub Pages URL)
//        GITHUB_SHA commit shown in the footer (falls back to `git rev-parse`)

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { Marked } from 'marked';
import {
  REPO_URL, stripTags, truncate, parseDevlogFileName, parseDevlogSource,
  splitDevlogMeta, extractSection, dropTitle, resolveRepoLink, githubUrl, rootPrefix,
} from './src/lib.mjs';
import * as tpl from './src/templates.mjs';

const WEBSITE_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(WEBSITE_DIR, '..');
const DIST = path.join(WEBSITE_DIR, 'dist');
const DEVLOG_DIR = 'docs/devlog';
const SITE_URL = (process.env.SITE_URL || 'https://badsadcode.github.io/NoorEngine/').replace(/\/?$/, '/');

const warnings = [];
const warn = (msg) => { warnings.push(msg); console.warn(`warning: ${msg}`); };

const repoFile = (rel) => path.join(REPO_ROOT, ...rel.split('/'));
const readRepoFile = (rel) => fs.readFileSync(repoFile(rel), 'utf8');

function commitSha() {
  if (process.env.GITHUB_SHA) return process.env.GITHUB_SHA;
  try {
    return execFileSync('git', ['rev-parse', 'HEAD'], { cwd: REPO_ROOT, encoding: 'utf8' }).trim();
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------------------
// Devlog discovery
// ---------------------------------------------------------------------------

function loadDevlogs() {
  const files = fs.readdirSync(repoFile(DEVLOG_DIR)).filter((f) => f.toLowerCase().endsWith('.md'));
  const entries = [];
  for (const file of files) {
    const name = parseDevlogFileName(file);
    if (!name) {
      warn(`${DEVLOG_DIR}/${file} skipped: name must look like DEVLOG_003_SOME_TITLE.md`);
      continue;
    }
    const sourcePath = `${DEVLOG_DIR}/${file}`;
    const parsed = parseDevlogSource(readRepoFile(sourcePath));
    if (!parsed.date) warn(`${sourcePath} has no "**Date:** YYYY-MM-DD" line; shown undated`);
    entries.push({ ...name, ...parsed, sourcePath });
  }
  const seen = new Map();
  for (const e of entries) {
    if (seen.has(e.number)) {
      throw new Error(`devlog number ${e.number} used twice: ${seen.get(e.number)} and ${e.sourcePath}`);
    }
    seen.set(e.number, e.sourcePath);
  }
  entries.sort((a, b) => b.number - a.number); // newest first
  if (entries.length === 0) throw new Error(`no devlog entries found in ${DEVLOG_DIR}/`);
  return entries;
}

// ---------------------------------------------------------------------------
// Markdown rendering with repository-aware links
// ---------------------------------------------------------------------------

const copiedMedia = new Set();

function headingId(text) {
  return stripTags(text).toLowerCase().replace(/[^\p{L}\p{N}]+/gu, '-').replace(/^-|-$/g, '');
}

/**
 * Render repository Markdown to HTML for a page `depth` levels below the site root.
 * Relative links are rewritten: devlogs to their site pages, the ADR index to the
 * decisions page, images into dist/media/, everything else to GitHub.
 */
function renderMarkdown(markdown, sourceFile, depth, devlogsByPath) {
  const root = rootPrefix(depth);
  const marked = new Marked({
    gfm: true,
    walkTokens(token) {
      if (token.type !== 'link' && token.type !== 'image') return;
      const resolved = resolveRepoLink(token.href, sourceFile);
      if (!resolved) return;
      const { repoPath, hash } = resolved;
      const abs = repoFile(repoPath);
      const exists = fs.existsSync(abs);
      if (!exists) warn(`${sourceFile}: link target "${token.href}" (${repoPath}) does not exist`);

      if (token.type === 'image') {
        if (exists) {
          const dest = path.join(DIST, 'media', ...repoPath.split('/'));
          if (!copiedMedia.has(dest)) {
            fs.mkdirSync(path.dirname(dest), { recursive: true });
            fs.copyFileSync(abs, dest);
            copiedMedia.add(dest);
          }
          token.href = `${root}media/${repoPath.split('/').map(encodeURIComponent).join('/')}`;
        } else {
          token.href = githubUrl(repoPath);
        }
        return;
      }

      const devlog = devlogsByPath.get(repoPath);
      if (devlog) {
        token.href = `${root}devlog/${devlog.slug}/${hash}`;
      } else if (repoPath === 'docs/decisions' || repoPath === 'docs/decisions/README.md') {
        token.href = `${root}decisions/${hash}`;
      } else {
        const isDir = exists && fs.statSync(abs).isDirectory();
        token.href = githubUrl(repoPath, { isDir, hash });
      }
    },
    renderer: {
      heading({ tokens, depth: level }) {
        const inner = this.parser.parseInline(tokens);
        if (level === 1) return `<h1>${inner}</h1>\n`;
        const id = headingId(inner);
        return `<h${level} id="${id}"><a class="anchor" href="#${id}" aria-hidden="true" tabindex="-1">#</a>${inner}</h${level}>\n`;
      },
    },
  });
  const html = marked.parse(markdown);
  // Wide tables scroll inside their own box on phones instead of widening the page.
  return html.replaceAll('<table>', '<div class="table-wrap"><table>').replaceAll('</table>', '</table></div>');
}

/** First real paragraph of a devlog (skips the **Key:** value metadata block). */
function excerptOf(entry) {
  const marked = new Marked({ gfm: true });
  for (const token of marked.lexer(entry.body)) {
    if (token.type !== 'paragraph') continue;
    if (/^\*\*[^*\n]+:\*\*/.test(token.raw.trim())) continue;
    const text = stripTags(marked.parseInline(token.text));
    if (text) return truncate(text, 240);
  }
  return '';
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

function writePage(relPath, html) {
  const dest = path.join(DIST, ...relPath.split('/'));
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.writeFileSync(dest, html);
}

function copyDir(src, dest) {
  fs.mkdirSync(dest, { recursive: true });
  for (const item of fs.readdirSync(src, { withFileTypes: true })) {
    const s = path.join(src, item.name);
    const d = path.join(dest, item.name);
    if (item.isDirectory()) copyDir(s, d);
    else fs.copyFileSync(s, d);
  }
}

function build() {
  fs.rmSync(DIST, { recursive: true, force: true });
  fs.mkdirSync(DIST, { recursive: true });
  copyDir(path.join(WEBSITE_DIR, 'assets'), path.join(DIST, 'assets'));

  const site = { sha: commitSha(), repoUrl: REPO_URL, siteUrl: SITE_URL };
  const devlogs = loadDevlogs();
  const devlogsByPath = new Map(devlogs.map((d) => [d.sourcePath, d]));
  for (const d of devlogs) d.excerpt = excerptOf(d);

  // Home: status comes straight from README.md so it cannot drift from the repo.
  const statusMd = extractSection(readRepoFile('README.md'), 'Honest status');
  if (!statusMd) throw new Error('README.md has no "## Honest status" section; the home page needs it');
  const milestoneMatch = /\*\*([^*]+?)\.?\*\*/.exec(statusMd);
  writePage('index.html', tpl.homePage({
    site,
    statusHtml: renderMarkdown(statusMd, 'README.md', 0, devlogsByPath),
    milestone: milestoneMatch ? milestoneMatch[1] : null,
    latest: devlogs.slice(0, 3),
  }));

  // Devlog index and entries.
  writePage('devlog/index.html', tpl.devlogIndexPage({ site, devlogs }));
  const inline = new Marked({ gfm: true });
  devlogs.forEach((entry, i) => {
    const { meta, rest } = splitDevlogMeta(entry.body);
    writePage(`devlog/${entry.slug}/index.html`, tpl.devlogEntryPage({
      site,
      entry,
      // The date is already in the page header; the other facts go in a small box.
      facts: meta
        .filter((m) => !(m.key.toLowerCase() === 'date' && entry.date))
        .map((m) => ({ key: m.key, valueHtml: inline.parseInline(m.value) })),
      html: renderMarkdown(rest, entry.sourcePath, 2, devlogsByPath),
      newer: devlogs[i - 1] ?? null,
      older: devlogs[i + 1] ?? null,
    }));
  });

  // Decisions: rendered ADR index, every ADR link goes to GitHub.
  const adrMd = dropTitle(readRepoFile('docs/decisions/README.md'));
  writePage('decisions/index.html', tpl.decisionsPage({
    site,
    html: renderMarkdown(adrMd, 'docs/decisions/README.md', 1, devlogsByPath),
  }));

  writePage('404.html', tpl.notFoundPage({ site }));
  writePage('feed.xml', tpl.atomFeed({ site, devlogs }));

  console.log(`Built ${devlogs.length} devlog entr${devlogs.length === 1 ? 'y' : 'ies'} into ${path.relative(process.cwd(), DIST) || '.'}`);
  if (warnings.length) console.log(`${warnings.length} warning(s); see above.`);
}

try {
  build();
} catch (err) {
  console.error(`build failed: ${err.message}`);
  process.exitCode = 1;
}
