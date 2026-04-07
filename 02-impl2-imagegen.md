# Impl 2 — Image Generation (img2img via fal.ai)

## Approach

Claude receives the word manifest and returns a style prompt per word. Each word is first rendered in a neutral base font to create a structural reference image. That image is sent to fal.ai Flux ControlNet (canny edge) as an `img2img` call with the style prompt. The returned styled PNG is composited into the poster canvas at the layout position.

All word images are generated in parallel. Total latency ~2-4s regardless of word count.

---

## Dependencies

```json
{
  "@fal-ai/client": "^1.0.0",
  "canvas": "^2.11.2"
}
```

---

## Step 1 — Claude Style Prompt Generation

One batched call for all words. Returns a visual description of the lettering style per word.

```js
const prompt = `You are a creative director for an expressive typography system.
Each word in a spoken recording will be rendered as styled lettering using an image generation model.
Your job is to write a prompt that describes the visual style of the lettering for each word.

The prompts will be used with a ControlNet img2img model — the structure of the letters is already set.
Your prompt controls the STYLE: texture, weight, material, treatment, color, and character.

Style guide by tag:
- angry: "aggressive blackletter lettering, sharp thorned serifs, thick jagged strokes, [color] ink, hand-stamped"
- sad: "thin melting letterforms, drooping strokes, watercolor bleed, desaturated ink, fragile"
- excited: "bold graffiti lettering, energetic brush strokes, warm [color], spray paint edges, kinetic"
- calm: "clean humanist letterforms, gentle ink, even weight, soft [color], refined"
- uncertain: "shaky hand-lettered, uneven baseline, thin strokes, faded ink, hesitant"
- emphatic: "heavy slab serif, compressed wide strokes, bold [color], stamped texture, commanding"
- tender: "soft brush calligraphy, light strokes, warm gentle [color], delicate"
- fearful: "thin erratic strokes, scratched texture, pale ink, jagged breaks"
- neutral: "clean sans-serif lettering, balanced weight, neutral ink"

Volume modifier:
- volume > 0.7: add "bold", "heavy", "large"
- volume < 0.3: add "faint", "small", "delicate"

Pitch modifier:
- pitch > 0.7: add "tall", "condensed", "vertical"
- pitch < 0.3: add "wide", "low", "horizontal"

isFiller: true → "tiny ornamental letterform, decorative, faint, barely visible, serif flourish"

For each word, return:
- stylePrompt: 10-20 words describing the lettering style
- negativePrompt: what to avoid (e.g. "clean digital, geometric, sans-serif, blurry")
- color: dominant hex color for the word

Here is the word manifest:
${JSON.stringify(manifest, null, 2)}

Return ONLY a JSON array, one entry per word, same order:
[{ "word": "...", "stylePrompt": "...", "negativePrompt": "...", "color": "#rrggbb" }]`;
```

---

## Step 2 — Base Render (Structure Reference)

For each word, render it in a neutral font on a white background. This becomes the ControlNet structure guide.

```js
import { createCanvas } from 'canvas';

function renderBaseWord(word, fontSize) {
  // canvas sized tightly to the word
  const width = Math.ceil(word.length * fontSize * 0.65) + 40;
  const height = Math.ceil(fontSize * 1.6) + 20;

  const canvas = createCanvas(width, height);
  const ctx = canvas.getContext('2d');

  // white background (required for canny edge detection)
  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, width, height);

  // black text, neutral font, vertically centered
  ctx.fillStyle = '#000000';
  ctx.font = `bold ${fontSize}px serif`;
  ctx.textBaseline = 'middle';
  ctx.fillText(word, 20, height / 2);

  return canvas.toBuffer('image/png');
}
```

The base render should be clean and neutral — no styling. ControlNet uses it only for edge structure.

---

## Step 3 — fal.ai img2img Call

```js
import * as fal from '@fal-ai/client';

fal.config({ credentials: process.env.FAL_KEY });

async function generateStyledWord(baseImageBuffer, stylePrompt, negativePrompt) {
  const base64Image = baseImageBuffer.toString('base64');
  const dataUrl = `data:image/png;base64,${base64Image}`;

  const result = await fal.subscribe('fal-ai/flux/dev/image-to-image', {
    input: {
      image_url: dataUrl,
      prompt: `expressive hand lettering, ${stylePrompt}, isolated word on dark background, typography art`,
      negative_prompt: `${negativePrompt}, multiple words, sentences, background clutter, watermark`,
      strength: 0.78,         // high enough for style transformation, low enough to keep structure
      num_inference_steps: 28,
      guidance_scale: 7.5,
      image_size: { width: 512, height: 256 },
    },
  });

  return result.data.images[0].url;
}
```

### ControlNet alternative (stronger structure preservation)

If plain img2img loses letterform structure too often, switch to ControlNet canny:

```js
const result = await fal.subscribe('fal-ai/controlnet-canny', {
  input: {
    control_image_url: dataUrl,
    prompt: `expressive hand lettering, ${stylePrompt}, isolated letters on dark background`,
    negative_prompt: negativePrompt,
    controlnet_conditioning_scale: 0.85,  // how much structure is preserved
    strength: 0.9,
    num_inference_steps: 30,
  },
});
```

**Tuning `strength` vs `controlnet_conditioning_scale`:**
- Raise `controlnet_conditioning_scale` if letters become unrecognizable
- Lower it if the style is too tame / not transforming enough
- `strength` controls how much the init image influences the output — start at 0.78

---

## Step 4 — Compositing

Download each generated image and composite it into the poster canvas at the layout position.

```js
import { loadImage, createCanvas } from 'canvas';

async function compositeWord(ctx, layoutWord, imageUrl) {
  const { layout, styleData } = layoutWord;

  const img = await loadImage(imageUrl);

  ctx.save();
  ctx.globalAlpha = layout.opacity;

  // optional: color tint overlay using styleData.color
  // draw the generated image, scaled to fit the layout slot
  const targetH = layout.fontSize * 1.6;
  const targetW = img.width * (targetH / img.height);

  ctx.drawImage(img, layout.x, layout.y - targetH * 0.8, targetW, targetH);
  ctx.restore();
}
```

---

## Full Render Pipeline

```js
async function renderImpl2(layoutManifest) {
  // 1. get style prompts from Claude (one call)
  const styleData = await getStylePrompts(layoutManifest);
  const withStyle = layoutManifest.map((w, i) => ({ ...w, styleData: styleData[i] }));

  // 2. render all base images
  const baseImages = withStyle.map(w => renderBaseWord(w.word.word, w.layout.fontSize));

  // 3. fire all fal.ai calls in parallel
  const imageUrls = await Promise.all(
    withStyle.map((w, i) =>
      generateStyledWord(baseImages[i], w.styleData.stylePrompt, w.styleData.negativePrompt)
    )
  );

  // 4. composite onto poster
  const canvas = createCanvas(1200, 800);
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = '#0f0f0f';
  ctx.fillRect(0, 0, 1200, 800);

  for (let i = 0; i < withStyle.length; i++) {
    await compositeWord(ctx, withStyle[i], imageUrls[i]);
  }

  return canvas;
}
```

---

## Cost and Rate Limits

- fal.ai Flux img2img: ~$0.003–0.006 per image at 512×256
- 10-word sentence ≈ $0.05 per poster render
- Parallel requests: fal.ai handles burst well — no need to throttle for < 20 concurrent requests

---

## Tuning Notes

- **Dark background in the generated image matters.** Specify `"isolated on dark background"` in every prompt — the compositing step assumes dark surrounds so edges blend into the poster background.
- **`strength: 0.78` is a starting point.** At 0.6 the style barely changes. At 0.9 letterforms often disappear. 0.75–0.82 is the useful range.
- **Filler words:** generate at small size (fontSize ~10) or skip generation entirely and just render a small dot or ornamental SVG character. The img2img pipeline at tiny sizes produces noise, not letters.
- **Inconsistency is a feature.** Each word is generated independently with randomness. The lack of visual consistency across the poster reflects the chaotic nature of voice. Lean into it.
- **If a word fails:** catch errors per-word, fall back to plain canvas text at that layout slot. Don't let one failure break the poster.

---

## Comparison Output

Run both impls on the same recording and output a side-by-side comparison:

```js
// compare.js
const manifest = await buildManifest('test-recordings/sample.wav');
const layout = layoutWords(manifest);

const [canvas1, canvas2] = await Promise.all([
  renderImpl1(layout),
  renderImpl2(layout),
]);

// stitch horizontally
const comparison = createCanvas(2400, 800);
const ctx = comparison.getContext('2d');
ctx.drawImage(canvas1, 0, 0);
ctx.drawImage(canvas2, 1200, 0);

// label
ctx.fillStyle = '#ffffff';
ctx.font = '20px monospace';
ctx.fillText('IMPL 1 — parametric', 20, 780);
ctx.fillText('IMPL 2 — image gen', 1220, 780);

fs.writeFileSync('comparison.png', comparison.toBuffer('image/png'));
```
