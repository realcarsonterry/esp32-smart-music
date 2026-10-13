import { ApiError } from "./protocol";

export const RECORDING_FORMAT = { container: "WAV", encoding: "PCM", sample_rate: 16000, channels: 1, bits_per_sample: 16, max_seconds: 4 } as const;
export const SYNTHESIS_FORMAT = { container: "WAV", encoding: "PCM", sample_rate: 32000, channels: 1, bits_per_sample: 16 } as const;

// Inspect bytes, never filename/MIME. Accept ancillary RIFF chunks with padding.
export function validateWav(bytes: Uint8Array): void {
  const fail = (): never => { throw new ApiError("INVALID_AUDIO", "Expected non-silent mono 16000 Hz 16-bit PCM WAV, at most 4 seconds"); };
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (offset: number) => String.fromCharCode(...bytes.subarray(offset, offset + 4));
  if (bytes.length < 12 || tag(0) !== "RIFF" || tag(8) !== "WAVE"
    || view.getUint32(4, true) !== bytes.length - 8) fail();
  let offset = 12;
  let formatSeen = false;
  let data: Uint8Array | undefined;
  while (offset < bytes.length) {
    if (bytes.length - offset < 8) fail();
    const kind = tag(offset);
    const size = view.getUint32(offset + 4, true);
    const start = offset + 8;
    const next = start + size + (size % 2);
    if (next > bytes.length) fail();
    if (kind === "fmt ") {
      if (formatSeen || size < 16 || (size !== 16 && (size < 18 || view.getUint16(start + 16, true) !== size - 18))) fail();
      if (view.getUint16(start, true) !== 1 || view.getUint16(start + 2, true) !== 1
        || view.getUint32(start + 4, true) !== 16000 || view.getUint32(start + 8, true) !== 32000
        || view.getUint16(start + 12, true) !== 2 || view.getUint16(start + 14, true) !== 16) fail();
      formatSeen = true;
    } else if (kind === "data") {
      if (!formatSeen || data !== undefined || size === 0 || size % 2 !== 0 || size > 16000 * 4 * 2) fail();
      data = bytes.subarray(start, start + size);
    }
    offset = next;
  }
  if (!formatSeen || data === undefined || !data.some(byte => byte !== 0)) fail();
}
