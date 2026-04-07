import { OPENAI_KEY } from '../config.js';

export async function transcribeAudio(blob) {
  const form = new FormData();
  form.append('file', blob, 'recording.webm');
  form.append('model', 'whisper-1');
  form.append('response_format', 'verbose_json');
  form.append('timestamp_granularities[]', 'word');

  const res = await fetch('https://api.openai.com/v1/audio/transcriptions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${OPENAI_KEY}` },
    body: form,
  });
  if (!res.ok) throw new Error('Whisper error: ' + await res.text());
  const data = await res.json();
  return (data.words || [])
    .map(w => ({ word: w.word.trim(), start: w.start, end: w.end }))
    .filter(w => w.word.length > 0);
}
