// Read-only API audit with an in-process Worker and the existing C executable.
// No microphone, external model, login or deployment is used.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { handleRequest } from '../backend/src/worker.ts';
import { assertMusic, errorSchemaValid } from '../backend/src/validate.ts';
import { ASR_MODEL } from '../backend/src/asr.ts';
import { INTENT_MODEL } from '../backend/src/ai.ts';

const root = fileURLToPath(new URL('../', import.meta.url));
const artifacts = new URL('../artifacts/', import.meta.url);
mkdirSync(artifacts, { recursive: true });
const simulator = process.argv[2] ?? fileURLToPath(new URL('../build/simulator.exe', import.meta.url));
let problems = 0;
for (const [name, request_id, expectedStatus] of [
  ['ascii', 'review-ascii', 200],
  ['ascii-symbols', 'AZaz09._:-', 200],
  ['ascii-max', 'x'.repeat(64), 200],
  ['unicode', '任'.repeat(22), 400],
  ['nul', 'review\0id', 400],
  ['space', 'review id', 400],
  ['newline', 'review\n', 400],
  ['empty', '', 400],
  ['overlong', 'x'.repeat(65), 400],
]) {
  const response = await handleRequest(new Request('http://127.0.0.1/api/v1/generate-from-text', {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ text: '温暖', request_id }),
  }));
  const body = await response.text();
  const input = fileURLToPath(new URL(`review-${name}.json`, artifacts));
  const output = fileURLToPath(new URL(`review-${name}.wav`, artifacts));
  writeFileSync(input, body);
  const value = JSON.parse(body);
  if (response.status !== expectedStatus) {
    console.log(`${name}: expected HTTP ${expectedStatus}, got ${response.status}`);
    problems++;
    continue;
  }
  if (response.status !== 200) {
    console.log(`${name}: API rejected input (HTTP ${response.status})`);
    if (value.error?.code !== 'INVALID_REQUEST' || !errorSchemaValid(value) || value.request_id !== 'request') {
      console.log('Unexpected API failure; this does not resolve the contract mismatch.');
      problems++;
    }
    continue;
  }
  assertMusic(value);
  if (value.request_id !== request_id) problems++;
  const result = spawnSync(simulator, [input, output], { cwd: root, encoding: 'utf8' });
  if (result.error) throw result.error;
  console.log(`${name}: API HTTP ${response.status}; C simulator exit=${result.status}; ${(result.stdout + result.stderr).trim()}`);
  if (result.status !== 0) problems++;
}

// Synthetic PCM only checks the upload route; it is not speech/ASR evidence.
const wav = new Uint8Array(46);
const view = new DataView(wav.buffer);
const tag = (offset, text) => wav.set(new TextEncoder().encode(text), offset);
tag(0, 'RIFF'); view.setUint32(4, 38, true); tag(8, 'WAVEfmt ');
view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 1, true);
view.setUint32(24, 16000, true); view.setUint32(28, 32000, true);
view.setUint16(32, 2, true); view.setUint16(34, 16, true);
tag(36, 'data'); view.setUint32(40, 2, true); view.setInt16(44, 1000, true);
const form = new FormData();
form.set('audio', new Blob([wav], { type: 'audio/wav' }), 'probe.wav');
const audio = await handleRequest(new Request('http://127.0.0.1/api/v1/generate', {
  method: 'POST', body: form,
}));
const audioBody = await audio.json();
console.log(`audio route: HTTP ${audio.status}; ${JSON.stringify(audioBody)}`);
if (audio.status !== 503 || audioBody.error?.code !== 'ASR_UNAVAILABLE' || !errorSchemaValid(audioBody)) problems++;

// Offline ASR -> existing generators -> schema -> real C parser/renderer.
for (const mode of ['rules', 'ai']) {
  const calls = [];
  const transcript = '温暖轻快 120BPM 🎸';
  const AI = { async run(model, input) {
    calls.push(model);
    if (model === ASR_MODEL && calls.length === 1) {
      assert.deepEqual(Buffer.from(input.audio, 'base64'), Buffer.from(wav));
      return { text: transcript };
    }
    if (mode === 'ai' && model === INTENT_MODEL && calls.length === 2) {
      assert.equal(input.messages[1].content, transcript);
      return { response: { mood: 'bright', bpm: 96, root: 'C', mode: 'major', bars: 4, style: 'acoustic' } };
    }
    throw new Error('Unexpected extra model call');
  } };
  const body = new FormData();
  body.set('audio', new Blob([wav]), 'synthetic.wav');
  body.set('mode', mode);
  body.set('hint', '悲伤 40BPM');
  const response = await handleRequest(new Request('http://127.0.0.1/api/v1/generate', {
    method: 'POST', body, headers: { 'x-request-id': `review-asr-${mode}` },
  }), { AI });
  const music = await response.json();
  assert.equal(response.status, 200);
  assertMusic(music);
  assert.equal(music.transcript, transcript);
  assert.equal(music.mode, mode);
  assert.deepEqual(calls, mode === 'rules' ? [ASR_MODEL] : [ASR_MODEL, INTENT_MODEL]);
  function render(name, value, expectedExit = 0) {
    const input = fileURLToPath(new URL(`review-asr-${mode}-${name}.json`, artifacts));
    const output = fileURLToPath(new URL(`review-asr-${mode}-${name}.wav`, artifacts));
    writeFileSync(input, JSON.stringify(value));
    const result = spawnSync(simulator, [input, output], { cwd: root, encoding: 'utf8' });
    if (result.error) throw result.error;
    assert.equal(result.status, expectedExit, result.stdout + result.stderr);
    return expectedExit === 0 ? readFileSync(output) : undefined;
  }
  const rendered = render('transcript', music);
  const { transcript: ignored, ...withoutTranscript } = music;
  assert.deepEqual(render('no-transcript', withoutTranscript), rendered);
  // Unicode length is characters, not bytes; metadata cannot alter PCM.
  assert.deepEqual(render('unicode-limit', { ...music, transcript: '🎸'.repeat(500) }), rendered);
  for (const [name, transcript] of [['type', 1], ['empty', ''], ['long', '木'.repeat(501)], ['nul', 'a\0b']]) {
    render(`invalid-${name}`, { ...music, transcript }, 2);
  }
  console.log(`offline audio ${mode}: HTTP 200; ASR=1; LLM=${mode === 'ai' ? 1 : 0}; C exit=0; transcript ignored (PCM identical)`);
}
console.log('Real speech / Workers AI model calls: BLOCKED (offline fake clients only)');
console.log(`Cross-language contract problems found: ${problems}`);
// A nonzero code means a real outstanding contract mismatch was reproduced.
process.exitCode = problems ? 1 : 0;
