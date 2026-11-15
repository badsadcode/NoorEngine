// Tiny local preview server for website/dist (no dependencies).
// Serves the site under the same path as GitHub Pages (/NoorEngine/) so relative
// links and the 404 page behave like production.
//
// Usage: npm run build && npm run serve   ->  http://localhost:4173/NoorEngine/

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const DIST = path.join(path.dirname(fileURLToPath(import.meta.url)), 'dist');
const BASE = '/NoorEngine/';
const PORT = Number(process.env.PORT) || 4173;
const TYPES = {
  '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml', '.xml': 'application/atom+xml; charset=utf-8',
};

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (!url.pathname.startsWith(BASE)) {
    res.writeHead(302, { Location: BASE }).end();
    return;
  }
  let rel = decodeURIComponent(url.pathname.slice(BASE.length));
  if (rel === '' || rel.endsWith('/')) rel += 'index.html';
  const file = path.join(DIST, rel);
  if (!file.startsWith(DIST)) { res.writeHead(400).end(); return; }
  if (fs.existsSync(file) && fs.statSync(file).isDirectory()) {
    res.writeHead(301, { Location: `${url.pathname}/` }).end(); // like GitHub Pages
    return;
  }
  fs.readFile(file, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': TYPES['.html'] });
      res.end(fs.existsSync(path.join(DIST, '404.html')) ? fs.readFileSync(path.join(DIST, '404.html')) : 'Not found');
      return;
    }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
    res.end(data);
  });
}).listen(PORT, () => console.log(`Preview: http://localhost:${PORT}${BASE}`));
