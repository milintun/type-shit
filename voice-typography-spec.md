# Voice Typography App — Build Spec

## Overview

A real-time web app that listens to your voice and renders what you say in live, expressive typography — where the *look* of the text reflects *how* you said it. Pitch and volume drive visual parameters. Words appear as you speak, styled to match the sonic character of your voice in that moment.

This is V1. It uses zero ML inference. Everything runs in the browser.

---

## Core Experience

1. User clicks a button to start listening
2. Words appear on screen in real time as they're spoken (interim results, not waiting for sentence completion)
3. Each word or phrase is styled based on the audio characteristics at the moment it was spoken
4. Styles persist — the screen fills up with a visual record of the conversation's emotional arc

---

## Tech Stack

| Layer | Tool | Notes |
|---|---|---|
| Speech-to-text | Web Speech API | `interimResults: true`, `continuous: true` |
| Pitch detection | Web Audio API + autocorrelation | Run on `AnalyserNode` time-domain data |
| Volume | Web Audio API RMS | Computed from `getFloatTimeDomainData` |
| Rendering | HTML Canvas or DOM | See rendering section |
| Pitch lib (optional) | `pitchy` (npm) | Cleaner autocorrelation than rolling your own |

No backend. No API calls. Runs entirely client-side.

---

## Audio Pipeline

### Setup
```js
const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
const audioCtx = new AudioContext();
const source = audioCtx.createMediaStreamSource(stream);
const analyser = audioCtx.createAnalyser();
analyser.fftSize = 2048;
source.connect(analyser);
```

### Volume (RMS) — per animation frame
```js
const buf = new Float32Array(analyser.fftSize);
analyser.getFloatTimeDomainData(buf);
const rms = Math.sqrt(buf.reduce((s, v) => s + v * v, 0) / buf.length);
// rms range: ~0.0 (silence) to ~0.8 (loud)
```

### Pitch (autocorrelation) — per animation frame
Use `pitchy` or implement autocorrelation on `buf`. Returns Hz.
Typical speech range: 80Hz (low male) to 400Hz (high female/excited).
Clamp and normalize to [0, 1] before mapping.

### The rAF loop
```js
function tick() {
  requestAnimationFrame(tick);
  const volume = getRMS();
  const pitch = getPitch();
  currentAudioState = { volume, pitch };
}
tick();
```

---

## Speech Pipeline

```js
const recognition = new webkitSpeechRecognition();
recognition.continuous = true;
recognition.interimResults = true;
recognition.lang = 'en-US';

recognition.onresult = (event) => {
  const result = event.results[event.results.length - 1];
  const transcript = result[0].transcript;
  const isFinal = result.isFinal;

  // Stamp current audio state onto this word chunk
  renderWordChunk(transcript, { ...currentAudioState }, isFinal);
};
```

Key insight: stamp `currentAudioState` at the moment the word arrives. That's what gets baked into its visual style.

---

## Mapping Function

This is the creative core. Takes normalized audio features, returns visual style params.

```js
function audioToStyle({ pitch, volume }) {
  // pitch, volume: both normalized 0–1

  const lerp = (a, b, t) => a + (b - a) * t;
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

  return {
    fontSize:      lerp(18, 96, volume),
    fontWeight:    Math.round(lerp(300, 900, volume) / 100) * 100,
    letterSpacing: lerp(-1, 14, pitch),      // high pitch = airy/spread
    opacity:       lerp(0.4, 1.0, volume),   // whisper = ghost
    skewX:         lerp(0, 12, pitch),       // high pitch = italic lean
    color:         lerpColor('#3B8BD4', '#E8593C', pitch), // cool→warm
    // optionally:
    textShadow:    volume > 0.7 ? `0 0 ${volume * 20}px currentColor` : 'none',
    blur:          volume < 0.15 ? `blur(${(0.15 - volume) * 8}px)` : 'none',
  };
}

function lerpColor(hex1, hex2, t) {
  // parse, lerp each channel, return hex
}
```

### Suggested Mappings Table

| Audio feature | Visual output | Rationale |
|---|---|---|
| Volume ↑ | Font size ↑, weight ↑ | Loud = physically large |
| Volume ↓ | Opacity ↓, blur slight | Whisper = barely there |
| Pitch ↑ | Warmer color, wider letter-spacing | High = bright, airy |
| Pitch ↓ | Cooler color, tighter tracking | Low = dense, grounded |
| Pitch ↑ (extreme) | skewX ↑ | High pitch feels slanted |
| Volume spike | Brief scale punch (CSS transition) | Impact frame |
| Monotone pitch | Desaturate toward gray | Flat voice = flat color |

These are defaults. The mapping function is the place to experiment — this is the personality of the app.

---

## Rendering

### Option A: DOM-based (simpler, more flexible)
Each word chunk is a `<span>` appended to a container div. Styles applied inline.

```js
function renderWordChunk(text, audioState, isFinal) {
  const style = audioToStyle(audioState);
  const span = document.createElement('span');
  span.textContent = text + ' ';
  Object.assign(span.style, {
    fontSize: style.fontSize + 'px',
    fontWeight: style.fontWeight,
    letterSpacing: style.letterSpacing + 'px',
    opacity: style.opacity,
    transform: `skewX(-${style.skewX}deg)`,
    color: style.color,
    display: 'inline-block',
    transition: 'all 0.15s ease',
  });
  container.appendChild(span);
  // if interim, update the last span instead of appending
}
```

Interim results: keep a reference to the "pending" span. Replace its text and restyle it on each interim event. Commit it (freeze style) on `isFinal`.

### Option B: Canvas (more control, harder to implement)
Use PixiJS `Text` objects. More powerful for V2 when you need per-glyph manipulation. Overkill for V1.

**Recommendation: start with DOM. Switch to PixiJS when you need per-character effects or glyph warping (V2).**

---

## Layout & Visual Design

- Dark background preferred — colored text pops more
- Words flow left-to-right, wrapping naturally (flexbox `flex-wrap: wrap; align-items: baseline`)
- No fixed grid — let the size variation create organic flow
- Consider: new sentences start on a new line; phrases within a sentence flow inline
- Optionally: fade out old words over time so screen doesn't fill up

### Font choices
Pick a font with a wide weight range (100–900) so `fontWeight` mapping has real range. Good options:
- **Variable fonts**: Any Google Fonts variable font (e.g. `Fraunces`, `Recursive`, `Cabinet Grotesk`)
- The variable weight axis is what makes the volume→weight mapping feel smooth

---

## UI Chrome

Minimal. The text IS the UI.

- Single button: **Start / Stop**
- Small status indicator: mic active, silence detected
- Optional: live pitch/volume bars (for debugging, can be hidden in production)
- No settings panel in V1

---

## State Machine

```
IDLE → (click start) → LISTENING → (click stop) → IDLE
LISTENING → (silence timeout ~3s) → PAUSED → (voice resumes) → LISTENING
```

---

## Edge Cases to Handle

- **No pitch detected** (silence between words): use last known pitch, or fall back to defaults
- **Browser support**: Web Speech API is Chrome/Edge only. Show a warning for Firefox/Safari
- **Mic permission denied**: show a clear error state
- **Recognition restart**: `webkitSpeechRecognition` times out after ~60s of continuous use — auto-restart in `onend` handler while still in LISTENING state
- **Interim vs final**: don't stamp final styles until `isFinal === true`, or the style will flicker as interim transcripts update

---

## File Structure

```
/
├── index.html
├── style.css
├── main.js
│   ├── audio.js       — Web Audio setup, RMS, pitch
│   ├── speech.js      — Web Speech API wrapper
│   ├── mapping.js     — audioToStyle(), lerpColor()
│   └── renderer.js    — DOM span management
└── README.md
```

Or bundle it all in one HTML file for simplicity.

---

## V2 Hooks (don't build yet, but design for)

The mapping function should be the **only** thing that changes between V1 and V2. V2 replaces `audioToStyle()` returning CSS properties with a function that returns glyph bezier warp parameters, feeds them to `opentype.js`, and registers a new `FontFace` before rendering.

Keep the audio pipeline, speech pipeline, and renderer interface stable. Swap the mapping layer.

```js
// V1
const style = audioToStyle(audioState);  // → CSS props

// V2
const glyphParams = audioToGlyphParams(audioState);  // → bezier warp params
const font = await buildWarpedFont(glyphParams);      // → opentype.js TTF blob
await registerFont(font);                             // → FontFace API
const style = { fontFamily: font.name };              // → use it
```

---

## Success Criteria

- Words appear within ~300ms of being spoken
- Visual styles feel meaningfully connected to how the word was said
- Whispering looks different from shouting looks different from normal speech
- No crashes on 5 minutes of continuous use
- Works in Chrome desktop

---

## What This Is Not

- Not a transcription tool
- Not trying to be accurate above ~90% — errors are fine, even interesting
- Not a production app with accounts, saving, sharing (V1)
- Not cross-browser (V1 is Chrome-only, that's fine)
