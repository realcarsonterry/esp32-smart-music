import { ApiError, MAX_BODY_BYTES, PROTOCOL_VERSION, validRequestId } from "./protocol";
import { generateAI } from "./ai";
import { transcribeWav } from "./asr";
import { generateRules } from "./rules";
import { assertMusic } from "./validate";
import { RECORDING_FORMAT, SYNTHESIS_FORMAT, validateWav } from "./wav";

export interface Env {
  AI?: unknown;
  DEVICE_TOKEN?: string;
  AUTH_REQUIRED?: string;
  ALLOW_MOCK?: string;
  ALLOW_DEPLOY?: string;
}

function json(value: unknown, status = 200): Response {
  return Response.json(value, { status });
}

async function readBody(request: Request): Promise<Uint8Array<ArrayBuffer>> {
  const declared = request.headers.get("content-length");
  if (declared !== null && (!/^\d+$/.test(declared) || Number(declared) > MAX_BODY_BYTES)) {
    throw new ApiError("REQUEST_TOO_LARGE", "Request body exceeds 1000000 bytes", 413);
  }
  const chunks: Uint8Array[] = [];
  let size = 0;
  const reader = request.body?.getReader();
  if (reader) {
    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        size += value.byteLength;
        if (size > MAX_BODY_BYTES) {
          await reader.cancel();
          throw new ApiError("REQUEST_TOO_LARGE", "Request body exceeds 1000000 bytes", 413);
        }
        chunks.push(value);
      }
    } finally { reader.releaseLock(); }
  }
  const bytes = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
  return bytes;
}

export async function handleRequest(request: Request, env: Env = {}): Promise<Response> {
  let requestId = "request"; // Deterministic fallback keeps identical requests identical.
  try {
    const headerId = request.headers.get("x-request-id");
    if (headerId !== null) {
      if (!validRequestId(headerId)) throw new ApiError("INVALID_REQUEST", "Invalid X-Request-ID");
      requestId = headerId;
    }
    const path = new URL(request.url).pathname;
    if (request.method === "GET" && path === "/health") {
      return json({ ok: true, protocol_version: PROTOCOL_VERSION });
    }
    if (request.method === "GET" && path === "/api/v1/info") {
      return json({ ok: true, protocol_version: PROTOCOL_VERSION, rules_available: true,
        ai_configured: env.AI !== undefined && env.AI !== null, auth_required: env.AUTH_REQUIRED === "1",
        recording_format: RECORDING_FORMAT, synthesis_format: SYNTHESIS_FORMAT });
    }
    if (request.method !== "POST" || (path !== "/api/v1/generate-from-text" && path !== "/api/v1/generate")) {
      throw new ApiError("INVALID_REQUEST", "Unknown route or method");
    }
    if (env.AUTH_REQUIRED === "1"
      && (!env.DEVICE_TOKEN || request.headers.get("authorization") !== `Bearer ${env.DEVICE_TOKEN}`)) {
      throw new ApiError("UNAUTHORIZED", "A valid bearer token is required", 401);
    }
    const bytes = await readBody(request);
    const contentType = request.headers.get("content-type") ?? "";
    if (path === "/api/v1/generate-from-text") {
      if (contentType.split(";")[0]?.trim().toLowerCase() !== "application/json") {
        throw new ApiError("INVALID_REQUEST", "Expected application/json");
      }
      let body: Record<string, unknown>;
      try {
        const parsed: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
        if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error();
        body = parsed as Record<string, unknown>;
      } catch { throw new ApiError("INVALID_REQUEST", "Invalid JSON object"); }
      if (body.request_id !== undefined) {
        if (!validRequestId(body.request_id)) throw new ApiError("INVALID_REQUEST", "Invalid request_id");
        requestId = body.request_id;
      }
      if (Object.keys(body).some(key => !["text", "seed", "request_id", "mode"].includes(key))) {
        throw new ApiError("INVALID_REQUEST", "Unexpected request field");
      }
      const mode = body.mode === undefined ? "rules" : body.mode;
      if (mode === "ai") {
        const music = await generateAI({ text: body.text as string, seed: body.seed as number | undefined,
          request_id: requestId }, env.AI);
        assertMusic(music);
        return json(music);
      }
      if (mode !== "rules" && !(mode === "mock" && env.ALLOW_MOCK === "1")) {
        throw new ApiError("INVALID_REQUEST", "Unsupported or disabled mode");
      }
      const music = generateRules({ text: body.text as string, seed: body.seed as number | undefined, request_id: requestId });
      if (mode === "mock") music.mode = "mock";
      assertMusic(music);
      return json(music);
    }
    if (!/^multipart\/form-data\s*;/i.test(contentType)) {
      throw new ApiError("INVALID_REQUEST", "Expected multipart/form-data");
    }
    let form: FormData;
    try {
      form = await new Response(bytes, { headers: { "content-type": contentType } }).formData();
    } catch { throw new ApiError("INVALID_REQUEST", "Invalid multipart body"); }
    if ([...form.keys()].some(key => !["audio", "hint", "mode"].includes(key))
      || form.getAll("audio").length !== 1 || form.getAll("hint").length > 1
      || form.getAll("mode").length > 1
      || (form.has("hint") && typeof form.get("hint") !== "string")) {
      throw new ApiError("INVALID_REQUEST", "Expected one audio file and optional hint/mode");
    }
    const mode = form.get("mode") ?? "rules";
    if (mode !== "rules" && mode !== "ai") throw new ApiError("INVALID_REQUEST", "Unsupported audio mode");
    const audio = form.get("audio");
    if (audio === null || typeof audio === "string") throw new ApiError("INVALID_AUDIO", "Missing audio file");
    const wav = new Uint8Array(await audio.arrayBuffer());
    validateWav(wav);
    // hint is accepted for compatibility but never used as recognized text.
    const transcript = await transcribeWav(wav, env.AI);
    const input = { text: transcript, request_id: requestId };
    const music = mode === "ai" ? await generateAI(input, env.AI, 1) : generateRules(input);
    music.transcript = transcript;
    assertMusic(music);
    return json(music);
  } catch (error) {
    const safe = error instanceof ApiError ? error : new ApiError("INTERNAL_ERROR", "Internal error", 500);
    return json({ ok: false, protocol_version: PROTOCOL_VERSION, request_id: requestId,
      error: { code: safe.code, message: safe.message } }, safe.status);
  }
}

export default { fetch: handleRequest };
