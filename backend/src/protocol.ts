export const PROTOCOL_VERSION = "1.0";
export const PPQ = 480;
export const TICKS_PER_BAR = 1920;
export const MAX_BODY_BYTES = 1_000_000;

export type ErrorCode =
  | "INVALID_REQUEST" | "INVALID_TEXT" | "INVALID_AUDIO" | "INVALID_BPM"
  | "INVALID_MUSIC_DATA" | "UNSUPPORTED_PROTOCOL_VERSION" | "ASR_UNAVAILABLE"
  | "ASR_FAILED" | "EMPTY_TRANSCRIPTION" | "AI_UNAVAILABLE" | "AI_OUTPUT_INVALID"
  | "MODEL_ERROR" | "REQUEST_TOO_LARGE" | "UNAUTHORIZED" | "INTERNAL_ERROR";

export class ApiError extends Error {
  constructor(public readonly code: ErrorCode, message: string, public readonly status = 400) {
    super(message);
  }
}

export interface Chord {
  start_tick: number;
  duration_tick: number;
  symbol: string;
  midi_notes: number[];
}

export interface MusicEvent {
  id: string;
  part: "accompaniment" | "melody";
  midi: number;
  velocity: number;
  start_tick: number;
  duration_tick: number;
}

export interface Music {
  ok: true;
  protocol_version: "1.0";
  request_id: string;
  transcript?: string;
  mode: "rules" | "ai" | "mock";
  seed: number;
  bpm: number;
  time_signature: { numerator: 4; denominator: 4 };
  ppq: 480;
  bars: number;
  key: { root: "A" | "B" | "C" | "D" | "E" | "F" | "G"; mode: "major" | "minor" };
  chords: Chord[];
  events: MusicEvent[];
}

export function validRequestId(value: unknown): value is string {
  // Compare the whole match: JavaScript's $ also matches before a final newline.
  return typeof value === "string" && value.match(/^[A-Za-z0-9._:-]{1,64}$/)?.[0] === value;
}
