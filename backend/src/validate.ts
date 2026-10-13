import Ajv2020 from "ajv/dist/2020.js";
import addFormats from "ajv-formats";
import musicSchema from "../../shared/music.schema.json";
import errorSchema from "../../shared/error.schema.json";
import { ApiError, TICKS_PER_BAR } from "./protocol";
import type { Music } from "./protocol";

const ajv = new Ajv2020({ allErrors: true, strict: true });
addFormats(ajv);
const schemaValid = ajv.compile<Music>(musicSchema);
export const errorSchemaValid = ajv.compile(errorSchema);

export function assertMusic(value: unknown): asserts value is Music {
  if (!schemaValid(value)) {
    throw new ApiError("INVALID_MUSIC_DATA", "Music JSON does not match the protocol schema");
  }
  const total = value.bars * TICKS_PER_BAR;
  const invalid = () => { throw new ApiError("INVALID_MUSIC_DATA", "Music cross-field validation failed"); };
  const spanValid = (span: { start_tick: number; duration_tick: number }) =>
    Number.isSafeInteger(span.start_tick) && Number.isSafeInteger(span.duration_tick)
    && span.start_tick < total && span.duration_tick <= total - span.start_tick;
  for (const chord of value.chords) if (!spanValid(chord)) invalid();
  const ids = new Set<string>();
  const endpoints: { tick: number; delta: number }[] = [];
  for (const event of value.events) {
    if (!spanValid(event) || ids.has(event.id)) invalid();
    ids.add(event.id);
    endpoints.push({ tick: event.start_tick, delta: 1 },
      { tick: event.start_tick + event.duration_tick, delta: -1 });
  }
  // Half-open intervals: ending notes leave before starting notes at equal tick.
  endpoints.sort((a, b) => a.tick - b.tick || a.delta - b.delta);
  let active = 0;
  for (const point of endpoints) {
    active += point.delta;
    if (active > 8) invalid();
  }
  for (const event of value.events) {
    if (event.part !== "accompaniment") continue;
    let cursor = event.start_tick;
    const end = cursor + event.duration_tick;
    // Match core across the whole duration, including chord boundaries/gaps.
    while (cursor < end) {
      let coveredUntil = cursor;
      for (const chord of value.chords) {
        if (chord.start_tick <= cursor && chord.start_tick + chord.duration_tick > coveredUntil
          && chord.midi_notes.some(note => note % 12 === event.midi % 12)) {
          coveredUntil = chord.start_tick + chord.duration_tick;
        }
      }
      if (coveredUntil === cursor) invalid();
      cursor = coveredUntil;
    }
  }
}
