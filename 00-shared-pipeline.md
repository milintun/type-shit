# Voice Typography — Shared Pipeline

## Project Goal

User records a short message (< 10s). The system produces a poster-like static image where every word is typographically styled to reflect how it was spoken — volume, pitch, pace, pauses, semantic tone, and filler words all expressed visually. The output is a rich transcription with emotion and voice baked into the letterforms.

This shared pipeline runs once per recording and feeds both implementation variants identically. The two impls are different consumers of the same word manifest.

---

## Future / Notes

- **Layout is rules-based for now but intentionally replaceable.** Interesting territory to explore: layout as expressive as letterforms. Words trailing off could physically lift off the baseline. Fast speech could compress horizontal spacing. Overlapping words for interruptions. Placement as a first-class expressive dimension, not just a container.
- Both impl outputs should be runnable on the same recording for direct comparison.

---

## Pipeline Overview

```
Audio recording (< 10s)
        ↓
Whisper API — transcription + word-level timestamps
        ↓
Web Audio API — per-word: RMS volume, pitch (autocorrelation)
        ↓
Claude API — per-word semantic tag (one batched call)
        ↓
Word Manifest
        ↓
Layout Engine — positions each word on a canvas
        ↓
[ Impl 1: Parametric ]   [ Impl 2: Image Gen ]
        ↓                         ↓
     Poster PNG               Poster PNG
```

---

## Step 1 — Transcription (Whisper)

Use OpenAI Whisper via API with `word_timestamps: true`. This gives start/end time per word.

```js
const { default: OpenAI } = await import('openai');
const openai = new OpenAI({ apiKey: process.env.OPENAI_API_KEY });

const transcription = await openai.audio.transcriptions.create({
  file: fs.createReadStream('recording.wav'),
  model: 'whisper-1',
  response_format: 'verbose_json',
  timestamp_granularities: ['word'],
});

// transcription.words: [{ word, start, end }, ...]
```

---

## Step 2 — Audio Feature Extraction (Web Audio API)

For each word, extract volume and pitch from the audio segment `[word.start, word.end]`.

```js
async function extractAudioFeatures(audioBuffer, wordTimestamps) {
  const sampleRate = audioBuffer.sampleRate;
  const channelData = audioBuffer.getChannelData(0);

  return wordTimestamps.map(({ word, start, end }) => {
    const startSample = Math.floor(start * sampleRate);
    const endSample = Math.floor(end * sampleRate);
    const segment = channelData.slice(startSample, endSample);

    const volume = getRMS(segment);
    const pitch = autocorrelate(segment, sampleRate);

    return { word, start, end, volume, pitch };
  });
}

function getRMS(samples) {
  const sum = samples.reduce((s, v) => s + v * v, 0);
  return Math.sqrt(sum / samples.length);
}

// autocorrelate returns Hz (80–400 typical speech range), or -1 if no pitch detected
function autocorrelate(buf, sampleRate) {
  // use pitchy lib or roll your own — returns fundamental frequency in Hz
  // normalize result: (hz - 80) / (400 - 80), clamped 0–1
}
```

**Normalization targets:**
- Volume: raw RMS ~0.0–0.8 → normalize to 0–1
- Pitch: 80Hz–400Hz speech range → normalize to 0–1. Return 0.5 as default if undetected.

---

## Step 3 — Semantic Tagging (Claude, one batched call)

Send the full word list with audio features in a single Claude call. Returns semantic tags and filler detection.

```js
const prompt = `You are analyzing a spoken audio transcript to tag each word with its emotional/tonal character for expressive typography.

For each word, return a JSON object with:
- "tag": one of: angry, sad, excited, calm, uncertain, emphatic, tender, fearful, neutral
- "isFiller": true if the word is a filler (um, uh, like, you know, so, just, literally, basically)
- "intensity": 0.0–1.0, how strongly the tag applies

Here are the words with their audio features:
${JSON.stringify(wordFeatures, null, 2)}

Return ONLY a JSON array, one entry per word, same order. No preamble.`;

const response = await anthropic.messages.create({
  model: 'claude-sonnet-4-20250514',
  max_tokens: 1000,
  messages: [{ role: 'user', content: prompt }],
});

const tags = JSON.parse(response.content[0].text);
```

---

## Word Manifest Schema

After steps 1–3, merge everything into a manifest. One object per word.

```js
{
  word: "burning",
  start: 1.24,
  end: 1.71,
  pauseBefore: 0.42,       // gap in seconds from previous word end → this word start
  duration: 0.47,          // end - start
  volume: 0.82,            // normalized 0–1
  pitch: 0.74,             // normalized 0–1
  speakingRate: 0.65,      // local pace: duration normalized against avg word duration
  tag: "angry",            // from Claude
  intensity: 0.9,          // from Claude
  isFiller: false          // from Claude
}
```

**Computing `pauseBefore`:**
```js
words.forEach((w, i) => {
  w.pauseBefore = i === 0 ? 0 : w.start - words[i - 1].end;
});
```

**Computing `speakingRate`:**
```js
const avgDuration = words.reduce((s, w) => s + w.duration, 0) / words.length;
words.forEach(w => {
  w.speakingRate = 1 - Math.min(w.duration / (avgDuration * 2), 1); // fast = high
});
```

---

## Step 4 — Layout Engine

Positions every word on the poster canvas. Returns a layout manifest with pixel positions.

### Rules

| Feature | Layout effect |
|---|---|
| `volume` | `fontSize` = lerp(16px, 96px, volume) |
| `pauseBefore > 0.3s` | extra left margin = lerp(0, 80px, pauseBefore / 1.0) |
| `pauseBefore > 0.8s` | force new line |
| `speakingRate` high | tighter letter-spacing |
| `isFiller: true` | fontSize = 10px, opacity = 0.35, flag as ornamental |
| Line breaks | at sentence boundaries (`.`, `,`, `?`) + when line exceeds canvas width |

### Layout function

```js
function layoutWords(manifest, canvasWidth = 1200, canvasHeight = 800) {
  const padding = 80;
  const lineHeight = 1.6;
  let x = padding;
  let y = padding + 96; // first baseline

  return manifest.map((word, i) => {
    const fontSize = word.isFiller ? 10 : lerp(16, 96, word.volume);
    const extraMargin = word.pauseBefore > 0.3
      ? lerp(0, 80, (word.pauseBefore - 0.3) / 0.7)
      : 0;

    // line break conditions
    if (word.pauseBefore > 0.8 || x + estimateWordWidth(word.word, fontSize) > canvasWidth - padding) {
      x = padding;
      y += fontSize * lineHeight + 16;
    }

    x += extraMargin;

    const slot = { x, y, fontSize, word: word.word, opacity: word.isFiller ? 0.35 : 1 };
    x += estimateWordWidth(word.word, fontSize) + fontSize * 0.3; // word spacing

    return { ...word, layout: slot };
  });
}

function estimateWordWidth(word, fontSize) {
  return word.length * fontSize * 0.55; // rough estimate, refine per font
}
```

---

## Debug View

Before running either impl, build a debug renderer that just draws plain text at layout positions. Validates manifest and layout are correct before touching font generation.

```js
// Node.js with canvas package, or browser Canvas 2D
ctx.fillStyle = '#1a1a1a';
ctx.fillRect(0, 0, 1200, 800);

layoutManifest.forEach(({ word, layout, tag, volume }) => {
  ctx.font = `${layout.fontSize}px serif`;
  ctx.globalAlpha = layout.opacity;
  ctx.fillStyle = '#ffffff';
  ctx.fillText(word.word, layout.x, layout.y);

  // debug: tag label below word
  ctx.font = '10px monospace';
  ctx.fillStyle = '#888';
  ctx.fillText(tag, layout.x, layout.y + 12);
});
```

Get this working and looking right before touching either impl.

---

## File Structure

```
/
├── pipeline/
│   ├── transcribe.js      — Whisper call, returns word timestamps
│   ├── audioFeatures.js   — RMS + pitch per word segment
│   ├── semanticTag.js     — Claude batched tagging
│   ├── manifest.js        — merges all above into word manifest
│   └── layout.js          — layout engine, returns positioned manifest
├── impl1-parametric/
│   └── (see impl1 spec)
├── impl2-imagegen/
│   └── (see impl2 spec)
├── debug.js               — plain text render of layout manifest
├── test-recordings/       — 5–10 sample .wav files for comparison
└── compare.js             — runs both impls on same recording, outputs side by side
```

---

## Test Recordings to Prepare

Have at least these ready before building:

1. Calm, even-paced sentence
2. Sentence with one angry/emphatic word
3. Sentence with long pauses
4. Sentence with several filler words
5. Whispering trailing off at the end

These five cover the full range of the manifest's expressive space.
