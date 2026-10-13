# 架构（阶段 8）

SPEC.md 是协议契约。阶段 8 的离线适配后，生产服务已部署，真实 LLM/ASR（中文 TTS）已完成验收。
当前状态见 [最终审查](final-review-20261013.md)，真实请求记录见 [云端结果](../artifacts/cloud/verification.json)。

## 数据流与职责

1. Worker 校验请求大小、认证、字段、文本与 seed。
2. rules（缺省）直接调用 backend/src/rules.ts 的 generateRules。
3. ai 由 backend/src/ai.ts 经 env.AI.run 获取意图，严格校验后调用同一个 generateRules。
   音频入口先检查 WAV，经 backend/src/asr.ts 获取转写，再走 rules 或一次 AI 意图提取。
4. 两种模式都通过 shared/music.schema.json 和 backend/src/validate.ts 跨字段校验。
5. simulator 只解析 Music JSON 并调用 core；C11 core 完成唯一一份 KS 合成和 WAV 输出。
6. firmware 通过 EXTRA_COMPONENT_DIRS 引用原 core，播放前完整校验 JSON。

core 不依赖 HTTP、JSON、ESP-IDF、FreeRTOS、Node 或 Python。
后端不合成音频；模拟器和固件不重新解释 chords，音符来源仅为 events。
宿主固件测试用显式 fake I2S/HTTP 边界，不能证明 ESP-IDF 编译或真实硬件可用。

## AI 边界

ModelClient 只定义 run(model, input)，测试用内存假客户端注入 env.AI。
无绑定、缺 run 或无法访问 run 返回 503/AI_UNAVAILABLE。
文本 rules、mock、health、info 不读取 AI 方法，也不调用模型。
info 的 ai_configured 只报告绑定存在，不表示调用成功。

模型只输出 mood、bpm、root、mode、bars、style。禁止 events、chords 或额外字段。
字符串必须是完整 JSON，拒绝代码围栏和尾随文本；也支持官方 JSON Mode 的对象 response。
类型、范围、字段集合及 mood/root/mode/style 的预设对应关系均在本地复核。
语法或意图非法时仅重试一次；两次非法返回 502/AI_OUTPUT_INVALID。
run 异常立即返回 502/MODEL_ERROR；错误不携带原始模型输出、令牌或堆栈。
不把模型原文写入下一次提示；AI 错误不降级、不改标为 rules。

通过校验的 mood 映射到 SPEC 的四套固定预设，bars 必须为 4。
适配层只把预设关键词和校验后的 BPM 交给 generateRules，不实现第二套音符生成器。
显式文本 BPM 优先于模型 BPM；seed 使用用户值或原始去首尾空白文本的 FNV-1a。
root/mode/style 仅接受现有预设组合，不扩展调性、节奏或小节数。

## 本地与云端边界

backend/wrangler.jsonc 不配置 AI 绑定，默认 ALLOW_DEPLOY=0。
本地测试使用 --local，真实 HTTP 请求只访问 127.0.0.1，模型测试只用假客户端。
ASR 已实现：WAV 校验通过后，一次 env.AI.run 调用多语言 ASR。
完整 WAV 编为 Base64，task=transcribe，不指定 language（自动识别原语言）。
缺绑定 503/ASR_UNAVAILABLE；失败/非法输出 502/ASR_FAILED；空转写 502/EMPTY_TRANSCRIPTION。
hint 接受但忽略；只有 text 输出能进入生成器。音频 ai 的 LLM 最多一次，无重试/降级。
成功响应含可选 transcript，schema/C 解析器验证后忽略，合成只依赖原有音乐字段。
生产配置 backend/wrangler.production.jsonc 绑定 AI 并强制认证；2026-10-13 已正式部署并通过 12 个真实音乐请求。
输入语音为 Windows TTS，完整真人测试和 ESP32 硬件仍待验证；本地回归与云端验收分别记录。

## 官方资料核对（2026-10-13）

Context7 查询因 registry.npmjs.org DNS ENOTFOUND 失败，改核对官方网页。
选用 @cf/meta/llama-3.3-70b-instruct-fp8-fast：模型页仍在目录，JSON Mode 支持列表包含它。
旧 llama-3.1-8b-instruct 模型页重定向至 unavailable，未采用该名称。
ASR 核对 @cf/openai/whisper-large-v3-turbo，接受 Base64 audio、task=transcribe，返回 text。
Context7 本次仍因 npm DNS ENOTFOUND 不可用；直接读取官方模型页及原始输入/输出 schema。

- [模型页](https://developers.cloudflare.com/workers-ai/models/llama-3.3-70b-instruct-fp8-fast/)
- [绑定与 run 接口](https://developers.cloudflare.com/workers-ai/configuration/bindings/)
- [JSON Mode](https://developers.cloudflare.com/workers-ai/features/json-mode/)
- [多语言 ASR](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/)
- [ASR 输入 schema](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/schema-input.json)
- [ASR 输出 schema](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/schema-output.json)

请求使用 messages、stream=false、temperature=0、max_tokens=256 和
response_format={type:json_schema,json_schema:...}。服务端 JSON Mode 不能替代本地校验。
文档核对与代码适配不能单独作为真实模型证据；实际成功证据另见 artifacts/cloud/verification.json。
