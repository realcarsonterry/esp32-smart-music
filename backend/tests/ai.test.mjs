import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import Ajv2020 from 'ajv/dist/2020.js';
import { INTENT_MODEL } from '../src/ai.ts';
import { defaultSeed, generateRules } from '../src/rules.ts';
import { handleRequest } from '../src/worker.ts';
import { assertMusic } from '../src/validate.ts';

const ajv = new Ajv2020({ strict: true });
const errorSchema = ajv.compile(JSON.parse(readFileSync(new URL('../../shared/error.schema.json', import.meta.url))));
const bright = { mood: 'bright', bpm: 96, root: 'C', mode: 'major', bars: 4, style: 'acoustic' };
function post(body) {
  return new Request('http://127.0.0.1/api/v1/generate-from-text', {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ text: '温暖', mode: 'ai', request_id: 'stage8-fake', ...body }),
  });
}
function fake(...outputs) {
  return {
    calls: [],
    async run(model, input) {
      this.calls.push({ model, input: structuredClone(input) });
      assert.ok(this.calls.length <= outputs.length, 'unexpected extra model call');
      const value = outputs[this.calls.length - 1];
      if (value instanceof Error) throw value;
      return value;
    },
  };
}
async function error(body, AI, code, status, calls) {
  const response = await handleRequest(post(body), { AI });
  const result = await response.json();
  assert.equal(response.status, status);
  assert.ok(errorSchema(result), JSON.stringify(errorSchema.errors));
  assert.equal(result.request_id, 'stage8-fake');
  assert.equal(result.error.code, code);
  assert.ok(!JSON.stringify(result).includes('secret-model-output'));
  assert.ok(!JSON.stringify(result).includes('stack'));
  assert.equal(result.mode, undefined); // No fallback to rules or success wrapper.
  if (AI?.calls) assert.equal(AI.calls.length, calls);
}

test('AI_UNAVAILABLE: no binding, null binding, malformed binding', async () => {
  for (const binding of [undefined, null, {}, { run: 1 }]) {
    await error({}, binding, 'AI_UNAVAILABLE', 503);
  }
});

test('invalid JSON retries exactly once; two invalid results return AI_OUTPUT_INVALID', async () => {
  const AI = fake({ response: '{secret-model-output' }, { response: 'still invalid' });
  await error({}, AI, 'AI_OUTPUT_INVALID', 502, 2);
  assert.match(AI.calls[1].input.messages[0].content, /Previous output was invalid/);
  assert.ok(!JSON.stringify(AI.calls[1]).includes('secret-model-output'));
});

test('one invalid JSON then valid intent succeeds; only rules generate events', async () => {
  const AI = fake({ response: '{' }, { response: JSON.stringify(bright) });
  const response = await handleRequest(post({ text: '这是用户的原始文本', seed: 0 }), { AI });
  const music = await response.json();
  assert.equal(response.status, 200);
  assertMusic(music);
  assert.equal(AI.calls.length, 2);
  assert.deepEqual(music, { ...generateRules({ text: '轻快 96BPM', seed: 0, request_id: 'stage8-fake' }), mode: 'ai' });
  for (const { model, input } of AI.calls) {
    assert.equal(model, INTENT_MODEL);
    assert.equal(model, '@cf/meta/llama-3.3-70b-instruct-fp8-fast');
    assert.equal(input.stream, false);
    assert.equal(input.response_format.type, 'json_schema');
    const schema = input.response_format.json_schema;
    assert.deepEqual(Object.keys(schema.properties).sort(), ['bars', 'bpm', 'mode', 'mood', 'root', 'style']);
    assert.equal(schema.additionalProperties, false);
  }
});

test('all four validated intents use SPEC rules, original text hash and explicit BPM', async () => {
  const profiles = [
    [bright, '轻快'],
    [{ mood: 'sad', bpm: 72, root: 'A', mode: 'minor', bars: 4, style: 'acoustic' }, '悲伤'],
    [{ mood: 'rock', bpm: 128, root: 'E', mode: 'minor', bars: 4, style: 'rock' }, '摇滚'],
    [{ mood: 'default', bpm: 100, root: 'G', mode: 'major', bars: 4, style: 'acoustic' }, '伴奏'],
  ];
  for (const [intent, keyword] of profiles) {
    for (const explicit of ['', ' 117BPM']) {
      const text = ` \t保留原文哈希${explicit}\r\n`;
      const AI = fake({ response: intent }); // Official JSON Mode object response.
      const response = await handleRequest(post({ text }), { AI });
      assert.equal(response.status, 200);
      const music = await response.json();
      assertMusic(music);
      assert.equal(AI.calls.length, 1);
      assert.deepEqual(music, { ...generateRules({ text: `${keyword} ${explicit ? 117 : intent.bpm}BPM`,
        seed: defaultSeed(text), request_id: 'stage8-fake' }), mode: 'ai' });
    }
  }
});

test('strict intent gate rejects missing/extra fields, model events, wrong types/ranges/preset pairs', async () => {
  const missing = { ...bright }; delete missing.style;
  const invalid = [missing, { ...bright, events: [] }, { ...bright, chords: [] },
    { ...bright, extra: true }, { ...bright, bpm: '96' }, { ...bright, bpm: 39 },
    { ...bright, bpm: 221 }, { ...bright, bpm: 96.5 }, { ...bright, bars: 16 },
    { ...bright, root: 'G' }, { ...bright, mode: 'minor' }, { ...bright, style: 'rock' },
    { ...bright, mood: '__proto__' }, { ...bright, mood: 'unknown' }, null, [], 42,
    '```json\n' + JSON.stringify(bright) + '\n```', JSON.stringify(bright) + ' trailing text'];
  for (const value of invalid) {
    const AI = fake({ response: value }, { response: value });
    await error({}, AI, 'AI_OUTPUT_INVALID', 502, 2);
  }
  for (const output of [undefined, null, [], {}, { events: [] }]) {
    await error({}, fake(output, output), 'AI_OUTPUT_INVALID', 502, 2);
  }
});

test('schema-invalid intent may recover on the second attempt', async () => {
  const AI = fake({ response: { ...bright, events: [{ midi: 1 }] } }, { response: bright });
  const response = await handleRequest(post({}), { AI });
  assert.equal(response.status, 200);
  assertMusic(await response.json());
  assert.equal(AI.calls.length, 2);
});

test('model errors are sanitized MODEL_ERROR with no retry or rules fallback', async () => {
  await error({}, fake(new Error('secret-model-output token stack')), 'MODEL_ERROR', 502, 1);
  await error({}, fake({ response: '{' }, new Error('secret-model-output')), 'MODEL_ERROR', 502, 2);
});

test('bad AI requests fail before model calls', async () => {
  for (const [body, code] of [[{ text: '' }, 'INVALID_TEXT'], [{ text: '木'.repeat(501) }, 'INVALID_TEXT'],
    [{ seed: -1 }, 'INVALID_REQUEST'], [{ text: '221bpm' }, 'INVALID_BPM']]) {
    await error(body, fake(), code, 400, 0);
  }
});

test('rules/default/mock/health/info call fake model zero times', async () => {
  const AI = fake();
  for (const mode of [undefined, 'rules', 'mock']) {
    const response = await handleRequest(post({ mode }), { AI, ALLOW_MOCK: '1' });
    assert.equal(response.status, 200);
    assertMusic(await response.json());
  }
  for (const path of ['/health', '/api/v1/info']) {
    assert.equal((await handleRequest(new Request(`http://127.0.0.1${path}`), { AI })).status, 200);
  }
  assert.equal(AI.calls.length, 0);
});
