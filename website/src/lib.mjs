// Pure helpers for the NoorEngine website build. No file system access in here,
// so everything can be unit-tested (see ../test/lib.test.mjs).

import path from 'node:path';

export const REPO = 'badsadcode/NoorEngine';
export const REPO_URL = `https://github.com/${REPO}`;
export const BRANCH = 'main';

/** Escape text for safe use inside HTML text and attribute values. */
export function escapeHtml(text) {
  return String(text)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

/** Remove HTML tags and collapse whitespace (for excerpts and meta descriptions). */
export function stripTags(html) {
  return String(html)
    .replace(/<[^>]*>/g, '')
    .replaceAll('&amp;', '&')
    .replaceAll('&lt;', '<')
    .replaceAll('&gt;', '>')
    .replaceAll('&quot;', '"')
    .replaceAll('&#39;', "'")
    .replace(/\s+/g, ' ')
    .trim();
}

/** Shorten text at a word boundary, adding an ellipsis when cut. */
export function truncate(text, max) {
  if (text.length <= max) return text;
  const cut = text.slice(0, max);
  const lastSpace = cut.lastIndexOf(' ');
  return `${(lastSpace > max * 0.5 ? cut.slice(0, lastSpace) : cut).replace(/[\s,.;:—-]+$/, '')}…`;
}

const DEVLOG_FILE = /^DEVLOG_(\d+)_(.+)\.md$/i;

/**
 * Parse a devlog file name such as `DEVLOG_002_THE_PURPLE_RECTANGLE.md`.
 * Returns null for files that do not follow the naming convention.
 */
export function parseDevlogFileName(fileName) {
  const m = DEVLOG_FILE.exec(fileName);
  if (!m) return null;
  const number = Number.parseInt(m[1], 10);
  const words = m[2].toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
  return { number, slug: `${m[1]}-${words}` };
}

/**
 * Extract metadata from a devlog's Markdown source.
 * - title: the first `# ` heading (required)
 * - shortTitle: the part after an em dash, e.g. "The Purple Rectangle"
 * - date: `**Date:** YYYY-MM-DD`, or null if absent
 * - body: the Markdown without the title heading
 */
export function parseDevlogSource(markdown) {
  const text = markdown.replace(/^﻿/, '').replace(/\r\n?/g, '\n');
  const titleMatch = /^#[ \t]+(.+?)[ \t]*$/m.exec(text);
  if (!titleMatch) throw new Error('devlog has no "# Title" heading');
  const title = titleMatch[1].trim();
  const dash = title.split(/\s+[—–]\s+/);
  const shortTitle = dash.length > 1 ? dash.slice(1).join(' — ') : title;
  const dateMatch = /\*\*Date:\*\*[ \t]*(\d{4}-\d{2}-\d{2})/.exec(text);
  const body = text.slice(0, titleMatch.index) + text.slice(titleMatch.index + titleMatch[0].length);
  return { title, shortTitle, date: dateMatch ? dateMatch[1] : null, body: body.trim() + '\n' };
}

/**
 * Split the `**Key:** value` lines at the top of a devlog body (Date, Status, Build / commit...)
 * from the rest, so pages can show them as a tidy fact box. A horizontal rule right after the
 * block is dropped too. Returns { meta: [{ key, value }], rest }.
 */
export function splitDevlogMeta(body) {
  const lines = body.replace(/\r\n?/g, '\n').split('\n');
  const meta = [];
  let i = 0;
  for (; i < lines.length; i++) {
    const line = lines[i].trim();
    if (line === '') continue;
    const m = /^\*\*([^*\n]+?):\*\*\s*(.*)$/.exec(line);
    if (!m) break;
    meta.push({ key: m[1].trim(), value: m[2].trim() });
  }
  let rest = lines.slice(i);
  if (meta.length && rest.length && /^(-{3,}|\*{3,}|_{3,})$/.test(rest[0].trim())) rest = rest.slice(1);
  return { meta, rest: rest.join('\n').trim() + '\n' };
}

/**
 * Return the Markdown of the `## heading` section (heading excluded), up to the next
 * level-2 heading. Returns null when the heading does not exist.
 */
export function extractSection(markdown, heading) {
  const lines = markdown.replace(/\r\n?/g, '\n').split('\n');
  const start = lines.findIndex((l) => l.trim().toLowerCase() === `## ${heading}`.toLowerCase());
  if (start === -1) return null;
  let end = lines.length;
  let inFence = false;
  for (let i = start + 1; i < lines.length; i++) {
    if (/^\s*(```|~~~)/.test(lines[i])) inFence = !inFence;
    if (!inFence && /^##\s/.test(lines[i])) { end = i; break; }
  }
  return lines.slice(start + 1, end).join('\n').trim() + '\n';
}

/** Remove the first `# Title` line from Markdown (pages render their own title). */
export function dropTitle(markdown) {
  return markdown.replace(/\r\n?/g, '\n').replace(/^#[ \t]+.*\n?/m, '').trim() + '\n';
}

/** True for links that leave the repository or stay on the page (http:, mailto:, #anchor, //host). */
export function isExternalOrAnchor(href) {
  return /^[a-z][a-z0-9+.-]*:/i.test(href) || href.startsWith('#') || href.startsWith('//');
}

/**
 * Resolve a relative link found in `sourceFile` (repo-relative, POSIX separators)
 * to a repo-relative path. Returns { repoPath, hash } or null if the link points
 * outside the repository or is external.
 */
export function resolveRepoLink(href, sourceFile) {
  if (!href || isExternalOrAnchor(href)) return null;
  const hashAt = href.indexOf('#');
  const rawPath = hashAt === -1 ? href : href.slice(0, hashAt);
  const hash = hashAt === -1 ? '' : href.slice(hashAt);
  let decoded;
  try { decoded = decodeURI(rawPath); } catch { decoded = rawPath; }
  const joined = decoded.startsWith('/')
    ? decoded.slice(1)
    : path.posix.join(path.posix.dirname(sourceFile), decoded);
  const repoPath = path.posix.normalize(joined).replace(/\/$/, '');
  if (repoPath === '..' || repoPath.startsWith('../')) return null;
  return { repoPath: repoPath === '.' ? '' : repoPath, hash };
}

/** GitHub URL for a repository path (`tree` for directories, `blob` for files). */
export function githubUrl(repoPath, { isDir = false, hash = '' } = {}) {
  const kind = isDir ? 'tree' : 'blob';
  const encoded = repoPath.split('/').map(encodeURIComponent).join('/');
  return `${REPO_URL}/${kind}/${BRANCH}/${encoded}${hash}`;
}

/** Prefix that leads from a page at `depth` directories below the site root back to it. */
export function rootPrefix(depth) {
  return depth === 0 ? './' : '../'.repeat(depth);
}

/** Human-readable date, e.g. "15 November 2026". Accepts YYYY-MM-DD; returns '' for null. */
export function formatDate(isoDate) {
  if (!isoDate) return '';
  const [y, m, d] = isoDate.split('-').map(Number);
  const months = ['January', 'February', 'March', 'April', 'May', 'June', 'July',
    'August', 'September', 'October', 'November', 'December'];
  return `${d} ${months[m - 1]} ${y}`;
}
