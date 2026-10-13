import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import Ajv2020 from 'ajv/dist/2020.js';
import addFormats from 'ajv-formats';
import worker, { handleRequest } from '../src/worker.ts';
import { defaultSeed, generateRules, xorshift32 } from '../src/rules.ts';
import { assertMusic } from '../src/validate.ts';
import { validateWav } from '../src/wav.ts';

const fixture = name => JSON.parse(readFileSync(new URL(`../../shared/fixtures/${name}.json`, import.meta.url), 'utf8'));
const ajv = new Ajv2020({ strict: true, allErrors: true });
addFormats(ajv);
const musicSchema = ajv.compile(JSON.parse(readFileSync(new URL('../../shared/music.schema.json', import.meta.url), 'utf8')));
const errorSchema = ajv.compile(JSON.parse(readFileSync(new URL('../../shared/error.schema.json', import.meta.url), 'utf8')));
const cases = [
  {
    text: '生成一段温暖、轻快、适合夏日海边的木吉他伴奏', hash: 691577472,
    key: { root: 'C', mode: 'major' }, bpm: 96, symbols: ['C', 'G', 'Am', 'F'],
    notes: [[48, 52, 55, 60], [43, 47, 50, 55], [45, 48, 52, 57], [41, 45, 48, 53]],
    velocity: 80, interval: 480, duration: 400,
  },
  {
    text: '来一段悲伤、慢的木吉他', hash: 3350900807,
    key: { root: 'A', mode: 'minor' }, bpm: 72, symbols: ['Am', 'G', 'F', 'Em'],
    notes: [[45, 48, 52, 57], [43, 47, 50, 55], [41, 45, 48, 53], [40, 47, 52, 55]],
    velocity: 64, interval: 480, duration: 400,
  },
  {
    text: '强烈、摇滚的木吉他伴奏', hash: 1415375574,
    key: { root: 'E', mode: 'minor' }, bpm: 128, symbols: ['Em', 'D', 'C', 'D'],
    notes: [[40, 47, 52, 59], [42, 45, 50, 54], [48, 52, 55, 60], [42, 45, 50, 54]],
    velocity: 112, interval: 240, duration: 200,
  },
  {
    text: '随便来一段伴奏', hash: 3245418374,
    key: { root: 'G', mode: 'major' }, bpm: 100, symbols: ['G', 'Em', 'C', 'D'],
    notes: [[43, 47, 50, 55], [40, 47, 52, 55], [48, 52, 55, 60], [50, 54, 57, 62]],
    velocity: 84, interval: 480, duration: 400,
  },
];

function post(body, headers = {}) {
  return new Request('http://127.0.0.1/api/v1/generate-from-text', {
    method: 'POST', headers: { 'content-type': 'application/json', ...headers }, body: JSON.stringify(body),
  });
}
async function expectError(request, code, status = 400, env = {}) {
  const response = await handleRequest(request, env);
  const body = await response.json();
  assert.equal(response.status, status);
  assert.ok(errorSchema(body), JSON.stringify(errorSchema.errors));
  assert.equal(body.error.code, code);
  return body;
}
function validMusic(value) {
  assert.ok(musicSchema(value), JSON.stringify(musicSchema.errors));
  assertMusic(value);
}
function expectMusicError(value) {
  assert.throws(() => assertMusic(value), error => error.code === 'INVALID_MUSIC_DATA');
}

for (const spec of cases) {
  test(`preset, exact voicings/ticks and HTTP: ${spec.text}`, async () => {
    const music = generateRules(Object.freeze({ text: spec.text, seed: 1 }));
    validMusic(music);
    assert.equal(music.seed, 1);
    assert.equal(music.mode, 'rules');
    assert.equal(music.bars, 4);
    assert.deepEqual(music.key, spec.key);
    assert.equal(music.bpm, spec.bpm);
    assert.deepEqual(music.chords.map(chord => chord.symbol), spec.symbols);
    assert.deepEqual(music.chords.map(chord => chord.midi_notes), spec.notes);
    assert.deepEqual(music.chords.map(chord => [chord.start_tick, chord.duration_tick]),
      [[0, 1920], [1920, 1920], [3840, 1920], [5760, 1920]]);
    const accompaniment = music.events.filter(event => event.part === 'accompaniment');
    let index = 0;
    for (let bar = 0; bar < 4; bar++) {
      for (let offset = 0; offset < 1920; offset += spec.interval) {
        for (let voice = 0; voice < 4; voice++) {
          const event = accompaniment[index++];
          assert.deepEqual([event.midi, event.velocity, event.start_tick, event.duration_tick],
            [spec.notes[bar][voice], spec.velocity, bar * 1920 + offset + voice * 10, spec.duration]);
        }
      }
    }
    assert.equal(accompaniment.length, index);
    const melody = music.events.filter(event => event.part === 'melody');
    assert.equal(melody.length, 16);
    for (let beat = 0; beat < 16; beat++) {
      const note = melody[beat];
      assert.equal(note.start_tick, beat * 480);
      assert.equal(note.duration_tick, 360);
      assert.equal(note.velocity, 88);
      assert.ok(note.midi >= 60 && note.midi <= 76);
      assert.ok(spec.notes[Math.floor(beat / 4)].some(midi => midi % 12 === note.midi % 12));
    }
    const first = await handleRequest(post({ text: spec.text, seed: 1 }));
    const second = await worker.fetch(post({ text: spec.text, seed: 1 }));
    assert.equal(first.status, 200);
    assert.equal(second.status, 200);
    const firstJson = await first.text();
    assert.equal(await second.text(), firstJson); // Includes every field, not just events.
    validMusic(JSON.parse(firstJson));
  });

  test(`FNV-1a decimal vector: ${spec.hash}`, async () => {
    assert.equal(defaultSeed(spec.text), spec.hash);
    assert.equal(defaultSeed(` \t\r\n${spec.text}\n\t `), spec.hash);
    assert.equal(generateRules({ text: spec.text }).seed, spec.hash);
    const response = await handleRequest(post({ text: spec.text }));
    assert.equal(response.status, 200);
    const music = await response.json();
    validMusic(music);
    assert.equal(music.seed, spec.hash);
  });
}

test('FNV hashes UTF-8 without Unicode normalization or broad whitespace trimming', () => {
  assert.equal(defaultSeed(''), 2166136261);
  assert.equal(defaultSeed('a'), 3826002220);
  assert.notEqual(defaultSeed('é'), defaultSeed('e\u0301'));
  assert.notEqual(defaultSeed('\u00a0a\u00a0'), defaultSeed('a'));
});

test('pure generator keeps independent RNG state and returns independent objects', () => {
  const input = Object.freeze({ text: cases[0].text, seed: 1, request_id: 'pure' });
  const first = generateRules(input);
  generateRules({ text: cases[2].text, seed: 9 });
  assert.equal(JSON.stringify(first), JSON.stringify(generateRules(input)));
  first.chords[0].midi_notes[0] = 99;
  first.key.root = 'B';
  assert.equal(generateRules(input).chords[0].midi_notes[0], 48);
  assert.deepEqual(generateRules(input).key, cases[0].key);
  assert.equal(xorshift32(1), 270369);
  assert.equal(xorshift32(270369), 67634689);
  assert.deepEqual(generateRules({ text: '伴奏', seed: 0 }).events,
    generateRules({ text: '伴奏', seed: 2164325893 }).events);
  assert.equal(generateRules({ text: '伴奏', seed: 0 }).seed, 0);
  assert.notDeepEqual(generateRules({ text: '伴奏', seed: 1 }).events,
    generateRules({ text: '伴奏', seed: 2 }).events);
});

test('keyword priority is rock > sad > bright > default', () => {
  for (const text of ['温暖悲伤强烈', '摇滚慢夏日', '夏日摇滚悲伤']) {
    assert.deepEqual(generateRules({ text }).key, cases[2].key);
  }
  for (const text of ['夏日忧伤', '温暖慢', '轻快悲伤']) {
    assert.deepEqual(generateRules({ text }).key, cases[1].key);
  }
  assert.deepEqual(generateRules({ text: '夏日' }).key, cases[0].key);
});

test('explicit BPM overrides only tempo; all allowed suffixes and endpoints', async () => {
  for (const suffix of ['BPM', 'bpm', '拍']) {
    for (const bpm of [40, 117, 220]) {
      const source = generateRules({ text: cases[0].text, seed: 1 });
      const changed = generateRules({ text: `${cases[0].text} ${bpm}${suffix}`, seed: 1 });
      assert.equal(changed.bpm, bpm);
      assert.deepEqual({ ...changed, bpm: source.bpm }, source);
      validMusic(changed);
    }
  }
  for (const text of ['10 BPM', '10bpm', '10拍', '221BPM', '0拍', '-96bpm', '96.5bpm']) {
    await expectError(post({ text }), 'INVALID_BPM');
  }
});

test('empty/missing/invalid/overlength text and invalid seeds are rejected', async () => {
  for (const text of ['', ' \t\r\n ', null, 123, '木'.repeat(501)]) {
    await expectError(post({ text }), 'INVALID_TEXT');
  }
  await expectError(post({}), 'INVALID_TEXT');
  const response = await handleRequest(post({ text: '木'.repeat(500), seed: 0xffffffff }));
  assert.equal(response.status, 200);
  validMusic(await response.json());
  for (const seed of [-1, 0x100000000, 1.2, '1', null]) {
    await expectError(post({ text: '伴奏', seed }), 'INVALID_REQUEST');
  }
});

test('schema and cross-field fixtures: valid, BPM, MIDI, version, overflow, nine voices', () => {
  validMusic(fixture('valid-one-bar'));
  for (const name of ['invalid-bpm', 'invalid-midi', 'invalid-version', 'invalid-overflow-time', 'invalid-polyphony']) {
    expectMusicError(fixture(name));
  }
  const nine = fixture('invalid-polyphony');
  assert.ok(musicSchema(nine)); // Schema alone cannot detect this.
  for (const event of nine.events) event.part = 'melody'; // Isolate polyphony from off-chord pitches.
  expectMusicError(nine);
  nine.events.pop();
  validMusic(nine);
  const next = structuredClone(nine.events);
  for (const event of next) { event.id += 'b'; event.start_tick = 200; }
  nine.events.push(...next);
  nine.events.reverse();
  validMusic(nine); // Eight end and eight begin on the same tick.
});

test('cross-fields reject duplicate IDs, invalid spans, chord gaps and off-chord accompaniment', () => {
  const mutations = [
    value => { value.events[1].id = value.events[0].id; },
    value => { value.events[0].duration_tick = 1921; },
    value => { value.chords[0].duration_tick = 1921; },
    value => { value.events[0].midi = 49; },
    value => { value.chords[0].start_tick = 1; value.chords[0].duration_tick = 1919; },
    value => { value.chords[0].duration_tick = 100; },
  ];
  for (const mutate of mutations) {
    const value = fixture('valid-one-bar');
    mutate(value);
    expectMusicError(value);
  }
  const value = fixture('valid-one-bar');
  value.events = [{ ...value.events[0], duration_tick: 400 }];
  value.chords[0].duration_tick = 200;
  value.chords.push({ ...value.chords[0], start_tick: 200, midi_notes: [49], symbol: 'Db' });
  expectMusicError(value); // Starts in C but crosses to a different pitch class.
  value.chords[1].midi_notes = [60];
  validMusic(value); // Same pitch class across octave/chord boundary.
  value.events[0].part = 'melody';
  value.events[0].midi = 61;
  validMusic(value); // Protocol permits off-chord melody pitches.
});

test('request_id echo on success and errors, header fallback and validation', async () => {
  const response = await handleRequest(post({ text: '伴奏', seed: 1, request_id: 'echo-123' }));
  const music = await response.json();
  validMusic(music);
  assert.equal(music.request_id, 'echo-123');
  const error = await expectError(post({ text: '', request_id: 'error-456' }), 'INVALID_TEXT');
  assert.equal(error.request_id, 'error-456');
  const header = await expectError(post({ text: '10bpm' }, { 'x-request-id': 'header-789' }), 'INVALID_BPM');
  assert.equal(header.request_id, 'header-789');
  for (const request_id of ['', 'a'.repeat(65), 1, null]) {
    await expectError(post({ text: '伴奏', request_id }), 'INVALID_REQUEST');
  }
});

test('ASCII request_id contract rejects review regressions before success and keeps errors valid', async () => {
  // Exact failure samples from submission-review-20261013.md, plus whitespace boundaries.
  const invalid = ['任'.repeat(22), 'review\0id', 'review id', 'id\n', 'id\r', 'id\u2028', 'id\u2029'];
  for (const request_id of invalid) {
    const error = await expectError(post({ text: '伴奏', request_id }), 'INVALID_REQUEST', 400);
    assert.equal(error.request_id, 'request');
    const withHeader = await expectError(post({ text: '伴奏', request_id }, { 'x-request-id': 'safe:header' }), 'INVALID_REQUEST', 400);
    assert.equal(withHeader.request_id, 'safe:header');
    assert.throws(() => generateRules({ text: '伴奏', request_id }), /request_id/);
    const music = { ...generateRules({ text: '伴奏' }), request_id };
    assert.equal(musicSchema(music), false);
    assert.equal(errorSchema({ ...error, request_id }), false);
  }
  // Headers cannot represent Unicode/NUL, but an internal space is valid HTTP syntax.
  const badHeader = await expectError(post({ text: '伴奏', request_id: 'valid-body' }, { 'x-request-id': 'bad id' }), 'INVALID_REQUEST', 400);
  assert.equal(badHeader.request_id, 'request');
  for (const request_id of ['A', 'AZaz09._:-', 'x'.repeat(64)]) {
    const response = await handleRequest(post({ text: '伴奏', request_id }));
    assert.equal(response.status, 200);
    const music = await response.json();
    validMusic(music);
    assert.equal(music.request_id, request_id);
    const error = await expectError(post({ text: '', request_id }), 'INVALID_TEXT');
    assert.equal(error.request_id, request_id);
  }
});

test('health/info/rules/mock never call network/model; missing or inaccessible AI unavailable', async () => {
  const previousFetch = globalThis.fetch;
  globalThis.fetch = () => { throw new Error('Network/model call forbidden'); };
  const env = { AI: new Proxy({}, { get() { throw new Error('AI binding accessed'); } }) };
  try {
    const health = await worker.fetch(new Request('http://127.0.0.1/health'), env);
    assert.deepEqual(await health.json(), { ok: true, protocol_version: '1.0' });
    const info = await (await handleRequest(new Request('http://127.0.0.1/api/v1/info'), env)).json();
    assert.equal(info.protocol_version, '1.0');
    assert.equal(info.rules_available, true);
    assert.equal(info.ai_configured, true);
    assert.equal(info.auth_required, false);
    assert.deepEqual(info.recording_format, { container: 'WAV', encoding: 'PCM', sample_rate: 16000, channels: 1, bits_per_sample: 16, max_seconds: 4 });
    assert.equal(info.synthesis_format.sample_rate, 32000);
    assert.equal(info.synthesis_format.channels, 1);
    assert.equal(info.synthesis_format.bits_per_sample, 16);
    assert.equal((await (await handleRequest(new Request('http://127.0.0.1/api/v1/info'))).json()).ai_configured, false);
    validMusic(await (await handleRequest(post({ text: '伴奏' }), env)).json());
    for (const configured of [{}, env]) {
      const error = await expectError(post({ text: '伴奏', mode: 'ai', request_id: 'ai-echo' }), 'AI_UNAVAILABLE', 503, configured);
      assert.equal(error.request_id, 'ai-echo');
    }
    await expectError(post({ text: '伴奏', mode: 'mock' }), 'INVALID_REQUEST');
    const mock = await (await handleRequest(post({ text: '伴奏', mode: 'mock' }), { ...env, ALLOW_MOCK: '1' })).json();
    validMusic(mock);
    assert.equal(mock.mode, 'mock');
    assert.equal((await (await handleRequest(post({ text: '伴奏' }), { ALLOW_MOCK: '1' })).json()).mode, 'rules');
    await expectError(post({ text: '伴奏', mode: 'other' }), 'INVALID_REQUEST');
  } finally { globalThis.fetch = previousFetch; }
});

function wav(samples = [0, 1, -1, 0]) {
  const bytes = new Uint8Array(44 + samples.length * 2);
  const view = new DataView(bytes.buffer);
  const tag = (offset, text) => bytes.set(new TextEncoder().encode(text), offset);
  tag(0, 'RIFF'); view.setUint32(4, bytes.length - 8, true); tag(8, 'WAVE');
  tag(12, 'fmt '); view.setUint32(16, 16, true); view.setUint16(20, 1, true);
  view.setUint16(22, 1, true); view.setUint32(24, 16000, true); view.setUint32(28, 32000, true);
  view.setUint16(32, 2, true); view.setUint16(34, 16, true); tag(36, 'data');
  view.setUint32(40, samples.length * 2, true);
  samples.forEach((sample, i) => view.setInt16(44 + i * 2, sample, true));
  return bytes;
}
function upload(bytes, { name = 'audio', filename = 'recording.wav', hint = '温暖', headers = {} } = {}) {
  const form = new FormData();
  form.append(name, new Blob([bytes], { type: 'audio/wav' }), filename);
  if (hint !== undefined) form.append('hint', hint);
  return new Request('http://127.0.0.1/api/v1/generate', { method: 'POST', body: form, headers });
}

test('valid WAV (including 4-second boundary) reaches ASR_UNAVAILABLE, regardless of extension', async () => {
  for (const bytes of [wav(), wav(Array.from({ length: 64000 }, (_, i) => i === 0 ? 1 : 0))]) {
    validateWav(bytes);
    const error = await expectError(upload(bytes, { filename: 'actually-not-a-wav.txt', headers: { 'x-request-id': 'audio-echo' } }), 'ASR_UNAVAILABLE', 503);
    assert.equal(error.request_id, 'audio-echo');
  }
  // An odd-length unknown RIFF chunk requires a padding byte.
  const original = wav();
  const ancillary = new Uint8Array(original.length + 10);
  ancillary.set(original.subarray(0, 12));
  ancillary.set(new TextEncoder().encode('JUNK'), 12);
  new DataView(ancillary.buffer).setUint32(16, 1, true);
  ancillary[20] = 42;
  ancillary.set(original.subarray(12), 22);
  new DataView(ancillary.buffer).setUint32(4, ancillary.length - 8, true);
  validateWav(ancillary);
  await expectError(upload(ancillary), 'ASR_UNAVAILABLE', 503);
});

test('WAV content validation rejects corrupt headers, wrong formats, silence, empty/long/truncated data', async () => {
  const broken = [new Uint8Array(), new TextEncoder().encode('fake WAV'), wav([]), wav([0, 0]), wav(Array(64001).fill(1))];
  const fields = [[0, 0, 4], [4, 9999, 4], [8, 0, 4], [16, 15, 4], [20, 3, 2],
    [22, 2, 2], [24, 32000, 4], [28, 64000, 4], [32, 4, 2], [34, 8, 2], [40, 9999, 4], [40, 3, 4]];
  for (const [offset, value, width] of fields) {
    const bytes = wav();
    const view = new DataView(bytes.buffer);
    if (width === 2) view.setUint16(offset, value, true); else view.setUint32(offset, value, true);
    broken.push(bytes);
  }
  const truncated = wav().slice(0, -2);
  new DataView(truncated.buffer).setUint32(4, truncated.length - 8, true);
  broken.push(truncated);
  const noData = wav().slice(0, 36);
  new DataView(noData.buffer).setUint32(4, noData.length - 8, true);
  broken.push(noData);
  for (const bytes of broken) {
    assert.throws(() => validateWav(bytes), error => error.code === 'INVALID_AUDIO');
    await expectError(upload(bytes), 'INVALID_AUDIO');
  }
  const form = new FormData(); form.append('audio', 'not a file');
  await expectError(new Request('http://127.0.0.1/api/v1/generate', { method: 'POST', body: form }), 'INVALID_AUDIO');
  await expectError(upload(wav(), { name: 'wrong-field' }), 'INVALID_REQUEST');
});

test('body limits apply to actual streaming JSON/multipart bytes, not just Content-Length', async () => {
  await expectError(post({ text: '伴奏' }, { 'content-length': '1000001' }), 'REQUEST_TOO_LARGE', 413);
  await expectError(post({ text: 'a'.repeat(1_000_001) }), 'REQUEST_TOO_LARGE', 413);
  // Materialize Node's multipart encoder first. Cancelling its live generator
  // can enqueue/close after cancellation in Undici; real incoming HTTP bodies
  // are byte streams, not Node's outgoing FormData generator.
  const multipart = upload(new Uint8Array(1_000_001));
  await expectError(new Request(multipart.url, {
    method: 'POST', headers: multipart.headers, body: await multipart.arrayBuffer(),
  }), 'REQUEST_TOO_LARGE', 413);
  const stream = new ReadableStream({ start(controller) {
    controller.enqueue(new Uint8Array(600000)); controller.enqueue(new Uint8Array(400001)); controller.close();
  } });
  await expectError(new Request('http://127.0.0.1/api/v1/generate-from-text', {
    method: 'POST', headers: { 'content-type': 'application/json', 'content-length': '1' }, body: stream, duplex: 'half',
  }), 'REQUEST_TOO_LARGE', 413);
});

test('auth defaults off; explicit AUTH_REQUIRED=1 checks bearer without exposing token', async () => {
  const env = { AUTH_REQUIRED: '1', DEVICE_TOKEN: 'test-only-token' };
  for (const headers of [{}, { authorization: 'Bearer wrong' }]) {
    const body = await expectError(post({ text: '伴奏' }, headers), 'UNAUTHORIZED', 401, env);
    assert.ok(!JSON.stringify(body).includes(env.DEVICE_TOKEN));
    assert.ok(!JSON.stringify(body).includes('stack'));
  }
  await expectError(post({ text: '伴奏' }), 'UNAUTHORIZED', 401, { AUTH_REQUIRED: '1' });
  validMusic(await (await handleRequest(post({ text: '伴奏' }, { authorization: 'Bearer test-only-token' }), env)).json());
  await expectError(upload(wav()), 'UNAUTHORIZED', 401, env);
  const info = await (await handleRequest(new Request('http://127.0.0.1/api/v1/info'), env)).json();
  assert.equal(info.auth_required, true);
});

test('malformed JSON, content types, multipart, field/mode types and methods return schema-valid errors', async () => {
  for (const body of ['{', '[]', 'null']) {
    await expectError(new Request('http://127.0.0.1/api/v1/generate-from-text', {
      method: 'POST', headers: { 'content-type': 'application/json' }, body,
    }), 'INVALID_REQUEST');
  }
  await expectError(post({ text: '伴奏', extra: true }), 'INVALID_REQUEST');
  await expectError(post({ text: '伴奏', mode: 1 }), 'INVALID_REQUEST');
  await expectError(post({ text: '伴奏', mode: null }), 'INVALID_REQUEST');
  await expectError(new Request('http://127.0.0.1/api/v1/generate-from-text', { method: 'POST', body: '{}' }), 'INVALID_REQUEST');
  await expectError(new Request('http://127.0.0.1/api/v1/generate', {
    method: 'POST', headers: { 'content-type': 'multipart/form-data; boundary=broken' }, body: 'bad multipart',
  }), 'INVALID_REQUEST');
  await expectError(new Request('http://127.0.0.1/unknown'), 'INVALID_REQUEST');
  await expectError(new Request('http://127.0.0.1/api/v1/generate-from-text'), 'INVALID_REQUEST');
});
