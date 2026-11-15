// HTML templates for the NoorEngine website. Plain template literals, no framework.
// Every page uses relative URLs (`root`) so the site works under /NoorEngine/ on
// GitHub Pages and from any local folder. The 404 page is the exception: GitHub
// serves it at arbitrary paths, so it uses the absolute site path instead.

import { escapeHtml as e, formatDate, rootPrefix } from './lib.mjs';

const MOTTO = 'Code shall remain understandable to whichever AI inherits this mess.';

const icons = {
  sun: '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><circle cx="12" cy="12" r="4.5" fill="none" stroke="currentColor" stroke-width="2"/><path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M4.9 19.1l1.8-1.8M17.3 6.7l1.8-1.8" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>',
  moon: '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M20.5 14.5A8.5 8.5 0 0 1 9.5 3.5a8.5 8.5 0 1 0 11 11Z" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/></svg>',
  github: '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M12 .5a11.5 11.5 0 0 0-3.64 22.41c.58.1.79-.25.79-.56v-2c-3.2.7-3.88-1.37-3.88-1.37-.53-1.33-1.28-1.69-1.28-1.69-1.05-.71.08-.7.08-.7 1.16.08 1.77 1.19 1.77 1.19 1.03 1.77 2.7 1.26 3.36.96.1-.75.4-1.26.73-1.55-2.55-.29-5.24-1.28-5.24-5.68 0-1.26.45-2.28 1.19-3.09-.12-.29-.52-1.46.11-3.05 0 0 .97-.31 3.17 1.18a11 11 0 0 1 5.77 0c2.2-1.49 3.17-1.18 3.17-1.18.63 1.59.23 2.76.11 3.05.74.81 1.19 1.83 1.19 3.09 0 4.41-2.69 5.39-5.26 5.67.41.36.78 1.06.78 2.14v3.17c0 .31.21.67.8.56A11.5 11.5 0 0 0 12 .5Z"/></svg>',
  feed: '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path fill="currentColor" d="M5 17a2 2 0 1 1 0 4 2 2 0 0 1 0-4Zm-2-7a11 11 0 0 1 11 11h-3a8 8 0 0 0-8-8v-3Zm0-7a18 18 0 0 1 18 18h-3A15 15 0 0 0 3 6V3Z"/></svg>',
};

// Runs before first paint so a stored theme choice never flashes the wrong colours.
const THEME_BOOT = `(function(){try{var t=localStorage.getItem('noor-theme');if(t==='light'||t==='dark')document.documentElement.setAttribute('data-theme',t);}catch(_){}})();`;

function layout({ site, title, description, root, active, body }) {
  const pageTitle = title ? `${title} · NoorEngine` : 'NoorEngine · an honest game engine devlog';
  const nav = [
    ['home', '', 'Home'],
    ['devlog', 'devlog/', 'Devlog'],
    ['decisions', 'decisions/', 'Decisions'],
  ].map(([key, href, label]) =>
    `<a href="${root}${href}"${active === key ? ' aria-current="page"' : ''}>${label}</a>`).join('');
  const shortSha = site.sha ? site.sha.slice(0, 7) : null;
  const built = shortSha
    ? `Built from <a href="${site.repoUrl}/commit/${e(site.sha)}"><code>${e(shortSha)}</code></a>.`
    : '';
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${e(pageTitle)}</title>
<meta name="description" content="${e(description)}">
<meta name="color-scheme" content="dark light">
<meta name="theme-color" content="#0d0b16" media="(prefers-color-scheme: dark)">
<meta name="theme-color" content="#faf9fd" media="(prefers-color-scheme: light)">
<meta property="og:title" content="${e(pageTitle)}">
<meta property="og:description" content="${e(description)}">
<meta property="og:type" content="website">
<meta property="og:image" content="${e(site.siteUrl)}assets/img/icon-256.png">
<link rel="icon" type="image/png" sizes="64x64" href="${root}assets/img/icon-64.png">
<link rel="apple-touch-icon" href="${root}assets/img/apple-touch-icon.png">
<link rel="alternate" type="application/atom+xml" title="NoorEngine devlog" href="${root}feed.xml">
<link rel="stylesheet" href="${root}assets/style.css">
<script>${THEME_BOOT}</script>
<script src="${root}assets/theme.js" defer></script>
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="site-header">
  <div class="wrap header-inner">
    <a class="brand" href="${root}">
      <img src="${root}assets/img/icon-64.png" alt="" width="32" height="32">
      <span>Noor<span class="brand-accent">Engine</span></span>
    </a>
    <nav class="site-nav" aria-label="Main">${nav}</nav>
    <div class="header-tools">
      <a class="icon-btn" href="${site.repoUrl}" aria-label="NoorEngine on GitHub" title="GitHub">${icons.github}</a>
      <button class="icon-btn theme-toggle" type="button" aria-label="Toggle light or dark theme" title="Toggle theme">
        <span class="when-dark">${icons.sun}</span><span class="when-light">${icons.moon}</span>
      </button>
    </div>
  </div>
</header>
<main id="main">
${body}
</main>
<footer class="site-footer">
  <div class="wrap footer-inner">
    <p class="motto">“${e(MOTTO)}”</p>
    <p class="motto-credit">Bad Ideas Works · studio motto #001</p>
    <p class="fine">
      NoorEngine code is <a href="${site.repoUrl}/blob/main/LICENSE">MIT licensed</a>.
      Concept art and logos are not covered by the MIT license.
      ${built}
      <a href="${root}feed.xml">${icons.feed} Devlog feed</a>
    </p>
  </div>
</footer>
</body>
</html>
`;
}

function devlogCard(d, root) {
  return `<article class="card devlog-card">
  <p class="card-meta"><span class="pill">#${String(d.number).padStart(3, '0')}</span>${d.date ? `<time datetime="${d.date}">${formatDate(d.date)}</time>` : '<span>Undated</span>'}</p>
  <h3><a href="${root}devlog/${d.slug}/">${e(d.shortTitle)}</a></h3>
  ${d.excerpt ? `<p>${e(d.excerpt)}</p>` : ''}
  <a class="read-more" href="${root}devlog/${d.slug}/" aria-label="Read devlog #${d.number}: ${e(d.shortTitle)}">Read entry →</a>
</article>`;
}

export function homePage({ site, statusHtml, milestone, latest }) {
  const root = rootPrefix(0);
  const body = `
<section class="hero">
  <div class="wrap hero-inner">
    <div class="hero-text">
      ${milestone ? `<p class="eyebrow"><span class="dot" aria-hidden="true"></span>${e(milestone)}</p>` : ''}
      <h1>Noor<span class="grad">Engine</span></h1>
      <p class="lede">An independent, open-source (MIT) game engine, editor and toolset from
        <strong>Bad Ideas Works</strong>. Built slowly, on purpose, with tests, and with a devlog
        that is funny but never fake.</p>
      <blockquote class="hero-quote">We are building our own engine. We have excellent reasons. They remain classified.</blockquote>
      <div class="cta">
        <a class="btn btn-primary" href="${root}devlog/">Read the devlog</a>
        <a class="btn" href="${site.repoUrl}">${icons.github} View on GitHub</a>
      </div>
    </div>
    <div class="hero-art">
      <img src="${root}assets/img/icon-256.png" alt="NoorEngine logo: a purple hexagon with an N and a cyan orbit" width="256" height="256">
    </div>
  </div>
</section>

<section class="wrap section" id="status">
  <div class="section-head">
    <h2>Honest status</h2>
    <p class="section-sub">Pulled straight from the repository <a href="${site.repoUrl}#honest-status">README</a> at build time, so it cannot get ahead of the code.</p>
  </div>
  <div class="card prose status">${statusHtml}</div>
</section>

<section class="wrap section" id="latest">
  <div class="section-head">
    <h2>Latest from the devlog</h2>
    <p class="section-sub"><a href="${root}devlog/">All entries →</a></p>
  </div>
  <div class="card-grid">${latest.map((d) => devlogCard(d, root)).join('\n')}</div>
</section>

<section class="wrap section" id="concept">
  <div class="section-head">
    <h2>Where this is heading</h2>
    <p class="section-sub">A picture of the goal, clearly labelled as a picture of the goal.</p>
  </div>
  <figure class="concept">
    <div class="concept-frame">
      <span class="concept-badge">Concept art</span>
      <img src="${root}assets/img/concept-editor-clean.jpg" width="1400" height="788" loading="lazy"
        alt="CONCEPT ART: a mockup of a dark, purple-accented editor with a forest scene in the viewport. Not a screenshot.">
    </div>
    <figcaption><strong>CONCEPT ART: NoorEditor mockup.</strong> Not a screenshot of working software.
      It will be replaced by real screenshots once the editor exists.</figcaption>
  </figure>
</section>

<section class="wrap section" id="studio">
  <div class="section-head">
    <h2>Who is building this</h2>
  </div>
  <div class="card-grid three">
    <div class="card person"><h3>Tom</h3><p>Founder, creative director, game designer and enthusiastic inventor of wonderfully questionable ideas.</p></div>
    <div class="card person"><h3>Noor <span class="tag">AI · ChatGPT</span></h3><p>Concepts, specs, artwork and brainstorming. Suggests features that make the engineering considerably harder.</p></div>
    <div class="card person"><h3>Sage <span class="tag">AI · Claude</span></h3><p>Engineering, architecture and tests. Turns ridiculous ambitions into software, then writes down what broke.</p></div>
  </div>
  <p class="more-links">Why things are built the way they are: <a href="${root}decisions/">Architecture Decision Records</a>.</p>
</section>
`;
  return layout({
    site, root, active: 'home', title: '',
    description: 'NoorEngine: an independent, open-source game engine by Bad Ideas Works. Honest status and devlog.',
    body,
  });
}

export function devlogIndexPage({ site, devlogs }) {
  const root = rootPrefix(1);
  const body = `
<section class="wrap section page-head">
  <p class="eyebrow">Devlog</p>
  <h1>The NoorEngine devlog</h1>
  <p class="lede">What we actually did, what broke, and what we have not done yet. Funny, never fake.
    Newest first. <a href="${root}feed.xml">${icons.feed} Subscribe</a></p>
</section>
<section class="wrap section">
  <div class="card-grid">${devlogs.map((d) => devlogCard(d, root)).join('\n')}</div>
</section>
`;
  return layout({
    site, root, active: 'devlog', title: 'Devlog',
    description: 'The NoorEngine devlog: honest engineering progress from Bad Ideas Works.',
    body,
  });
}

export function devlogEntryPage({ site, entry, facts = [], html, newer, older }) {
  const root = rootPrefix(2);
  const num = String(entry.number).padStart(3, '0');
  const link = (d, rel, label) => d
    ? `<a class="pager-link ${rel}" href="${root}devlog/${d.slug}/" rel="${rel}"><span>${label}</span>#${String(d.number).padStart(3, '0')} · ${e(d.shortTitle)}</a>`
    : '<span></span>';
  const body = `
<article class="wrap entry">
  <header class="entry-head">
    <a class="back" href="${root}devlog/">← All devlog entries</a>
    <p class="eyebrow">Devlog #${num}${entry.date ? ` · <time datetime="${entry.date}">${formatDate(entry.date)}</time>` : ''}</p>
    <h1>${e(entry.shortTitle)}</h1>
    ${facts.length ? `<dl class="facts">${facts.map((f) => `<div><dt>${e(f.key)}</dt><dd>${f.valueHtml}</dd></div>`).join('')}</dl>` : ''}
  </header>
  <div class="prose">${html}</div>
  <p class="source-link"><a href="${site.repoUrl}/blob/main/${entry.sourcePath}">View this entry's Markdown on GitHub</a></p>
  <nav class="pager" aria-label="More devlog entries">${link(older, 'prev', '← Older')}${link(newer, 'next', 'Newer →')}</nav>
</article>
`;
  return layout({
    site, root, active: 'devlog', title: `Devlog #${num}: ${entry.shortTitle}`,
    description: entry.excerpt || entry.title,
    body,
  });
}

export function decisionsPage({ site, html }) {
  const root = rootPrefix(1);
  const body = `
<section class="wrap section page-head">
  <p class="eyebrow">Architecture</p>
  <h1>Architecture Decision Records</h1>
  <p class="lede">What has been decided, what is still open, and why. Each ADR lives in the repository;
    the links below open it on GitHub. Read these before arguing. 😉</p>
</section>
<section class="wrap section">
  <div class="card prose">${html}</div>
  <p class="more-links"><a href="${site.repoUrl}/tree/main/docs/decisions">Browse docs/decisions on GitHub →</a></p>
</section>
`;
  return layout({
    site, root, active: 'decisions', title: 'Decisions',
    description: 'NoorEngine Architecture Decision Records: what was decided and why.',
    body,
  });
}

export function notFoundPage({ site }) {
  const root = new URL(site.siteUrl).pathname; // absolute: 404 is served at any path
  const body = `
<section class="wrap section page-head not-found">
  <p class="eyebrow">404</p>
  <h1>This page has not been implemented yet</h1>
  <p class="lede">Much like most of the engine. We would rather say so than fake it.</p>
  <div class="cta"><a class="btn btn-primary" href="${root}">Go home</a><a class="btn" href="${root}devlog/">Read the devlog</a></div>
</section>
`;
  return layout({ site, root, active: '', title: 'Not found', description: 'Page not found.', body });
}

export function atomFeed({ site, devlogs }) {
  const updated = devlogs.map((d) => d.date).filter(Boolean).sort().at(-1) ?? '1970-01-01';
  const items = devlogs.map((d) => {
    const url = `${site.siteUrl}devlog/${d.slug}/`;
    return `  <entry>
    <title>${e(d.title)}</title>
    <link href="${e(url)}"/>
    <id>${e(url)}</id>
    <updated>${d.date ?? updated}T00:00:00Z</updated>
    <summary>${e(d.excerpt)}</summary>
  </entry>`;
  }).join('\n');
  return `<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>NoorEngine devlog</title>
  <subtitle>Honest engineering progress from Bad Ideas Works.</subtitle>
  <link href="${e(site.siteUrl)}"/>
  <link rel="self" href="${e(site.siteUrl)}feed.xml"/>
  <id>${e(site.siteUrl)}</id>
  <updated>${updated}T00:00:00Z</updated>
  <author><name>Bad Ideas Works</name></author>
${items}
</feed>
`;
}
