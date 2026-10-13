import { ApiError, PPQ, PROTOCOL_VERSION, TICKS_PER_BAR, validRequestId } from "./protocol";
import type { Music, MusicEvent } from "./protocol";

interface Preset {
  key: Music["key"];
  bpm: number;
  symbols: readonly string[];
  notes: readonly (readonly number[])[];
  velocity: number;
  interval: number;
  duration: number;
}

// These are SPEC's voicings, including the different Em/D octaves per preset.
const PRESETS: Record<"bright" | "sad" | "rock" | "default", Preset> = {
  bright: {
    key: { root: "C", mode: "major" }, bpm: 96, symbols: ["C", "G", "Am", "F"],
    notes: [[48, 52, 55, 60], [43, 47, 50, 55], [45, 48, 52, 57], [41, 45, 48, 53]],
    velocity: 80, interval: 480, duration: 400,
  },
  sad: {
    key: { root: "A", mode: "minor" }, bpm: 72, symbols: ["Am", "G", "F", "Em"],
    notes: [[45, 48, 52, 57], [43, 47, 50, 55], [41, 45, 48, 53], [40, 47, 52, 55]],
    velocity: 64, interval: 480, duration: 400,
  },
  rock: {
    key: { root: "E", mode: "minor" }, bpm: 128, symbols: ["Em", "D", "C", "D"],
    notes: [[40, 47, 52, 59], [42, 45, 50, 54], [48, 52, 55, 60], [42, 45, 50, 54]],
    velocity: 112, interval: 240, duration: 200,
  },
  default: {
    key: { root: "G", mode: "major" }, bpm: 100, symbols: ["G", "Em", "C", "D"],
    notes: [[43, 47, 50, 55], [40, 47, 52, 55], [48, 52, 55, 60], [50, 54, 57, 62]],
    velocity: 84, interval: 480, duration: 400,
  },
};

// Only SPACE, TAB, CR, LF are trimmed; no Unicode normalization or String.trim().
export function trimText(text: string): string {
  return text.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, "");
}

export function defaultSeed(text: string): number {
  let hash = 2166136261;
  for (const byte of new TextEncoder().encode(trimText(text))) {
    hash = Math.imul(hash ^ byte, 16777619) >>> 0;
  }
  return hash;
}

export function xorshift32(state: number): number {
  let x = state >>> 0;
  x ^= x << 13;
  x ^= x >>> 17;
  x ^= x << 5;
  return x >>> 0;
}

export interface RulesInput { text: string; seed?: number; request_id?: string }

// Shared request validation; this does not generate any events.
export function validateRulesInput(input: RulesInput): { text: string; explicitBpm?: number } {
  if (typeof input.text !== "string") throw new ApiError("INVALID_TEXT", "text must be a string");
  const text = trimText(input.text);
  if (text.length === 0 || [...text].length > 500) {
    throw new ApiError("INVALID_TEXT", "text must contain 1 to 500 characters");
  }
  if (input.seed !== undefined && (!Number.isInteger(input.seed) || input.seed < 0 || input.seed > 0xffffffff)) {
    throw new ApiError("INVALID_REQUEST", "seed must be a uint32 integer");
  }
  if (input.request_id !== undefined && !validRequestId(input.request_id)) {
    throw new ApiError("INVALID_REQUEST", "request_id must match ^[A-Za-z0-9._:-]{1,64}$");
  }
  const explicit = text.match(/([+-]?\d+(?:\.\d+)?)\s*(?:BPM|bpm|拍)/);
  const explicitBpm = explicit ? Number(explicit[1]) : undefined;
  if (explicitBpm !== undefined && (!Number.isInteger(explicitBpm) || explicitBpm < 40 || explicitBpm > 220)) {
    throw new ApiError("INVALID_BPM", "BPM must be an integer from 40 to 220");
  }
  return { text, explicitBpm };
}

// Pure: no I/O, clocks, global RNG, mutations of input/presets, or model calls.
export function generateRules(input: RulesInput): Music {
  const { text, explicitBpm } = validateRulesInput(input);
  // Priority is rock > sad > bright > default, independent of keyword order.
  const preset = /摇滚|强烈/.test(text) ? PRESETS.rock
    : /悲伤|忧伤|慢/.test(text) ? PRESETS.sad
    : /温暖|轻快|夏日/.test(text) ? PRESETS.bright : PRESETS.default;
  const bpm = explicitBpm ?? preset.bpm;
  const seed = input.seed ?? defaultSeed(text);
  // SPEC's previously confirmed decimal seed-0 state, shared with C core.
  let rng = seed === 0 ? 2164325893 : seed;
  const chords = preset.symbols.map((symbol, bar) => ({
    start_tick: bar * TICKS_PER_BAR, duration_tick: TICKS_PER_BAR,
    symbol, midi_notes: [...preset.notes[bar]!],
  }));
  const events: MusicEvent[] = [];
  for (const chord of chords) {
    for (let offset = 0; offset < TICKS_PER_BAR; offset += preset.interval) {
      chord.midi_notes.forEach((midi, voice) => events.push({
        id: `a${events.length}`, part: "accompaniment", midi, velocity: preset.velocity,
        start_tick: chord.start_tick + offset + voice * 10, duration_tick: preset.duration,
      }));
    }
  }
  // RNG call order: ascending bar, then beat 0..3, exactly one next() per melody
  // note (16 draws total). Accompaniment, IDs and metadata never draw randoms.
  // Candidates ascend from MIDI 60..76, filtered by the current chord's pitch
  // classes. Select next() % candidates.length. Every beat is strong per SPEC.
  for (const chord of chords) {
    const candidates = Array.from({ length: 17 }, (_, i) => i + 60)
      .filter(midi => chord.midi_notes.some(note => note % 12 === midi % 12));
    for (let beat = 0; beat < 4; beat++) {
      rng = xorshift32(rng);
      events.push({
        id: `m${chord.start_tick / TICKS_PER_BAR * 4 + beat}`, part: "melody",
        midi: candidates[rng % candidates.length]!, velocity: 88,
        start_tick: chord.start_tick + beat * PPQ, duration_tick: 360,
      });
    }
  }
  return {
    ok: true, protocol_version: PROTOCOL_VERSION, request_id: input.request_id ?? `rules-${seed}`,
    mode: "rules", seed, bpm, time_signature: { numerator: 4, denominator: 4 },
    ppq: PPQ, bars: 4, key: { ...preset.key }, chords, events,
  };
}
