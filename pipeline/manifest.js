// computeDerivedFields runs before the Claude call so pauseBefore/speakingRate
// are available as inputs to the word agent.
export function computeDerivedFields(wordsWithAudio) {
  const m = wordsWithAudio.map((w, i) => ({
    ...w,
    duration:    w.end - w.start,
    pauseBefore: i === 0 ? 0 : w.start - wordsWithAudio[i - 1].end,
  }));
  const avg = m.reduce((s, w) => s + w.duration, 0) / m.length;
  m.forEach(w => { w.speakingRate = 1 - Math.min(w.duration / (avg * 2), 1); });
  return m;
}

// Merges agent output (tag, isFiller, intensity) into the derived manifest.
export function buildManifest(wordsWithDerived, agentOutputs) {
  return wordsWithDerived.map((w, i) => ({
    ...w,
    tag:      agentOutputs[i]?.tag      ?? 'neutral',
    intensity: agentOutputs[i]?.intensity ?? 0.5,
    isFiller: agentOutputs[i]?.isFiller ?? false,
  }));
}
