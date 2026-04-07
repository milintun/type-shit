const CANVAS_W = 1200;
const lerp = (a, b, t) => a + (b - a) * t;

export function layoutWords(manifest) {
  const pad = 80;
  const lh  = 1.6;
  let x = pad, y = pad + 96;

  return manifest.map(word => {
    const fs    = word.isFiller ? 10 : Math.round(lerp(16, 96, word.volume));
    const extra = word.pauseBefore > 0.3
      ? lerp(0, 80, Math.min((word.pauseBefore - 0.3) / 0.7, 1))
      : 0;
    const estW  = word.word.length * fs * 0.55;

    if (word.pauseBefore > 0.8 || x + extra + estW > CANVAS_W - pad) {
      x  = pad;
      y += fs * lh + 16;
    }

    x += extra;
    const slot = { x, y, fontSize: fs, opacity: word.isFiller ? 0.35 : 1.0 };
    x += estW + fs * 0.3;

    return { ...word, layout: slot };
  });
}
