// Explicit opt-in live verification: consumes a small, finite number of AI calls.
// Requires MUSIC_BACKEND_URL and MUSIC_DEVICE_TOKEN; never logs credentials.
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { assertMusic, errorSchemaValid } from '../backend/src/validate.ts';
import { validateWav } from '../backend/src/wav.ts';
import { generateRules } from '../backend/src/rules.ts';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const destination = join(root, 'artifacts/cloud');
const token = process.env.MUSIC_DEVICE_TOKEN;
const base = process.env.MUSIC_BACKEND_URL;
if (!base || !token) throw new Error('Set MUSIC_BACKEND_URL and MUSIC_DEVICE_TOKEN before explicit live verification.');
const url = new URL(base);
assert.equal(url.protocol, 'https:');
assert.equal(url.pathname, '/');
assert.ok(!url.username && !url.password && !url.search && !url.hash);
const simulator = ['build/simulator.exe', 'build/Release/simulator.exe', 'build/Debug/simulator.exe']
  .map(path => join(root, path)).find(existsSync);
assert.ok(simulator, 'Build the shared C simulator first.');
await mkdir(join(destination, 'results'), { recursive: true });
const summary = { started_at: new Date().toISOString(), backend_url: url.origin,
  scope: 'Actual HTTPS and Workers AI; speech fixtures are Windows TTS, not human or ESP32 microphone recordings.',
  checks: [], music_cases: [], status: 'RUNNING' };
const save = () => writeFile(join(destination, 'verification.json'), JSON.stringify(summary, null, 2) + '\n');
await save();
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
let sequence = 0;
async function request(name, route, body, contentType, authorization = `Bearer ${token}`) {
  const requestId = `cloud-${Date.now()}-${++sequence}`;
  const headers = { 'x-request-id': requestId };
  if (contentType) headers['content-type'] = contentType;
  if (authorization) headers.authorization = authorization;
  const started = performance.now();
  const response = await fetch(new URL(route, url), { method: body === undefined ? 'GET' : 'POST',
    headers, body, redirect: 'error', signal: AbortSignal.timeout(120000) });
  const value = await response.json();
  const elapsed_ms = Math.round(performance.now() - started);
  return { name, response, value, elapsed_ms, requestId };
}
async function errorCase(name, body, contentType, status, code, authorization) {
  const result = await request(name, '/api/v1/generate', body, contentType, authorization);
  assert.equal(result.response.status, status, name);
  assert.ok(errorSchemaValid(result.value), `${name}: invalid error schema`);
  assert.equal(result.value.error.code, code, name);
  summary.checks.push({ name, http_status: status, error_code: code, elapsed_ms: result.elapsed_ms, status: 'PASS' });
  console.log(`PASS ${name}: HTTP ${status}/${code}`);
  await save();
}
async function musicCase(name, mode, input, expected) {
  let body, route, contentType;
  if (typeof input === 'string') {
    body = JSON.stringify({ text: input, mode }); route = '/api/v1/generate-from-text'; contentType = 'application/json';
  } else {
    validateWav(input);
    body = new FormData(); body.set('audio', new Blob([input], { type: 'audio/wav' }), `${name}.wav`);
    body.set('mode', mode); route = '/api/v1/generate';
  }
  const result = await request(name, route, body, contentType);
  const music = result.value;
  assert.equal(result.response.status, 200, `${name}: HTTP ${result.response.status}/${music?.error?.code ?? 'unknown'}`);
  assertMusic(music);
  assert.equal(music.request_id, result.requestId);
  assert.equal(music.mode, mode);
  if (typeof input !== 'string') assert.ok(music.transcript?.length, `${name}: missing actual transcript`);
  if (expected) {
    assert.equal(music.bpm, expected.bpm, name);
    assert.equal(music.key.root, expected.root, name);
    assert.equal(music.key.mode, expected.keyMode, name);
    assert.deepEqual(music.chords.map(chord => chord.symbol), expected.chords, name);
  }
  if (mode === 'rules') {
    const rules = generateRules({ text: typeof input === 'string' ? input : music.transcript, request_id: result.requestId });
    const { transcript, ...withoutTranscript } = music;
    assert.deepEqual(withoutTranscript, rules, `${name}: rules differ from actual recognized text`);
  }
  const jsonName = `results/${name}.json`, wavName = `results/${name}.wav`;
  const jsonPath = join(destination, jsonName), wavPath = join(destination, wavName);
  await writeFile(jsonPath, JSON.stringify(music, null, 2) + '\n');
  const rendered = spawnSync(simulator, [jsonPath, wavPath], { encoding: 'utf8', windowsHide: true });
  assert.equal(rendered.status, 0, `${name}: C parser/render failed`);
  const wav = await readFile(wavPath);
  assert.equal(wav.toString('ascii', 0, 4), 'RIFF'); assert.equal(wav.toString('ascii', 8, 12), 'WAVE');
  assert.equal(wav.readUInt16LE(20), 1); assert.equal(wav.readUInt16LE(22), 1);
  assert.equal(wav.readUInt32LE(24), 32000); assert.equal(wav.readUInt16LE(34), 16);
  const samples = Math.round(music.bars * 4 * 32000 * 60 / music.bpm);
  assert.equal(wav.length, 44 + samples * 2);
  let nonzero = 0, peak = 0, squares = 0;
  for (let offset = 44; offset < wav.length; offset += 2) {
    const value = wav.readInt16LE(offset); if (value) nonzero++;
    peak = Math.max(peak, Math.abs(value)); squares += value * value;
  }
  assert.ok(nonzero > samples / 2 && peak > 0, `${name}: silent/invalid PCM`);
  summary.music_cases.push({ name, mode, input_kind: typeof input === 'string' ? 'text' : 'Windows TTS WAV',
    input_text: typeof input === 'string' ? input : undefined, transcript: music.transcript,
    request_id: result.requestId, http_status: 200, elapsed_ms: result.elapsed_ms,
    bpm: music.bpm, key: music.key, chords: music.chords.map(chord => chord.symbol),
    events: music.events.length, seed: music.seed, json: jsonName, wav: wavName,
    pcm: { sample_rate: 32000, channels: 1, bits: 16, samples, nonzero, peak, rms: Math.sqrt(squares / samples) },
    wav_sha256: sha(wav), status: 'PASS' });
  console.log(`PASS ${name}: HTTP 200; ${music.bpm} BPM; ${music.key.root} ${music.key.mode}; C KS -> ${samples} PCM samples`);
  if (music.transcript) console.log(`  actual ASR transcript: ${music.transcript}`);
  await save();
}
try {
  for (const path of ['/health', '/api/v1/info']) {
    const result = await request(path, path);
    assert.equal(result.response.status, 200); assert.equal(result.value.ok, true);
    if (path.endsWith('info')) { assert.equal(result.value.ai_configured, true); assert.equal(result.value.auth_required, true); }
    summary.checks.push({ name: path, http_status: 200, status: 'PASS', value: result.value });
    console.log(`PASS ${path}`);
  }
  await errorCase('missing-token', '{}', 'application/json', 401, 'UNAUTHORIZED', '');
  await errorCase('wrong-token', '{}', 'application/json', 401, 'UNAUTHORIZED', 'Bearer deliberately-invalid');
  const invalid = new FormData(); invalid.set('audio', new Blob(['not WAV']), 'invalid.wav');
  await errorCase('invalid-audio', invalid, undefined, 400, 'INVALID_AUDIO');
  await errorCase('oversize-request', 'x'.repeat(1000001), 'application/octet-stream', 413, 'REQUEST_TOO_LARGE');
  const profiles = [
    { name: 'warm', text: '温暖轻快的吉他伴奏', bpm: 96, root: 'C', keyMode: 'major', chords: ['C', 'G', 'Am', 'F'] },
    { name: 'sad', text: '悲伤慢的吉他伴奏', bpm: 72, root: 'A', keyMode: 'minor', chords: ['Am', 'G', 'F', 'Em'] },
    { name: 'rock', text: '摇滚吉他伴奏 120BPM', bpm: 120, root: 'E', keyMode: 'minor', chords: ['Em', 'D', 'C', 'D'] },
  ];
  for (const profile of profiles) {
    await musicCase(`text-rules-${profile.name}`, 'rules', profile.text, profile);
    await musicCase(`text-ai-${profile.name}`, 'ai', profile.text, profile);
  }
  for (const profile of profiles) {
    const audio = await readFile(join(destination, `inputs/tts-${profile.name}.wav`));
    await musicCase(`audio-rules-${profile.name}`, 'rules', audio);
    await musicCase(`audio-ai-${profile.name}`, 'ai', audio);
  }
  summary.status = 'PASS'; summary.completed_at = new Date().toISOString(); await save();
  console.log(`Cloud verification PASS: ${summary.checks.length} protocol/security checks; ${summary.music_cases.length} live music cases.`);
} catch (error) {
  summary.status = 'FAIL'; summary.failure = error instanceof TypeError ? 'Network/response failure' : error.message;
  summary.completed_at = new Date().toISOString(); await save();
  console.error(`Cloud verification FAIL: ${summary.failure}`); process.exitCode = 1;
}
