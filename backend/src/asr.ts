import { ApiError } from "./protocol";
import { trimText } from "./rules";

// Verified 2026-10-13: Cloudflare's schema-input.json accepts Base64 audio;
// schema-output.json requires text. Encode the complete WAV, not raw samples.
// https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/
export const ASR_MODEL = "@cf/openai/whisper-large-v3-turbo";

export interface ASRInput { audio: string; task: "transcribe" }
export interface ASRClient {
  run(model: string, input: ASRInput): Promise<unknown>;
}

function clientFrom(binding: unknown): ASRClient {
  try {
    if (binding && typeof binding === "object") {
      const run = (binding as ASRClient).run;
      if (typeof run === "function") return { run: (model, input) => run.call(binding, model, input) };
    }
  } catch { /* An inaccessible binding is unconfigured. */ }
  throw new ApiError("ASR_UNAVAILABLE", "ASR binding is unavailable", 503);
}

// The upload route validates WAV contents before entering this adapter.
// No retries, hint substitution, translation, or secondary model calls here.
export async function transcribeWav(wav: Uint8Array, binding: unknown): Promise<string> {
  const client = clientFrom(binding);
  let text: unknown;
  try {
    // Chunk conversion avoids exceeding the argument limit on a 4-second WAV.
    let binary = "";
    for (let i = 0; i < wav.length; i += 8192) {
      binary += String.fromCharCode(...wav.subarray(i, i + 8192));
    }
    const output = await client.run(ASR_MODEL, { audio: btoa(binary), task: "transcribe" });
    if (!output || typeof output !== "object" || Array.isArray(output)) throw new Error();
    text = (output as { text?: unknown }).text;
    if (typeof text !== "string") throw new Error();
  } catch { throw new ApiError("ASR_FAILED", "Speech recognition failed", 502); }
  const transcript = trimText(text as string);
  if (!transcript) throw new ApiError("EMPTY_TRANSCRIPTION", "Speech recognition returned no text", 502);
  // Keep metadata compatible with the text limit and C's UTF-8 JSON parser.
  if ([...transcript].length > 500 || /[\u0000\uD800-\uDFFF]/u.test(transcript)) {
    throw new ApiError("ASR_FAILED", "Speech recognition returned invalid text", 502);
  }
  return transcript;
}
