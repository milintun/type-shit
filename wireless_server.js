import http from 'http';
import fs   from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import os from 'os';

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

const XIAO_IP   = env.XIAO_IP || 'xiao-printer.local';
const XIAO_URL  = `http://${XIAO_IP}`;

console.log(`XIAO target: ${XIAO_URL}`);

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

// ── Helper: fetch from XIAO ─────────────────────────────────────────────────
async function xiaoFetch(urlPath, options = {}) {
  const url = `${XIAO_URL}${urlPath}`;
  const res = await fetch(url, { ...options, signal: AbortSignal.timeout(5000) });
  return res;
}

// ── Button state endpoint (polls XIAO) ──────────────────────────────────────
async function handleButtonState(req, res) {
  try {
    const xiaoRes = await xiaoFetch('/button');
    const data = await xiaoRes.text();
    res.writeHead(200, {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*',
    });
    res.end(data);
  } catch (err) {
    res.writeHead(502, {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*',
    });
    res.end(JSON.stringify({ error: 'XIAO unreachable', detail: err.message }));
  }
}

// ── Print endpoint (converts image → ESC/POS → POST to XIAO) ───────────────
function handlePrint(req, res) {
  let body = '';
  req.on('data', chunk => { body += chunk; });
  req.on('end', async () => {
    try {
      const { imageBase64 } = JSON.parse(body);
      if (!imageBase64) {
        res.writeHead(400, { 'Access-Control-Allow-Origin': '*' });
        res.end('Missing imageBase64');
        return;
      }

      // Write image to temp file, convert to ESC/POS via Python, then POST to XIAO
      const tmpFile = path.join(os.tmpdir(), `print-${Date.now()}.png`);
      fs.writeFileSync(tmpFile, Buffer.from(imageBase64, 'base64'));

      const printScript = path.join(__dirname, 'firmware', 'wireless_print_bitmap.py');

      const { execFile } = await import('child_process');
      execFile('python3', [printScript, tmpFile, XIAO_URL], { timeout: 120000 }, (err, stdout, stderr) => {
        fs.unlink(tmpFile, () => {});
        if (err) {
          const detail = [stderr, stdout, err.message].filter(Boolean).join('\n').trim();
          console.error('Print error:', detail);
          res.writeHead(500, {
            'Content-Type': 'application/json',
            'Access-Control-Allow-Origin': '*',
          });
          res.end(JSON.stringify({ error: detail }));
        } else {
          console.log('Print OK:', stdout.trim());
          res.writeHead(200, {
            'Content-Type': 'application/json',
            'Access-Control-Allow-Origin': '*',
          });
          res.end(JSON.stringify({ ok: true }));
        }
      });
    } catch (e) {
      res.writeHead(400, { 'Access-Control-Allow-Origin': '*' });
      res.end('Invalid JSON');
    }
  });
}

// ── Server ────────────────────────────────────────────────────────────────────
http.createServer((req, res) => {
  // Button state endpoint
  if (req.url === '/button-state' && req.method === 'GET') {
    return handleButtonState(req, res);
  }

  // LED control endpoint (proxies to XIAO)
  if (req.url.startsWith('/led') && req.method === 'POST') {
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', async () => {
      try {
        await xiaoFetch('/led', { method: 'POST', body });
        res.writeHead(200, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' });
        res.end('{"ok":true}');
      } catch (err) {
        res.writeHead(502, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
    return;
  }

  // Print endpoint
  if (req.url === '/print' && req.method === 'POST') {
    res.setHeader('Access-Control-Allow-Origin', '*');
    return handlePrint(req, res);
  }

  // CORS preflight
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type',
    });
    return res.end();
  }

  // Intercept /config.js — serve API keys from .env
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
  const filePath = path.join(__dirname, urlPath === '/' ? '/index-v6.html' : urlPath);

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
  console.log(`Wireless server running at http://localhost:3000`);
  console.log(`XIAO endpoint: ${XIAO_URL}`);
  console.log('');
  console.log('Endpoints:');
  console.log('  GET  /button-state  — polls XIAO button');
  console.log('  POST /print         — converts image & sends to XIAO printer');
  console.log('  GET  /              — serves index-v6.html');
});
