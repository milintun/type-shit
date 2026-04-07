# Impl 1 — Parametric Font Warping

## Approach

Claude receives the word manifest and returns numeric warp parameters per word. A warp engine built on `opentype.js` applies those parameters to bezier control points of a base font, generating a unique TTF blob per word. Each blob is registered as a `FontFace` and rendered into the layout slot.

No image generation. No raster output. Pure vector — the poster is zoomable at any resolution.

---

## Dependencies

```json
{
  "opentype.js": "^1.3.4",
  "canvas": "^2.11.2"
}
```

---

## Step 1 — Claude Warp Parameter Generation

One batched call for all words. Claude acts as creative director, translating audio + semantic profile into warp parameter space.

### Warp parameter schema

```js
{
  wobble: 0.0–1.0,      // sinusoidal noise on control points — trembling, uncertain
  swell: 0.0–1.0,       // outward handle scaling — fat, bulbous strokes
  spike: 0.0–1.0,       // radial point pulling — serifs become thorns
  shatter: 0.0–1.0,     // random point displacement — fragmentation, chaos
  stretch: 0.0–1.0,     // vertical elongation
  crush: 0.0–1.0,       // vertical compression (use one or the other, not both)
  skew: -1.0–1.0,       // horizontal shear
  rotationVariance: 0.0–1.0,  // per-glyph random tilt amount
  color: "#rrggbb",
  shadowBlur: 0–30,
  opacity: 0.0–1.0
}
```

### Prompt

```js
const prompt = `You are a creative director for an expressive typography system. 
Each word in a spoken recording will be rendered in a unique warped font.
Your job is to choose warp parameters that visually express how each word was spoken.

Parameter guide:
- wobble (0-1): sinusoidal noise on letterform curves. High = trembling, uncertain, afraid
- swell (0-1): outward stroke scaling. High = fat, aggressive, loud
- spike (0-1): radial point pulling on terminals. High = sharp, angry, stabbing
- shatter (0-1): random point displacement. High = chaotic, fragmented, breaking apart
- stretch (0-1): vertical elongation. High = reaching, excited, high pitch
- crush (0-1): vertical compression. High = heavy, oppressed, low and slow
- skew (-1 to 1): shear. Positive = forward lean, negative = backward
- rotationVariance (0-1): random per-glyph tilt. High = unstable, drunk, confused
- color: hex color. Angry = deep reds/oranges. Sad = desaturated blues. Excited = bright warm. Calm = cool neutrals. Filler words = #444444
- shadowBlur (0-30): glow/bleed. High = loud, urgent, reverberating
- opacity (0-1): presence. Fillers and quiet words should be lower

Rules:
- angry + high volume: high spike (0.7+), high swell (0.6+), deep red color, low wobble
- sad + low volume: high wobble (0.6+), low swell, desaturated blue, high opacity
- excited + high pitch: high stretch, moderate spike, bright warm color
- uncertain: high wobble, high rotationVariance, low opacity
- isFiller: true → all params low (max 0.2), color #555, opacity 0.35
- calm + neutral: all params low-moderate (0.1–0.3), cool neutral color
- shatter should only go above 0.4 for genuinely extreme moments (yelling, breaking down)

Here is the word manifest:
${JSON.stringify(manifest, null, 2)}

Return ONLY a JSON array, one entry per word, same order:
[{ "word": "...", "params": { wobble, swell, spike, shatter, stretch, crush, skew, rotationVariance, color, shadowBlur, opacity } }]`;
```

---

## Step 2 — Base Font

Use a high-contrast serif with a wide weight range. The thin/thick stroke contrast means:
- `swell` on thick strokes → grotesque bloat
- `spike` on sharp terminals → thorns
- `wobble` on thin hairlines → trembling

**Recommended:** Download Playfair Display or IM Fell English as the base. Both are open license and have dramatic contrast.

Load once at startup:
```js
import opentype from 'opentype.js';
const baseFont = await opentype.load('./fonts/PlayfairDisplay-Regular.ttf');
```

---

## Step 3 — Warp Engine

Applies warp parameters to glyph bezier control points.

```js
function warpGlyph(glyph, params, fontSize) {
  const path = glyph.getPath(0, 0, fontSize);
  const commands = path.commands;
  const cx = glyph.advanceWidth * fontSize / baseFont.unitsPerEm / 2;
  const cy = -fontSize / 2;

  return commands.map(cmd => {
    if (cmd.type === 'Z' || cmd.type === 'M') return cmd;

    const warpPoint = (x, y) => {
      let nx = x, ny = y;

      // wobble — sinusoidal noise
      if (params.wobble > 0) {
        nx += Math.sin(y * 0.08 + Math.random() * 0.5) * params.wobble * 8;
        ny += Math.cos(x * 0.08 + Math.random() * 0.5) * params.wobble * 8;
      }

      // swell — push outward from glyph center
      if (params.swell > 0) {
        const dx = nx - cx, dy = ny - cy;
        const dist = Math.sqrt(dx * dx + dy * dy);
        const factor = 1 + (params.swell * 0.4 * (dist / (fontSize * 0.6)));
        nx = cx + dx * factor;
        ny = cy + dy * factor;
      }

      // spike — radial pull on points far from center
      if (params.spike > 0) {
        const dx = nx - cx, dy = ny - cy;
        const dist = Math.sqrt(dx * dx + dy * dy);
        if (dist > fontSize * 0.3) {
          const pull = params.spike * 0.5 * (dist / fontSize);
          nx += (dx / dist) * pull * fontSize * 0.15;
          ny += (dy / dist) * pull * fontSize * 0.15;
        }
      }

      // shatter — random displacement
      if (params.shatter > 0) {
        nx += (Math.random() - 0.5) * params.shatter * fontSize * 0.2;
        ny += (Math.random() - 0.5) * params.shatter * fontSize * 0.2;
      }

      // stretch / crush
      if (params.stretch > 0) ny *= (1 + params.stretch * 0.6);
      if (params.crush > 0) ny *= (1 - params.crush * 0.4);

      // skew
      if (params.skew !== 0) nx += ny * params.skew * 0.3;

      return [nx, ny];
    };

    const warped = { ...cmd };
    if (cmd.x !== undefined) [warped.x, warped.y] = warpPoint(cmd.x, cmd.y);
    if (cmd.x1 !== undefined) [warped.x1, warped.y1] = warpPoint(cmd.x1, cmd.y1);
    if (cmd.x2 !== undefined) [warped.x2, warped.y2] = warpPoint(cmd.x2, cmd.y2);
    return warped;
  });
}
```

### Building a word from warped glyphs

```js
async function buildWarpedWord(word, params, fontSize) {
  const glyphs = baseFont.stringToGlyphs(word);
  let xOffset = 0;
  const allCommands = [];

  glyphs.forEach(glyph => {
    // per-glyph rotation variance
    const tilt = (Math.random() - 0.5) * params.rotationVariance * 0.25;

    const warped = warpGlyph(glyph, params, fontSize);
    // translate to xOffset, apply tilt rotation
    warped.forEach(cmd => {
      // rotate cmd around glyph center, then translate by xOffset
      allCommands.push(rotateAndTranslate(cmd, tilt, xOffset, 0));
    });

    xOffset += glyph.advanceWidth * fontSize / baseFont.unitsPerEm * (1 + params.swell * 0.2);
  });

  // build a minimal TTF from these paths using opentype.js Glyph construction
  // return as ArrayBuffer → Blob → URL for FontFace
  return buildFontBlob(word, allCommands, fontSize);
}
```

---

## Step 4 — Font Registration and Rendering

```js
async function renderWordToCanvas(ctx, layoutWord) {
  const { word, layout, warpParams } = layoutWord;

  const blob = await buildWarpedWord(word.word, warpParams, layout.fontSize);
  const fontUrl = URL.createObjectURL(blob);
  const fontFamily = `voice-${word.word}-${Date.now()}`;

  const face = new FontFace(fontFamily, `url(${fontUrl})`);
  await face.load();
  document.fonts.add(face);

  ctx.save();
  ctx.globalAlpha = layout.opacity * warpParams.opacity;
  ctx.font = `${layout.fontSize}px "${fontFamily}"`;
  ctx.fillStyle = warpParams.color;

  if (warpParams.shadowBlur > 0) {
    ctx.shadowColor = warpParams.color;
    ctx.shadowBlur = warpParams.shadowBlur;
  }

  ctx.fillText(word.word, layout.x, layout.y);
  ctx.restore();

  URL.revokeObjectURL(fontUrl);
}
```

---

## Full Render Pipeline

```js
async function renderImpl1(layoutManifest) {
  // 1. get warp params from Claude (one call)
  const warpParams = await getWarpParams(layoutManifest);

  // 2. merge params into manifest
  const withParams = layoutManifest.map((w, i) => ({ ...w, warpParams: warpParams[i].params }));

  // 3. render all words (can parallelize font building)
  const canvas = document.createElement('canvas');
  canvas.width = 1200;
  canvas.height = 800;
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = '#0f0f0f';
  ctx.fillRect(0, 0, 1200, 800);

  // build all font blobs in parallel, then render sequentially
  const blobs = await Promise.all(withParams.map(w => buildWarpedWord(w.word.word, w.warpParams, w.layout.fontSize)));

  for (let i = 0; i < withParams.length; i++) {
    await renderWordToCanvas(ctx, withParams[i], blobs[i]);
  }

  return canvas;
}
```

---

## Tuning Notes

- `shatter` above 0.5 will likely break legibility completely. Reserve for the most extreme moments.
- `wobble` uses `Math.random()` — the same params will produce different results each render. Seed the RNG if you want reproducible output.
- The warp is applied to path commands, not to a rasterized image — letters can overlap, spikes can extend far beyond the bounding box. This is a feature.
- If a word looks too legible, increase `rotationVariance` first — it's the cheapest chaos.
- Background should be near-black. Colored glows and warm reds need dark surfaces.
