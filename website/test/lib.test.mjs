// Unit tests for the website build helpers. Run: npm test (from website/).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  parseDevlogFileName, parseDevlogSource, splitDevlogMeta, extractSection, dropTitle,
  resolveRepoLink, githubUrl, rootPrefix, truncate, stripTags, escapeHtml, formatDate,
} from '../src/lib.mjs';

test('devlog file names give a number and a URL slug', () => {
  assert.deepEqual(parseDevlogFileName('DEVLOG_002_THE_PURPLE_RECTANGLE.md'),
    { number: 2, slug: '002-the-purple-rectangle' });
  assert.deepEqual(parseDevlogFileName('devlog_010_Big_Day.md'), { number: 10, slug: '010-big-day' });
  assert.equal(parseDevlogFileName('README.md'), null);
  assert.equal(parseDevlogFileName('DEVLOG_DRAFT.md'), null);
});

test('devlog source: title, short title, date and body', () => {
  const src = '# NoorEngine Devlog #002 — The Purple Rectangle\r\n\r\n**Date:** 2026-11-15\r\n\r\nHello.\r\n';
  const d = parseDevlogSource(src);
  assert.equal(d.title, 'NoorEngine Devlog #002 — The Purple Rectangle');
  assert.equal(d.shortTitle, 'The Purple Rectangle');
  assert.equal(d.date, '2026-11-15');
  assert.equal(d.body, '**Date:** 2026-11-15\n\nHello.\n');
});

test('devlog source without a dash or date still parses', () => {
  const d = parseDevlogSource('# Just a title\n\nText.');
  assert.equal(d.shortTitle, 'Just a title');
  assert.equal(d.date, null);
});

test('devlog source without a title is rejected', () => {
  assert.throws(() => parseDevlogSource('No heading here.'), /no "# Title"/);
});

test('metadata block is split from the body, trailing rule dropped', () => {
  const { meta, rest } = splitDevlogMeta('**Studio:** BIW  \n**Date:** 2026-11-15  \n\n---\n\nToday we...\n');
  assert.deepEqual(meta, [{ key: 'Studio', value: 'BIW' }, { key: 'Date', value: '2026-11-15' }]);
  assert.equal(rest, 'Today we...\n');
});

test('body without metadata is left alone (rule kept)', () => {
  const { meta, rest } = splitDevlogMeta('---\n\nText\n');
  assert.deepEqual(meta, []);
  assert.equal(rest, '---\n\nText\n');
});

test('extractSection stops at the next level-2 heading but not inside code fences', () => {
  const md = '# T\n## Honest status\nA\n```\n## not a heading\n```\n### Sub\nB\n## Build\nC\n';
  assert.equal(extractSection(md, 'Honest status'), 'A\n```\n## not a heading\n```\n### Sub\nB\n');
  assert.equal(extractSection(md, 'Missing'), null);
});

test('dropTitle removes only the first H1', () => {
  assert.equal(dropTitle('# A\n\ntext\n# B\n'), 'text\n# B\n');
});

test('relative links resolve against the source file', () => {
  assert.deepEqual(resolveRepoLink('ADR-001-x.md', 'docs/decisions/README.md'),
    { repoPath: 'docs/decisions/ADR-001-x.md', hash: '' });
  assert.deepEqual(resolveRepoLink('../NoorEngine_Design_Package/SPEC.md#33', 'docs/decisions/README.md'),
    { repoPath: 'docs/NoorEngine_Design_Package/SPEC.md', hash: '#33' });
  assert.deepEqual(resolveRepoLink('docs/', 'README.md'), { repoPath: 'docs', hash: '' });
  assert.deepEqual(resolveRepoLink('/LICENSE', 'docs/devlog/X.md'), { repoPath: 'LICENSE', hash: '' });
  assert.deepEqual(resolveRepoLink('My%20File.md', 'README.md'), { repoPath: 'My File.md', hash: '' });
});

test('external links, anchors and escapes from the repo are not rewritten', () => {
  assert.equal(resolveRepoLink('https://vulkan.lunarg.com/', 'README.md'), null);
  assert.equal(resolveRepoLink('mailto:x@example.com', 'README.md'), null);
  assert.equal(resolveRepoLink('#build', 'README.md'), null);
  assert.equal(resolveRepoLink('../../outside.md', 'docs/README.md'), null);
});

test('GitHub URLs use tree for directories and blob for files', () => {
  assert.equal(githubUrl('docs/decisions', { isDir: true }),
    'https://github.com/badsadcode/NoorEngine/tree/main/docs/decisions');
  assert.equal(githubUrl('docs/My File.md', { hash: '#x' }),
    'https://github.com/badsadcode/NoorEngine/blob/main/docs/My%20File.md#x');
});

test('small helpers', () => {
  assert.equal(rootPrefix(0), './');
  assert.equal(rootPrefix(2), '../../');
  assert.equal(truncate('short', 10), 'short');
  assert.equal(truncate('one two three four five', 12), 'one two…');
  assert.equal(stripTags('<p>a &amp; <b>b</b></p>\n'), 'a & b');
  assert.equal(escapeHtml('<a href="x">\'&'), '&lt;a href=&quot;x&quot;&gt;&#39;&amp;');
  assert.equal(formatDate('2026-11-15'), '15 November 2026');
  assert.equal(formatDate(null), '');
});
