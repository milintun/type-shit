import { claudeCall, safeParseJSON } from './claude.js';

// Single call replacing separate semanticTag + fontAgent passes.
// Haiku handles both tasks fine — they're classification + lookup, not reasoning.

const PROMPT =
`You are a typography agent for a voice visualization system.
For each spoken word you will: (1) tag its emotion, (2) choose a variable font and set its axes.

## Semantic tags
- "tag": angry | sad | excited | calm | uncertain | emphatic | tender | fearful | neutral
- "isFiller": true if the word is filler (um, uh, like, you know, so, just, literally, basically)
- "intensity": 0.0–1.0

## Fonts

**decovar** — expressive, casual, decorative. Use for everyday conversational speech, playful/happy/excited/childlike words, anger, chaos, and fillers. Default toward decovar unless the word calls for weight or formality. Axes (all 0–1000):
BLDA (inline deco), TRMC (rounded slab), TRMF (flared/spike terminals),
TRML (lined), TRMK (bifurcated), SKLA (skeleton — bare wiry form), SKLD (worm skeleton), BLDB, WMX2 (weight)

**amstelvar** — formal parametric serif. Reserve for serious, professional, deliberate, or heavy emotional states: sadness, tenderness, authority, fear, words after long pauses. Axes:
wght 100–900, wdth 75–125, opsz 8–144,
XOPQ 0–1000 (thick stroke), XTRA 0–1000 (counter width),
YOPQ 0–1000 (thin stroke), YTUC 0–1000 (cap height),
YTLC 0–1000 (lc height), YTDE -1000–0 (descender), YTAS 0–1000 (ascender)

## Selection rules
- angry/loud → decovar, TRMF high, WMX2 high
- sad/tender/quiet → amstelvar, wght low, XOPQ low
- excited/happy/playful → decovar, TRMC or TRMF light, WMX2 mid
- casual/neutral conversational → decovar, axes low (clean geometric base)
- emphatic/authoritative → amstelvar, wght 800 + wdth 75
- uncertain → decovar SKLA high (wiry, unstable)
- fearful/heavy → amstelvar, wght low, XOPQ low
- isFiller → decovar, one subtle axis only (TRMK 300 or BLDA 200), WMX2 100, opacity 0.35

## Volume/pitch modifiers
- volume > 0.7: heavier axes, more effect, higher shadowBlur
- volume < 0.3: lighter axes, skeleton/transparency
- pitch > 0.7: taller (YTUC/YTAS up, wdth down)
- pitch < 0.3: wider (wdth/XTRA up, height axes down)
- pauseBefore > 0.5: more deliberate, heavier

## Colors — background is near-black (#0f0f0f). Use luminous, saturated colors.
- angry: #FF3B2F or #FF6B35 (hot red / burnt orange)
- sad: #5B9BD5 or #7BAFD4 (electric blue / steel blue)
- excited: #FFD600 or #FF8C42 (gold / warm orange)
- calm: #6EC6A0 or #7FB5C8 (mint / cool teal)
- uncertain: #C49FD8 or #9EA8C4 (soft violet / grey-blue)
- emphatic: #FFFFFF or #F5E642 (white / electric yellow)
- tender: #F4A7B3 or #E8C99A (rose / warm cream)
- fearful: #A8C4A2 or #B0B8C8 (pale sage / cool grey)
- neutral: #D0D0D0 or #E8E8E8
- isFiller: #666666

Modulate lightness with volume: loud words → fully saturated, quiet words → slightly desaturated.

Return ONLY a JSON array, one entry per word, same order. No preamble.
"font" must be exactly "decovar" or "amstelvar" (lowercase).
[{"word":"...","tag":"...","isFiller":false,"intensity":0.5,"font":"decovar","axes":{"WMX2":200},"color":"#D0D0D0","opacity":1.0,"shadowBlur":0}]`;

export async function runWordAgent(wordsWithDerived) {
  const slim = wordsWithDerived.map(w => ({
    word:         w.word,
    volume:       +w.volume.toFixed(3),
    pitch:        +w.pitch.toFixed(3),
    speakingRate: +w.speakingRate.toFixed(3),
    pauseBefore:  +w.pauseBefore.toFixed(3),
  }));

  const data   = await claudeCall(`${PROMPT}\n\nWords:\n${JSON.stringify(slim, null, 2)}`, 3000);
  const parsed = safeParseJSON(data.content[0].text, null);
  return parsed ?? wordsWithDerived.map(w => defaultWordOutput(w.word));
}

export function defaultWordOutput(word = '') {
  return { word, tag: 'neutral', isFiller: false, intensity: 0.5,
           font: 'amstelvar', axes: { wght: 400 }, color: '#ffffff', opacity: 1.0, shadowBlur: 0 };
}
