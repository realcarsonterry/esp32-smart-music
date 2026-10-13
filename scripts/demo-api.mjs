// Explicitly connects to the supplied API. No login, deployment, recording or retry.
// Run with node --import tsx scripts/demo-api.mjs --url https://... --text '温暖轻快'
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { parseArgs } from 'node:util';
import { assertMusic } from '../backend/src/validate.ts';
import { validateWav } from '../backend/src/wav.ts';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
try {
  const { values: args } = parseArgs({ options: {
    url: { type: 'string' }, text: { type: 'string' }, audio: { type: 'string' },
    mode: { type: 'string', default: 'rules' }, simulator: { type: 'string' },
  } });
  if (!args.url || Boolean(args.text) === Boolean(args.audio)) {
    throw new Error('Supply --url and exactly one of --text or --audio (PCM16 mono 16kHz WAV, <=4s).');
  }
  const base = new URL(args.url);
  if (base.username || base.password || base.search || base.hash || base.pathname !== '/') {
    throw new Error('--url must be a base origin without credentials, path, query or fragment.');
  }
  if (base.protocol !== 'https:' && !(base.protocol === 'http:' && ['127.0.0.1', 'localhost'].includes(base.hostname))) {
    throw new Error('Use HTTPS, or loopback HTTP for local verification.');
  }
  if (!['rules', 'ai'].includes(args.mode)) throw new Error('--mode must be rules or ai.');
  const simulator = args.simulator ? resolve(args.simulator) : [
    'build/simulator.exe', 'build/Release/simulator.exe', 'build/Debug/simulator.exe',
  ].map(path => join(root, path)).find(existsSync);
  if (!simulator || !existsSync(simulator)) throw new Error('Build the C simulator first; or supply --simulator.');
  const requestId = `pc-${crypto.randomUUID()}`;
  const headers = { 'x-request-id': requestId };
  if (process.env.MUSIC_DEVICE_TOKEN) headers.authorization = `Bearer ${process.env.MUSIC_DEVICE_TOKEN}`;
  let body, route;
  if (args.audio) {
    const bytes = await readFile(resolve(args.audio));
    validateWav(bytes); // Do not upload malformed files or pretend synthesized music is speech.
    body = new FormData();
    body.set('audio', new Blob([bytes], { type: 'audio/wav' }), 'recording.wav');
    body.set('mode', args.mode);
    route = '/api/v1/generate';
  } else {
    body = JSON.stringify({ text: args.text, mode: args.mode, request_id: requestId });
    headers['content-type'] = 'application/json';
    route = '/api/v1/generate-from-text';
  }
  const response = await fetch(new URL(route, base), {
    method: 'POST', headers, body, redirect: 'error', signal: AbortSignal.timeout(90000),
  });
  const value = await response.json();
  if (!response.ok) {
    // Print only the known error code; never log headers, secrets or arbitrary response text.
    const code = typeof value?.error?.code === 'string' && /^[A-Z_]{1,64}$/.test(value.error.code)
      ? value.error.code : 'UNKNOWN_ERROR';
    throw new Error(`API HTTP ${response.status}: ${code}`);
  }
  assertMusic(value);
  if (value.request_id !== requestId || value.mode !== args.mode) throw new Error('Response identity or mode mismatch.');
  const artifacts = join(root, 'artifacts');
  await mkdir(artifacts, { recursive: true });
  const jsonPath = join(artifacts, 'api_music.json'), wavPath = join(artifacts, 'api_music.wav');
  await writeFile(jsonPath, JSON.stringify(value, null, 2) + '\n', 'utf8');
  const rendered = spawnSync(simulator, [jsonPath, wavPath], { encoding: 'utf8', windowsHide: true });
  if (rendered.error || rendered.status !== 0) throw new Error(`C simulator failed: ${rendered.status ?? 'launch error'}`);
  console.log(`HTTP ${response.status}; mode=${value.mode}; BPM=${value.bpm}; key=${value.key.root} ${value.key.mode}`);
  console.log(`chords=${value.chords.map(chord => chord.symbol).join('/')}`);
  if (value.transcript) console.log(`transcript=${value.transcript}`);
  console.log(`JSON: ${jsonPath}\nWAV: ${wavPath}`);
  console.log('API -> shared C KS -> WAV: PASS (physical ESP32 not tested).');
} catch (error) {
  // Transport exception messages may contain URLs. Avoid exposing those by default.
  console.error(error instanceof TypeError ? 'Network or response parsing failed; check API availability.' : error.message);
  process.exitCode = 1;
}
