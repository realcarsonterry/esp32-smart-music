# 阶段 8：规则后端与 AI 意图适配

当前完整状态见 ../docs/final-review-20261013.md，现场演示与部署步骤见 ../START_HERE.md。
wrangler.production.jsonc 已正式部署，配置 AI 绑定与强制认证。
线上地址：https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev 。
真实 LLM、真实 ASR（中文 TTS）及共享 C 渲染已验证；日志见 ../artifacts/cloud-verification-20261013.txt。
本地 wrangler.jsonc 保持无 AI binding，默认 HTTP 回归不调用模型。

唯一契约为根目录 SPEC.md。Worker 入口 src/worker.ts 导出 handleRequest 和 fetch。
根目录实际执行过的后端命令为 `npm run typecheck`、`npm test`。
本次完整回归、环境、退出码和结果路径见 ../docs/acceptance-report.md。

rules 为缺省模式，继续使用原 src/rules.ts。mock 必须显式请求且 ALLOW_MOCK=1。
文本 rules、mock、health、info 不访问模型方法；ai_configured 只代表绑定存在。

src/ai.ts 的 ModelClient 可由测试假客户端注入。无可用绑定返回 AI_UNAVAILABLE。
模型结果只允许 mood、bpm、root、mode、bars、style，按现有四预设严格校验。
非法 JSON 或字段最多重试一次，两次非法返回 AI_OUTPUT_INVALID；模型异常 MODEL_ERROR。
有效意图才交给原 generateRules，保留原文 seed 和显式 BPM，成功标为 ai。
禁止模型直接提供 events；生成结果仍经 schema 与跨字段校验，不降级成 rules。

wrangler.jsonc 没有 AI 绑定；这是本地回归配置。生产配置 wrangler.production.jsonc 绑定真实 Workers AI。
音频接口检查真实 WAV 内容后调用 src/asr.ts，经 env.AI.run 使用多语言
@cf/openai/whisper-large-v3-turbo（完整 WAV 的 Base64，task=transcribe）。
无绑定返回 503/ASR_UNAVAILABLE；异常或非法输出返回 502/ASR_FAILED；
空转写返回 502/EMPTY_TRANSCRIPTION。ASR 不重试，hint 接受但忽略。
multipart mode 缺省 rules：一次 ASR 后直接规则生成；ai：一次 ASR 后一次 LLM
意图提取。所有失败不降级；最终 assertMusic 校验后返回可选 transcript。
C 解析器接受并忽略 transcript，原 C 引擎保持不变。
官方输入/输出 schema 及离线验收见 ../docs/asr-offline-verification.md。
真人语音完整测试：BLOCKED；假客户端和非语音合成 PCM 不是实际识别证据。
后续云端验收使用可听的 Windows 中文 TTS，真实 ASR 返回转写，结果与输入/输出样例已记录。
本地 --local HTTP rules → 原 C 引擎 → WAV 已实际验证，测试从不配置真实绑定。

完整边界、字段与用例分别见 ../docs/architecture.md、../docs/protocol.md、../docs/testing.md。
官方模型、绑定和 JSON Mode 核对链接见 ../docs/architecture.md；真实 AI 验收见 ../docs/final-review-20261013.md。
