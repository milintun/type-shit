import http from 'http';
import fs   from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// ── Parse .env ────────────────────────────────────────────────────────────────
const env = {};
try {
  fs.readFileSync(path.join(__dirname, '.env'), 'utf-8')
    .split('\n')
    .forEach(line => {
      const eq = line.indexOf('=');
      if (eq > 0) env[line.slice(0, eq).trim()] = line.slice(eq + 1).trim();
    });
} catch { /* .env optional */ }

// ── MIME types ────────────────────────────────────────────────────────────────
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.ttf':  'font/truetype',
  '.woff': 'font/woff',
  '.woff2':'font/woff2',
  '.png':  'image/png',
  '.json': 'application/json',
};

// ── Server ────────────────────────────────────────────────────────────────────
http.createServer((req, res) => {
  // Intercept /config.js — serve keys from .env, never from disk
  if (req.url === '/config.js') {
    res.writeHead(200, { 'Content-Type': MIME['.js'] });
    res.end(
`export const OPENAI_KEY    = '${env.OPENAI_API_KEY    || ''}';
export const ANTHROPIC_KEY = '${env.ANTHROPIC_API_KEY || ''}';`
    );
    return;
  }

  // Static file serving
  const urlPath  = req.url.split('?')[0];
  const filePath = path.join(__dirname, urlPath === '/' ? '/index-v2.html' : urlPath);

  fs.readFile(filePath, (err, data) => {
    if (err) { res.writeHead(404); res.end('Not found'); return; }
    const ext = path.extname(filePath);
    res.writeHead(200, {
      'Content-Type': MIME[ext] || 'application/octet-stream',
      'Access-Control-Allow-Origin': '*',
    });
    res.end(data);
  });

}).listen(3000, () => console.log('http://localhost:3000/index-v2.html'));
