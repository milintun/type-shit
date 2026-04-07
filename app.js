import { transcribeAudio }      from './pipeline/transcribe.js';
import { extractAudioFeatures }  from './pipeline/audioFeatures.js';
import { computeDerivedFields, buildManifest } from './pipeline/manifest.js';
import { layoutWords }           from './pipeline/layout.js';
import { runWordAgent, defaultWordOutput } from './pipeline/wordAgent.js';
import { renderPoster }          from './render/render.js';

// ── Phase management ──────────────────────────────────────────────────────────
const PHASES = ['phase-record', 'phase-process', 'phase-output'];
function showPhase(id) {
  PHASES.forEach(p => { document.getElementById(p).hidden = p !== id; });
}

// ── Recording state ───────────────────────────────────────────────────────────
let mediaRecorder   = null;
let audioChunks     = [];
let recordingStream = null;
let audioCtx        = null;
let analyserNode    = null;
let meterRafId      = null;
let timerInterval   = null;
let startTime       = null;
const MAX_MS        = 10000;

// ── Live level meter ──────────────────────────────────────────────────────────
function startMeter(stream) {
  audioCtx = new AudioContext();
  const src = audioCtx.createMediaStreamSource(stream);
  analyserNode = audioCtx.createAnalyser();
  analyserNode.fftSize = 1024;
  src.connect(analyserNode);

  const buf  = new Uint8Array(analyserNode.fftSize);
  const fill = document.getElementById('meter-fill');
  function tick() {
    meterRafId = requestAnimationFrame(tick);
    analyserNode.getByteTimeDomainData(buf);
    let sum = 0;
    for (let i = 0; i < buf.length; i++) { const v = (buf[i] - 128) / 128; sum += v * v; }
    fill.style.width = Math.min(100, Math.sqrt(sum / buf.length) * 300) + '%';
  }
  tick();
}

function stopMeter() {
  cancelAnimationFrame(meterRafId);
  document.getElementById('meter-fill').style.width = '0%';
}

// ── Timer ─────────────────────────────────────────────────────────────────────
function startTimer() {
  startTime = Date.now();
  const el = document.getElementById('timer-display');
  timerInterval = setInterval(() => {
    el.textContent = ((Date.now() - startTime) / 1000).toFixed(1) + 's';
  }, 100);
}

function stopTimer() { clearInterval(timerInterval); }

// ── Record start/stop ─────────────────────────────────────────────────────────
async function startRecording() {
  audioChunks = [];
  try {
    recordingStream = await navigator.mediaDevices.getUserMedia({ audio: true });
  } catch {
    showError('Microphone access denied. Allow mic access and reload.');
    return;
  }

  startMeter(recordingStream);
  startTimer();

  mediaRecorder = new MediaRecorder(recordingStream);
  mediaRecorder.ondataavailable = e => { if (e.data.size > 0) audioChunks.push(e.data); };
  mediaRecorder.onstop = onRecordingStop;
  mediaRecorder.start(100);

  setTimeout(() => { if (mediaRecorder?.state === 'recording') stopRecording(); }, MAX_MS);

  const btn = document.getElementById('record-btn');
  btn.textContent = '■ \u00a0Stop';
  btn.classList.add('recording');
}

function stopRecording() {
  if (!mediaRecorder || mediaRecorder.state === 'inactive') return;
  mediaRecorder.stop();
  recordingStream.getTracks().forEach(t => t.stop());
  stopTimer();
  stopMeter();
}

document.getElementById('record-btn').addEventListener('click', () => {
  if (!mediaRecorder || mediaRecorder.state === 'inactive') startRecording();
  else stopRecording();
});

// ── Pipeline entry ────────────────────────────────────────────────────────────
async function onRecordingStop() {
  const blob = new Blob(audioChunks, { type: 'audio/webm' });
  showPhase('phase-process');
  try {
    await runPipeline(blob);
  } catch (err) {
    console.error(err);
    showError(err.message);
    setTimeout(resetToRecord, 4000);
  }
}

// ── Step indicators ───────────────────────────────────────────────────────────
const STEP_DEFS = [
  { id: 'transcribe', label: 'Transcribing'  },
  { id: 'audio',      label: 'Audio features' },
  { id: 'agent',      label: 'Word agent'    },
];

function initSteps() {
  document.getElementById('process-steps').innerHTML =
    STEP_DEFS.map(s =>
      `<div class="step" id="step-${s.id}"><span class="step-icon">○</span>${s.label}</div>`
    ).join('');
}

function stepActive(id) {
  const el = document.getElementById('step-' + id);
  if (!el) return;
  el.classList.add('active');
  el.querySelector('.step-icon').textContent = '◉';
}

function stepDone(id) {
  const el = document.getElementById('step-' + id);
  if (!el) return;
  el.classList.remove('active');
  el.classList.add('done');
  el.querySelector('.step-icon').textContent = '✓';
}

// ── Full pipeline ─────────────────────────────────────────────────────────────
async function runPipeline(blob) {
  initSteps();

  stepActive('transcribe');
  const wordTimestamps = await transcribeAudio(blob);
  if (!wordTimestamps.length) throw new Error('No words detected in recording.');
  stepDone('transcribe');

  stepActive('audio');
  const arrayBuffer      = await blob.arrayBuffer();
  if (audioCtx.state === 'suspended') await audioCtx.resume();
  const audioBuffer      = await audioCtx.decodeAudioData(arrayBuffer);
  const wordsWithAudio   = extractAudioFeatures(audioBuffer, wordTimestamps);
  const wordsWithDerived = computeDerivedFields(wordsWithAudio);
  stepDone('audio');

  // Single Claude call: tag + font selection in one shot
  stepActive('agent');
  const agentOutputs   = await runWordAgent(wordsWithDerived);
  const manifest       = buildManifest(wordsWithDerived, agentOutputs);
  const layoutManifest = layoutWords(manifest);
  const withFont       = layoutManifest.map((w, i) => ({
    ...w,
    fontOutput: agentOutputs[i] || defaultWordOutput(w.word),
  }));
  stepDone('agent');

  // Show the output canvas before rendering so it's in the layout tree —
  // getComputedStyle needs the canvas visible for font-variation-settings to resolve.
  showPhase('phase-output');
  await renderPoster(withFont);
}

// ── Output controls ───────────────────────────────────────────────────────────
document.getElementById('download-btn').addEventListener('click', () => {
  const c = document.getElementById('poster-canvas');
  const a = document.createElement('a');
  a.download = 'voice-poster.png';
  a.href     = c.toDataURL('image/png');
  a.click();
});

document.getElementById('record-again-btn').addEventListener('click', resetToRecord);

function resetToRecord() {
  const btn = document.getElementById('record-btn');
  btn.innerHTML = '● &nbsp;Record';
  btn.classList.remove('recording');
  document.getElementById('timer-display').textContent = '';
  mediaRecorder = null;
  showPhase('phase-record');
}

// ── Error banner ──────────────────────────────────────────────────────────────
function showError(msg) {
  const el = document.getElementById('error-banner');
  el.textContent = msg;
  el.style.display = 'block';
  setTimeout(() => { el.style.display = 'none'; }, 7000);
}
