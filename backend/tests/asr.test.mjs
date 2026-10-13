import assert from 'node:assert/strict';
import test from 'node:test';
import { ASR_MODEL } from '../src/asr.ts';
import { INTENT_MODEL } from '../src/ai.ts';
import { generateRules } from '../src/rules.ts';
import { handleRequest } from '../src/worker.ts';
import { assertMusic, errorSchemaValid } from '../src/validate.ts';

// Synthetic PCM is a transport fixture, never real speech evidence.
function wav(samples = 64000) {
  const bytes = new Uint8Array(44 + samples * 2);
  const view = new DataView(bytes.buffer);
  const tag = (offset, value) => bytes.set(new TextEncoder().encode(value), offset);
  tag(0, 'RIFF'); view.setUint32(4, bytes.length - 8, true); tag(8, 'WAVEfmt ');
  view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 1, true);
  view.setUint32(24, 16000, true); view.setUint32(28, 32000, true);
  view.setUint16(32, 2, true); view.setUint16(34, 16, true);
  tag(36, 'data'); view.setUint32(40, samples * 2, true);
  if (samples) view.setInt16(44, 1000, true);
  return bytes;
}
function upload({ bytes = wav(), mode, hint = '摇滚 220BPM', fields = [], headers = {} } = {}) {
  const form = new FormData();
  form.set('audio', new Blob([bytes]), 'not-trusted.txt');
  form.set('hint', hint);
  if (mode !== undefined) form.set('mode', mode);
  for (const [key, value] of fields) form.append(key, value);
  return new Request('http://127.0.0.1/api/v1/generate', {
    method: 'POST', body: form, headers: { 'x-request-id': 'asr-fake', ...headers },
  });
}
function fake(...outputs) {
  return { calls: [], async run(model, input) {
    this.calls.push({ model, input });
    assert.ok(this.calls.length <= outputs.length, 'unexpected extra call');
    const output = outputs[this.calls.length - 1];
    if (output instanceof Error) throw output;
    return output;
  } };
}
async function expectError(request, AI, code, status, count = 0) {
  const response = await handleRequest(request, { AI });
  const body = await response.json();
  assert.equal(response.status, status);
  assert.ok(errorSchemaValid(body), JSON.stringify(errorSchemaValid.errors));
  assert.equal(body.error.code, code);
  assert.equal(body.request_id, 'asr-fake');
  assert.equal(body.mode, undefined);
  assert.equal(body.transcript, undefined);
  assert.doesNotMatch(JSON.stringify(body), /secret|token|stack/);
  if (AI?.calls) assert.equal(AI.calls.length, count);
}

test('audio without a usable AI binding returns sanitized ASR_UNAVAILABLE / 503; hint cannot substitute', async () => {
  for (const AI of [undefined, null, {}, { run: 1 }, { get run() { throw new Error('secret'); } }]) {
    await expectError(upload(), AI, 'ASR_UNAVAILABLE', 503);
  }
});

test('audio rules/default: one multilingual ASR call with exact Base64 WAV, then original rules and assertMusic', async () => {
  for (const mode of [undefined, 'rules']) {
    for (const text of ['温暖轻快', '来一段悲伤、慢的木吉他', '摇滚 120BPM', '夏日 🎸']) {
      const bytes = wav();
      const AI = fake({ text: ` \t${text}\r\n` });
      const response = await handleRequest(upload({ bytes, mode }), { AI });
      const music = await response.json();
      assert.equal(response.status, 200);
      assertMusic(music);
      assert.deepEqual(music, { ...generateRules({ text, request_id: 'asr-fake' }), transcript: text });
      assert.equal(AI.calls.length, 1);
      assert.equal(AI.calls[0].model, ASR_MODEL);
      assert.equal(ASR_MODEL, '@cf/openai/whisper-large-v3-turbo');
      assert.deepEqual(Object.keys(AI.calls[0].input).sort(), ['audio', 'task']);
      assert.equal(AI.calls[0].input.task, 'transcribe');
      assert.deepEqual(Buffer.from(AI.calls[0].input.audio, 'base64'), Buffer.from(bytes));
    }
  }
});

test('empty transcription returns EMPTY_TRANSCRIPTION / 502 once, including with hint', async () => {
  for (const text of ['', ' \t\r\n']) {
    await expectError(upload({ mode: 'ai' }), fake({ text }), 'EMPTY_TRANSCRIPTION', 502, 1);
  }
});

test('ASR failures and malformed output are sanitized ASR_FAILED / 502 with no retry', async () => {
  for (const output of [new Error('secret token stack'), undefined, null, [], {},
    { text: 1 }, { text: null }, { response: '温暖' }, { text: '木'.repeat(501) },
    { text: 'a\0b' }, { text: '\ud800' }, { get text() { throw new Error('secret'); } }]) {
    await expectError(upload({ mode: 'ai' }), fake(output), 'ASR_FAILED', 502, 1);
  }
});

test('illegal audio, auth, size and multipart/mode errors never call ASR or LLM', async () => {
  const invalid = [new Uint8Array(), new TextEncoder().encode('not WAV'), wav(0), wav(64001)];
  const silent = wav(); silent.fill(0, 44); invalid.push(silent);
  for (const [offset, value, width] of [[22, 2, 2], [24, 32000, 4], [34, 8, 2], [4, 100, 4]]) {
    const bytes = wav(); const view = new DataView(bytes.buffer);
    if (width === 2) view.setUint16(offset, value, true); else view.setUint32(offset, value, true);
    invalid.push(bytes);
  }
  for (const bytes of invalid) await expectError(upload({ bytes }), fake(), 'INVALID_AUDIO', 400);
  for (const mode of ['', 'other', 'mock', new Blob(['ai'])]) {
    await expectError(upload({ mode }), fake(), 'INVALID_REQUEST', 400);
  }
  for (const fields of [[['mode', 'ai'], ['mode', 'rules']], [['hint', 'second']], [['transcript', '温暖']]]) {
    await expectError(upload({ fields }), fake(), 'INVALID_REQUEST', 400);
  }
  await expectError(upload({ headers: { 'content-length': '1000001' } }), fake(), 'REQUEST_TOO_LARGE', 413);
  const AI = fake();
  const unauthorized = await handleRequest(upload(), { AI, AUTH_REQUIRED: '1', DEVICE_TOKEN: 'secret' });
  assert.equal(unauthorized.status, 401);
  assert.equal(AI.calls.length, 0);
});

test('audio ai: one ASR then one LLM intent extraction, with recognized text and no hint', async () => {
  const text = '温暖轻快 117BPM';
  const AI = fake({ text }, { response: { mood: 'bright', bpm: 96, root: 'C', mode: 'major', bars: 4, style: 'acoustic' } });
  const response = await handleRequest(upload({ mode: 'ai' }), { AI });
  const music = await response.json();
  assert.equal(response.status, 200);
  assertMusic(music);
  assert.equal(music.mode, 'ai');
  assert.equal(music.transcript, text);
  assert.equal(music.bpm, 117);
  assert.deepEqual(music, { ...generateRules({ text, request_id: 'asr-fake' }), mode: 'ai', transcript: text });
  assert.deepEqual(AI.calls.map(call => call.model), [ASR_MODEL, INTENT_MODEL]);
  assert.equal(AI.calls[1].input.messages[1].content, text);
});

test('audio LLM failure/invalid intent never retries ASR, retries LLM or falls back to rules', async () => {
  for (const [output, code] of [[new Error('secret token stack'), 'MODEL_ERROR'],
    [{ response: '{' }, 'AI_OUTPUT_INVALID'], [{ response: { mood: 'bright', events: [] } }, 'AI_OUTPUT_INVALID']]) {
    const AI = fake({ text: '温暖' }, output);
    await expectError(upload({ mode: 'ai' }), AI, code, 502, 2);
    assert.deepEqual(AI.calls.map(call => call.model), [ASR_MODEL, INTENT_MODEL]);
  }
  await expectError(upload({ mode: 'ai' }), fake({ text: '摇滚 221BPM' }), 'INVALID_BPM', 400, 1);
});

test('optional transcript schema accepts Unicode metadata and rejects incompatible values', () => {
  const music = generateRules({ text: '温暖' });
  for (const transcript of ['温暖\n轻快', '🎸'.repeat(500)]) assertMusic({ ...music, transcript });
  for (const transcript of [null, 1, '', '木'.repeat(501), 'a\0b', '\ud800']) {
    assert.throws(() => assertMusic({ ...music, transcript }), error => error.code === 'INVALID_MUSIC_DATA');
  }
});
