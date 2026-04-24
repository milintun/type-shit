import http from 'http';
import fs   from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { execFile }      from 'child_process';
import os                 from 'os';

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

// ── Print endpoint helper ────────────────────────────────────────────────────
const PRINT_SCRIPT = path.join(__dirname, 'firmware', 'print_bitmap.py');

function handlePrint(req, res) {
  let body = '';
  req.on('data', chunk => { body += chunk; });
  req.on('end', () => {
    try {
      const { imageBase64 } = JSON.parse(body);
      if (!imageBase64) { res.writeHead(400); res.end('Missing imageBase64'); return; }

      const tmpFile = path.join(os.tmpdir(), `print-${Date.now()}.png`);
      fs.writeFileSync(tmpFile, Buffer.from(imageBase64, 'base64'));

      execFile('python3', [PRINT_SCRIPT, tmpFile], { timeout: 60000 }, (err, stdout, stderr) => {
        fs.unlink(tmpFile, () => {});
        if (err) {
          console.error('Print error:', stderr || err.message);
          res.writeHead(500, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: stderr || err.message }));
        } else {
          console.log('Print OK:', stdout.trim());
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: true }));
        }
      });
    } catch (e) {
      res.writeHead(400); res.end('Invalid JSON');
    }
  });
}

// ── Server ────────────────────────────────────────────────────────────────────
http.createServer((req, res) => {
  // Print endpoint
  if (req.url === '/print' && req.method === 'POST') {
    res.setHeader('Access-Control-Allow-Origin', '*');
    return handlePrint(req, res);
  }
  // CORS preflight for /print
  if (req.url === '/print' && req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'POST',
      'Access-Control-Allow-Headers': 'Content-Type',
    });
    return res.end();
  }

  // Intercept /config.js — serve keys from .env, never from disk
  if (req.url === '/config.js') {
    res.writeHead(200, { 'Content-Type': MIME['.js'] });
    res.end(
`export const OPENAI_KEY    = '${env.OPENAI_API_KEY    || ''}';
export const ANTHROPIC_KEY = '${env.ANTHROPIC_API_KEY || ''}';
export const GEMINI_KEY    = '${env.GEMINI_API_KEY    || ''}';`
    );
    return;
  }

  // Static file serving
  const urlPath  = req.url.split('?')[0];
  const filePath = path.join(__dirname, urlPath === '/' ? '/index-v3.html' : urlPath);

  fs.readFile(filePath, (err, data) => {
    if (err) { res.writeHead(404); res.end('Not found'); return; }
    const ext = path.extname(filePath);
    res.writeHead(200, {
      'Content-Type': MIME[ext] || 'application/octet-stream',
      'Access-Control-Allow-Origin': '*',
    });
    res.end(data);
  });

}).listen(3000, () => {
  console.log('v2  http://localhost:3000/index-v2.html');
  console.log('v3  http://localhost:3000/index-v3.html');
  console.log('v4  http://localhost:3000/index-v4.html  (arduino serial)');
  console.log('v4  http://localhost:3000/index-v5.html  (+printer serial)');
});
