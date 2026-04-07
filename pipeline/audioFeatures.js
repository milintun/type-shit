export function extractAudioFeatures(audioBuffer, timestamps) {
  const sr   = audioBuffer.sampleRate;
  const data = audioBuffer.getChannelData(0);

  return timestamps.map(({ word, start, end }) => {
    const s0  = Math.floor(start * sr);
    const s1  = Math.min(Math.floor(end * sr), data.length);
    const seg = data.slice(s0, s1);

    const rawVol = getRMS(seg);
    const volume = Math.min(1, rawVol / 0.35);
    const hz     = autocorrelate(seg, sr);
    const pitch  = hz > 0 ? Math.max(0, Math.min(1, (hz - 80) / 320)) : 0.5;

    return { word, start, end, volume, pitch };
  });
}

function getRMS(samples) {
  if (!samples.length) return 0;
  let s = 0;
  for (let i = 0; i < samples.length; i++) s += samples[i] * samples[i];
  return Math.sqrt(s / samples.length);
}

function autocorrelate(buf, sampleRate) {
  const n = buf.length;
  if (n < 256) return -1;

  let energy = 0;
  for (let i = 0; i < n; i++) energy += buf[i] * buf[i];
  if (energy / n < 0.0001) return -1;

  const minLag = Math.floor(sampleRate / 400);
  const maxLag = Math.min(Math.ceil(sampleRate / 80), Math.floor(n / 2));

  let bestCorr = 0, bestLag = -1;
  for (let lag = minLag; lag <= maxLag; lag++) {
    let c = 0;
    const len = n - lag;
    for (let i = 0; i < len; i++) c += buf[i] * buf[i + lag];
    const norm = c / energy;
    if (norm > bestCorr) { bestCorr = norm; bestLag = lag; }
  }

  if (bestCorr < 0.25 || bestLag < 1) return -1;

  if (bestLag > 1 && bestLag < n / 2 - 1) {
    const ya = acAt(buf, n, bestLag - 1) / energy;
    const yb = bestCorr;
    const yc = acAt(buf, n, bestLag + 1) / energy;
    const d  = ya - 2 * yb + yc;
    if (d !== 0) return sampleRate / (bestLag - (yc - ya) / (2 * d));
  }
  return sampleRate / bestLag;
}

function acAt(buf, n, lag) {
  let c = 0;
  for (let i = 0; i < n - lag; i++) c += buf[i] * buf[i + lag];
  return c;
}
