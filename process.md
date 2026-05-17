# Voice Typography — Process, Iterations, and Findings

## Overview

This project explores the question: *can the way something is said become the way it looks?* The output is a thermal-printed label — a physical artifact — whose typography is generated in direct response to a spoken voice message. Over four major iterations, the pipeline evolved from real-time browser rendering to a hardware-embedded wireless printing system driven by AI image generation.

---

## Iteration 1 — Real-Time CSS Typography (no AI)

### Approach

The first implementation ran entirely in the browser with no backend or API calls. The Web Speech API (`webkitSpeechRecognition`, `continuous: true`, `interimResults: true`) provided live transcription. The Web Audio API ran a continuous `requestAnimationFrame` loop extracting two audio features per frame: RMS volume and pitch via autocorrelation. As each word or phrase was finalized, it was stamped with the current audio state and rendered as a `<span>` element with inline CSS properties derived from a deterministic mapping function.

**Mapping logic:**
- Volume → font size (18–96px), font weight (300–900), opacity (0.4–1.0)
- Pitch → letter spacing, horizontal skew, color (cool blue → warm red)
- Low volume → blur filter
- High volume + high pitch → text shadow glow

**Stack:** Web Speech API, Web Audio API, DOM spans, no server, no ML.

### What worked

Zero latency. Words appeared as they were spoken. The mapping was immediately legible — loud words were visually loud, whispers faded out. The system had a live, reactive quality that felt connected to the voice.

### What failed

**Latency between sound and word.** The Web Speech API delivers words in chunks, not phonemes. By the time a word was finalized and its style stamped, the audio feature values had already moved on. Words were stamped with the wrong audio context — a word spoken quietly but followed immediately by a loud exclamation would inherit the loud state.

**Pitch detection noise.** Autocorrelation on short word segments was unreliable. Unvoiced consonants and short vowel durations produced no detectable fundamental frequency. The pitch mapping degraded to noise for many words.

**Expressiveness ceiling.** CSS properties are a narrow expressive vocabulary. Font size and weight are not the same as typographic character. The output looked like a data visualization of audio features, not expressive typography.

**Web Speech API fragility.** The API restarted every ~60 seconds, produced inconsistent interim results across browsers, and was unavailable on non-Chromium browsers.

---

## Iteration 2 — Post-Hoc Processing with Variable Font Parametric Control

### Approach

The critical architectural pivot was abandoning real-time output in favor of a record-then-process model. The user holds a button, speaks a message, releases it, and waits a few seconds for the output. This eliminated the timing problem entirely: all audio features are extracted from the complete recording with accurate word-level alignment.

**Pipeline:**
1. Record full message via `MediaRecorder`
2. Transcribe with OpenAI Whisper (`word_timestamps: true`) — returns per-word start/end times
3. Extract audio features per word from the original recording using the Whisper timestamps: RMS volume, pitch (autocorrelation), speaking rate, pause duration before each word
4. One batched Claude API call for semantic tagging: each word tagged with an emotion (`angry`, `sad`, `excited`, `calm`, `uncertain`, `emphatic`, `tender`, `fearful`, `neutral`) and intensity
5. Build a **word manifest** — one object per word combining transcription, audio features, and semantic tag
6. Second Claude API call — a "font agent" that received the full manifest and returned variable font axis values per word
7. Render using `opentype.js` with two variable fonts: **Decovar** (chaotic, decorative, extreme) and **Amstelvar** (precise, parametric, always-serif)
8. A custom warp engine additionally modified glyph bezier control points directly for effects beyond what font axes offered (wobble, swell, spike, shatter)

**Word manifest schema (excerpt):**
```
word, start, end, pauseBefore, duration, volume (0–1), pitch (0–1),
speakingRate, tag, intensity, isFiller
```

**Font axes in use:**
- Decovar: `TRMF` (spike terminals), `WMX2` (weight), `SKLA` (skeleton), `TRMK` (bifurcated), `BLDA` (inline decoration)
- Amstelvar: `wght`, `wdth`, `XOPQ`, `YOPQ`, `XTRA`, `YTUC`, `YTAS`, `YTDE`
- Blend case: both fonts rendered to offscreen canvases, SDF-interpolated

### What worked

The record-then-process model solved the timing problem completely. Audio feature extraction against Whisper timestamps was accurate — volume and pitch values genuinely reflected how each word was spoken. The font agent produced varied, often surprising axis combinations that would not have been discovered manually. Variable fonts allowed continuous expression across a multi-dimensional parameter space. Individual word renders could be striking.

### What failed

**Variable fonts are not expressive enough.** Font axes are constrained by the typeface designer's intent. Even with two fonts and a warp engine, the output lived within a defined aesthetic space. The fonts could be pushed toward extremes but could not break their own structure. "Expressive typography" as a concept demands letterforms that look unlike any font — melting, shattered, hand-drawn, scratched, collaged. No variable font can produce these.

**Word-by-word analysis produced incoherence.** Each word's style was computed in isolation from its neighbors. The result was a poster with no visual coherence — a different typeface aesthetic every word, no compositional logic, no shared visual language across the image. The output read as a parameter dump, not a designed artifact.

**Pitch remained noisy.** Even with accurate timestamps, short words (1–3 phonemes) did not have enough audio data for reliable pitch extraction. The pitch axis was frequently wrong and added noise rather than signal.

**Latency from multiple API calls.** Whisper + semantic tagging + font agent = three sequential API round-trips before rendering began. Total processing time was 8–15 seconds per recording. For a physical printing artifact this was tolerable, but it made iteration slow.

**Layout was a separate hard problem.** Positioning words of wildly different sizes and visual weights into a coherent composition required its own layout engine. The rules-based layout (volume → fontSize, pauseBefore → extra margin) produced awkward compositions that did not account for the visual weight of each word. A second layout agent was proposed but added further latency.

---

## Iteration 3 — Image-Generation Per Word with Layout Agent

### Approach

Rather than controlling font axes, this variant used Flux (via fal.ai) as an `img2img` ControlNet diffusion model to style-transfer each word. Each word was first rendered in a neutral base font on white, then sent to ControlNet with a style prompt generated by Claude. The structure of the letterforms (captured by canny edge detection) was preserved while the style was transformed. All words were generated in parallel.

**Stack addition:** `@fal-ai/client`, Flux ControlNet canny, base canvas rendering as structure reference.

### What failed

**Per-word generation did not produce a coherent label.** Each word was generated with its own diffusion context, its own lighting, texture, and stylistic vocabulary. Compositing them produced a collage of unrelated images rather than a unified typographic piece.

**The layout problem became worse.** Generated word images had unpredictable natural sizes and aspect ratios. Fitting them into a compositional grid required scaling that destroyed the image quality or made the layout arbitrary.

**Generation time per word.** Even with parallelism, 10+ words × one diffusion call each saturated API rate limits and added cost. The visual result did not justify the overhead.

**The wrong unit of generation.** Generating typography word-by-word is fighting against how image generation models work. These models are trained to produce spatially coherent images. Generating a whole label as a single image — giving the model full compositional control — produces far more coherent results.

---

## Iteration 4 — Whole-Label Image Generation (Final System)

### Approach

The final system treats the entire spoken message as a single creative brief and generates one label image. The pipeline collapsed from seven sequential steps to three.

**Pipeline:**
1. Record full message via `MediaRecorder` with physical button (hold to record, release to stop)
2. Transcribe with OpenAI Whisper → full transcript + word timestamps
3. Extract average RMS volume across all words (single scalar)
4. One Claude API call: receives the transcript and overall volume label, returns a typography style name and a detailed image generation prompt specifying letterform style, per-word size/weight/italic variation, and overall composition
5. One Imagen 4.0 call with the returned prompt → full label PNG at 1:1 aspect ratio
6. Browser-side: auto-crop whitespace, rotate 180° for printer orientation
7. POST base64 image to local Node.js server → Python converts to ESC/POS bitmap → POST raw bytes to XIAO ESP32S3 over WiFi → XIAO forwards to thermal printer via Serial1 at 9600 baud

**Claude's role shifted from parameter setter to creative director.** Instead of returning numeric axis values, Claude returned a holistic image prompt describing the full visual concept — typeface character, dramatic per-word variation, composition, and style. This is a task the model does well; generating structured numeric parameters within a constrained schema was a task it did inconsistently.

**Volume handling.** Rather than analyzing pitch and volume word-by-word, only overall message volume is used — loud vs. normal vs. whisper — as a single intensity modifier. This proved sufficient: the semantic content of the transcript carries far more expressive information than fine-grained audio features. What was said matters more than exactly how loud each word was.

**Hardware system:**
- XIAO ESP32S3 flashed as a WiFi Access Point (SSID: "typeshit")
- Mac joins the XIAO's network; iPhone USB provides internet for API calls
- Physical button on XIAO → polled by browser via Node.js proxy → triggers recording
- Thermal printer: QR204, 58mm paper, ESC/POS protocol over Serial1
- ESC/POS bitmap: 384 dots wide (48 bytes/row), GS v 0 raster command, heat settings calibrated for consistent output
- Wireless print path: browser → Node.js → Python (`wireless_print_bitmap.py`) → HTTP chunked POST to XIAO → Serial1 → printer

### What worked

**Coherence.** A single image generation call produces a unified composition. The model handles layout, scale contrast, visual weight, and stylistic consistency as part of generating one image. This is categorically better than assembling word images post-hoc.

**Expressiveness.** Diffusion models trained on typography, lettering, and graphic design have internalized a vocabulary of expressive forms — brutalist, calligraphic, grunge, condensed, distressed — that no variable font can access. The prompt space is far larger than any font axis space.

**Prompt quality from Claude.** Asking Claude to write an image generation prompt for a typography concept produced significantly better results than asking it to return JSON parameters. The model's natural language strengths aligned with the task. Prompts described things like "impossibly tall stretched skeletal letters, VOID towering over the rest, the barely visible" — descriptions that Imagen could execute directly.

**Physical output changes the artifact.** Thermal print output is inherently limited: 1-bit monochrome, 384 dots wide, heat-sensitive paper. This constraint enforced a black-and-white aesthetic that became part of the design language. The physical label is a more compelling artifact than a screen render.

**Interaction model.** The physical button replaced browser UI entirely for recording control. Holding the button feels connected to the act of speaking; releasing it commits the utterance. The printer's output is a tangible record of a moment of speech.

### Limitations

**No word-level audio expressiveness.** The trade-off for coherence was granularity. The system no longer knows which specific word was emphasized, which trailed off, which had a pause before it. Overall volume is a coarse signal. A future direction would be to pass richer audio analysis to Claude — not to drive parameters directly, but as context for its creative direction.

**Image generation is non-deterministic.** The same transcript and volume can produce very different outputs. This is partly a feature (each print is unique) but makes quality inconsistent.

**Printer calibration is manual.** ESC/POS heat settings, chunk pacing, and threshold values for bitmap dithering all affect output quality and must be tuned for the specific printer and paper combination. These values are not automatically derived.

---

## Key Design Decisions and Lessons

**Record-then-process beats real-time for quality.** The latency budget for a physical print (5–10 seconds is acceptable) is completely different from a screen display. Accepting latency enabled accurate audio analysis and multiple AI pipeline stages.

**The unit of generation matters.** Generating the whole label as one image rather than assembling per-word images produced a qualitative leap in output coherence. AI image generation models produce spatially coherent outputs; that property should be used, not worked around.

**Coarse audio analysis is sufficient.** Detailed per-word pitch and volume analysis added complexity without improving outputs. The semantic content of what was said — available from transcription alone — carries most of the expressive information. Volume as a global modifier (loud/normal/quiet) provided enough acoustic grounding.

**Language models are better creative directors than parameter setters.** When Claude was asked to set numeric font axis values within a defined schema, outputs were inconsistent and required extensive prompt engineering. When Claude was asked to describe a typographic concept in natural language for an image model to execute, outputs were consistently creative and varied.

**Physical constraints are design material.** The 1-bit thermal print output forced a black-on-white aesthetic that disciplined the design space. Every generated label had to work in high-contrast monochrome. This constraint produced more graphically rigorous outputs than unconstrained color renders.

**Hardware simplicity compounds reliability.** Replacing complex WiFi-dependent printing with direct Serial1 output from firmware-embedded bitmap data (the `kick_print.cpp` approach) eliminated the entire software stack between button press and print. The most reliable configuration is the simplest: bytes in flash, direct to serial, triggered by GPIO.

---

## Final System Architecture

```
Physical button press (XIAO D1)
    ↓
Browser polls /button-state (150ms interval)
    ↓
MediaRecorder captures audio
    ↓
Whisper API — transcript + word timestamps
    ↓
Web Audio API — average RMS volume (single scalar)
    ↓
Claude Sonnet — typography style + Imagen prompt
    ↓
Imagen 4.0 — full label PNG (1:1)
    ↓
Canvas: auto-crop whitespace → rotate 180°
    ↓
POST /print (base64 PNG) → Node.js server
    ↓
Python: PNG → ESC/POS bitmap (384px wide, 1-bit)
    ↓
HTTP chunked POST → XIAO ESP32S3 (192.168.4.1)
    ↓
Serial1 (9600 baud) → QR204 thermal printer
    ↓
Physical printed label
```

**Total pipeline latency:** approximately 8–12 seconds from button release to print start.

**Hardware:**
- XIAO ESP32S3 (Seeed Studio) — WiFi AP, HTTP server, serial bridge
- QR204 thermal printer — 58mm paper, ESC/POS, 9600 baud
- Physical momentary button, LED indicator
- Mac as processing host; iPhone USB as internet uplink while on XIAO WiFi
