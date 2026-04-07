# Voice Typography — Font Agent Implementation Handoff

## What We're Building

A system that takes a spoken audio recording (< 10s), processes it into a word manifest, then renders each word in expressive variable font typography that reflects how it was spoken. The output is a static poster-like image.

Both font TTF files are already in the codebase.

---

## The Word Manifest

Every word gets reduced to this object before any font decisions are made:

```js
{
  word: "burning",
  start: 1.24,
  end: 1.71,
  pauseBefore: 0.42,       // seconds of silence before this word
  duration: 0.47,
  volume: 0.82,            // normalized 0–1 (RMS from Web Audio)
  pitch: 0.74,             // normalized 0–1 (autocorrelation, 80–400Hz range)
  speakingRate: 0.65,      // normalized 0–1, fast = high
  tag: "angry",            // Claude semantic tag
  intensity: 0.9,          // 0–1, how strongly the tag applies
  isFiller: false          // um, uh, like, you know, etc.
}
```

---

## The Two Fonts

Both TTFs are in the codebase. Load them with opentype.js.

### Decovar (`DecovarAlpha-VF.ttf`)
Character: chaotic, decorative, extreme display. Can range from full serif to bare skeleton to worm-like forms. Best for emotional extremes.

All axes run 0–1000.

| Tag | Name | Effect |
|-----|------|--------|
| BLDA | Inline A | Adds inline stroke decoration inside letterforms |
| TRMC | Rounded Slab | Rounded slab serif terminals |
| TRMF | Flared | Flared, spiked terminals — becomes thorns at high values |
| TRML | Lined | Lined terminal treatment |
| TRMK | Bifurcated | Split/forked terminals |
| SKLA | Skeleton A | Strips letterform to bare skeleton — wiry, minimal |
| SKLD | Skeleton D | Worm-like skeleton variant |
| BLDB | Inline B | Second inline decoration variant |
| WMX2 | Weight | Stroke weight (Decovar-specific, not standard wght) |

Key combinations:
- `TRMF: 800, WMX2: 600` → aggressive spiked serifs, angry
- `SKLA: 700, WMX2: 200` → skeletal, ghostly, barely there
- `TRMC: 600, WMX2: 400` → rounded slab, friendly/heavy
- `TRMK: 500, BLDA: 300` → bifurcated + inline, ornate/filler
- All axes 0 → clean geometric sans

### Amstelvar (`AmstelvarAlpha-VF.ttf`)
Character: precise, parametric, always serif. Best for proportional expression — height, width, weight, stroke contrast all independently controllable. Good for calm, formal, whispered, compressed, or tall/narrow emotional states.

| Tag | Name | Range | Effect |
|-----|------|-------|--------|
| wght | Weight | 100–900 | Overall stroke weight |
| wdth | Width | 75–125 | Horizontal compression/expansion |
| opsz | Optical size | 8–144 | Optimized for display vs text |
| XHGT | X-height | 0–1000 | Height of lowercase letters |
| XOPQ | X opaque | 0–1000 | Thick stroke width (horizontal) |
| XTRA | X transparent | 0–1000 | Counter width — open vs tight |
| YOPQ | Y opaque | 0–1000 | Thin stroke width (vertical) |
| YTUC | UC height | 0–1000 | Uppercase letter height |
| YTLC | LC height | 0–1000 | Lowercase letter height |
| YTDE | Descender | -1000–0 | Depth of descenders |
| YTAS | Ascender | 0–1000 | Height of ascenders |

Key combinations:
- `wght: 200, XOPQ: 100, YOPQ: 50` → whisper-thin, delicate
- `wght: 800, wdth: 75, XTRA: 200` → heavy compressed, emphatic
- `YTUC: 800, YTAS: 900, wdth: 75` → tall narrow, high pitch
- `wdth: 120, YTLC: 300, wght: 600` → wide and low, slow/heavy
- `XTRA: 800, XOPQ: 200` → very open counters, airy/calm

---

## The Font Agent

This is the core of the system. One batched Claude API call for the entire word manifest. Returns font selection + axis values per word.

### Architecture: two-stage reasoning in one prompt

Ask Claude to reason about font choice first (Decovar vs Amstelvar vs blend), then set axis values. Chain-of-thought before the JSON output produces significantly better results.

### The prompt

```js
const FONT_AGENT_PROMPT = `You are a typography agent for an expressive voice visualization system.
Each word in a spoken recording will be rendered in a variable font whose axes you control.
Your job is to choose the right font and set its axes to visually express how each word was spoken.

## Fonts available

**Decovar** — chaotic, decorative, extreme. Use for emotional extremes, anger, chaos, filler ornaments.
Axes (all 0–1000): BLDA (inline decoration), TRMC (rounded slab), TRMF (flared/spike terminals), 
TRML (lined terminals), TRMK (bifurcated/forked terminals), SKLA (skeleton — strips to bare wiry form), 
SKLD (worm skeleton variant), BLDB (inline variant), WMX2 (weight)

**Amstelvar** — precise, parametric, always serif. Use for calm, formal, whispered, compressed, tall/narrow.
Axes: wght 100–900, wdth 75–125, opsz 8–144, XHGT 0–1000 (x-height), XOPQ 0–1000 (thick stroke),
XTRA 0–1000 (counter width), YOPQ 0–1000 (thin stroke), YTUC 0–1000 (uppercase height),
YTLC 0–1000 (lowercase height), YTDE -1000–0 (descender), YTAS 0–1000 (ascender)

## Selection rules

- angry / loud / high pitch → Decovar, TRMF high, WMX2 high
- sad / low volume → Amstelvar, wght low, XOPQ low, YOPQ low
- excited / fast / high pitch → Decovar, SKLA mid, or Amstelvar YTUC high + wdth narrow
- calm / neutral → Amstelvar, balanced axes
- uncertain / trembling → Amstelvar wght low + XTRA high, or Decovar SKLA high
- emphatic / punchy → Amstelvar wght 800 + wdth 75, or Decovar TRMF mid + WMX2 high
- tender / quiet → Amstelvar wght 200 + XOPQ low
- isFiller: true → Decovar, one subtle axis only (TRMK: 300 or BLDA: 200), WMX2: 100
- blend: for in-between states, return both fonts with a blendRatio 0–1

## Volume and pitch modifiers (apply on top of tag)
- volume > 0.7: increase weight axes, increase spike/swell effects
- volume < 0.3: decrease weight, increase transparency/skeleton
- pitch > 0.7: increase vertical axes (YTUC, YTAS), decrease width
- pitch < 0.3: increase horizontal axes (wdth, XTRA), decrease height axes
- pauseBefore > 0.5: the word arrives after silence — make it more deliberate, heavier

## Output format

Think through your font choice first, then output JSON.
Return ONLY a JSON array, one entry per word, same order as input. No preamble, no trailing text.

[
  {
    "word": "burning",
    "reasoning": "angry tag, high volume, high pitch — Decovar with spike terminals and weight",
    "font": "decovar",          // "decovar" | "amstelvar" | "blend"
    "blendRatio": null,         // 0–1 if font is "blend" (0 = full decovar, 1 = full amstelvar)
    "axes": {
      "TRMF": 750,
      "WMX2": 650,
      "SKLA": 0
    },
    "color": "#E8593C",
    "opacity": 1.0,
    "shadowBlur": 14
  }
]`;
```

### Calling the agent

```js
async function runFontAgent(manifest) {
  const response = await anthropic.messages.create({
    model: 'claude-sonnet-4-20250514',
    max_tokens: 4000,
    messages: [{
      role: 'user',
      content: `${FONT_AGENT_PROMPT}\n\nWord manifest:\n${JSON.stringify(manifest, null, 2)}`
    }]
  });

  const text = response.content[0].text;
  const clean = text.replace(/```json|```/g, '').trim();
  return JSON.parse(clean);
}
```

---

## Rendering Pipeline

### Step 1 — Load fonts once at startup

```js
import opentype from 'opentype.js';

const fonts = {
  decovar: await opentype.load('./fonts/DecovarAlpha-VF.ttf'),
  amstelvar: await opentype.load('./fonts/AmstelvarAlpha-VF.ttf'),
};
```

### Step 2 — Apply axes and render word to canvas

Variable font axes in opentype.js are set via the `variation` option on `getPath`:

```js
function renderWord(ctx, word, x, y, fontSize, agentOutput) {
  const font = fonts[agentOutput.font];

  ctx.save();
  ctx.globalAlpha = agentOutput.opacity;

  if (agentOutput.shadowBlur > 0) {
    ctx.shadowColor = agentOutput.color;
    ctx.shadowBlur = agentOutput.shadowBlur;
  }

  // opentype.js variable font rendering
  const path = font.getPath(word, x, y, fontSize, {
    variation: agentOutput.axes  // { TRMF: 750, WMX2: 650, ... }
  });

  path.fill = agentOutput.color;
  path.draw(ctx);

  ctx.restore();
}
```

### Step 3 — Blend case (SDF interpolation)

When `font === 'blend'`, render both fonts to offscreen canvases, convert to SDF, interpolate, extract contour:

```js
async function renderBlendedWord(ctx, word, x, y, fontSize, agentOutput) {
  const { blendRatio, axes } = agentOutput;
  // blendRatio: 0 = full decovar, 1 = full amstelvar

  // render each to offscreen canvas
  const offA = renderToOffscreen(word, fontSize, fonts.decovar, axes.decovar);
  const offB = renderToOffscreen(word, fontSize, fonts.amstelvar, axes.amstelvar);

  // convert to SDF, blend, extract
  const sdfA = canvasToSDF(offA);
  const sdfB = canvasToSDF(offB);
  const blended = blendSDF(sdfA, sdfB, blendRatio);
  const contour = marchingSquares(blended, 0);

  // draw contour path
  drawContour(ctx, contour, x, y, agentOutput.color);
}
```

SDF blend is a stretch goal — implement after single-font rendering is working.

---

## Layout Engine

Positions words on the poster canvas before rendering.

```js
function layoutWords(manifest, canvasWidth = 1200, canvasHeight = 800) {
  const padding = 80;
  let x = padding;
  let y = padding + 96;

  return manifest.map((word) => {
    const fontSize = word.isFiller ? 10 : lerp(16, 96, word.volume);
    const extraMargin = word.pauseBefore > 0.3
      ? lerp(0, 80, Math.min((word.pauseBefore - 0.3) / 0.7, 1))
      : 0;

    const estimatedWidth = word.word.length * fontSize * 0.55;

    if (word.pauseBefore > 0.8 || x + estimatedWidth > canvasWidth - padding) {
      x = padding;
      y += fontSize * 1.6 + 16;
    }

    x += extraMargin;
    const slot = { x, y, fontSize };
    x += estimatedWidth + fontSize * 0.3;

    return { ...word, layout: slot };
  });
}

const lerp = (a, b, t) => a + (b - a) * Math.max(0, Math.min(1, t));
```

---

## Full Render Flow

```js
async function renderPoster(audioFile) {
  // 1. transcribe + get word timestamps
  const words = await transcribeWithWhisper(audioFile);

  // 2. extract audio features per word
  const withFeatures = await extractAudioFeatures(audioFile, words);

  // 3. semantic tagging (separate Claude call or combined)
  const withTags = await tagSemantics(withFeatures);

  // 4. compute derived fields
  const manifest = buildManifest(withTags);

  // 5. layout
  const layoutManifest = layoutWords(manifest);

  // 6. font agent — one call for all words
  const agentOutputs = await runFontAgent(layoutManifest);

  // 7. render
  const canvas = createCanvas(1200, 800);
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#0f0f0f';
  ctx.fillRect(0, 0, 1200, 800);

  for (let i = 0; i < layoutManifest.length; i++) {
    const word = layoutManifest[i];
    const agent = agentOutputs[i];
    renderWord(ctx, word.word, word.layout.x, word.layout.y, word.layout.fontSize, agent);
  }

  return canvas;
}
```

---

## File Structure

```
/
├── fonts/
│   ├── DecovarAlpha-VF.ttf       ← already in codebase
│   └── AmstelvarAlpha-VF.ttf     ← already in codebase
├── src/
│   ├── transcribe.js             — Whisper API, returns word timestamps
│   ├── audioFeatures.js          — RMS + pitch per word segment
│   ├── semanticTag.js            — Claude semantic tagging (separate call)
│   ├── manifest.js               — builds final word manifest
│   ├── layout.js                 — positions words on canvas
│   ├── fontAgent.js              — Claude font selection + axis setting
│   ├── render.js                 — opentype.js canvas rendering
│   └── index.js                  — orchestrates full pipeline
├── test/
│   ├── recordings/               — test .wav files
│   └── debugRender.js            — plain text layout debug view
└── output/                       — generated poster PNGs
```

---

## Build Order

1. **`debugRender.js` first** — render plain text at layout positions using manifest data. Validates the pipeline is working before touching font rendering.
2. **Single word render** — get one word rendering in Amstelvar with manual axis values. Confirm opentype.js variable font API works with these specific TTFs.
3. **Font agent on one word** — run the agent prompt with one word, confirm JSON output parses and renders correctly.
4. **Full pipeline** — wire all stages together on a test recording.
5. **Blend case** — SDF interpolation, implement last.

---

## Notes

- The layout rules are intentionally simple for now. Future direction: layout as expressive as letterforms — words trailing off lift off the baseline, fast speech compresses spacing, pauses create spatial drama. Treat layout as a first-class expressive dimension later.
- Test with at least: a calm sentence, a sentence with one angry word, a sentence with long pauses, a sentence with fillers, and something that trails off quietly.
- The font agent reasoning field is important — log it during development so you can see why it's making choices and tune the prompt accordingly.
