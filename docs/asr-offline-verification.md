# ASR 离线验收（2026-10-13，Asia/Singapore）

> 本文保留离线阶段历史证据。后续 IDF 构建和真实云端 ASR/LLM（中文 TTS 输入）均已完成；
> 下文的 BLOCKED/未部署是当时状态，当前状态见 [最终软件审查](final-review-20261013.md)。

状态：ASR 代码与离线测试 PASS；真实语音 / Workers AI 调用 BLOCKED。
根目录：含 SPEC.md 的 C:/Users/Terry/Desktop/context。
环境：Windows NT 10.0.26200.0 X64、PowerShell 7.6.6、Node 24.19.0、npm 11.17.0、
CMake/CTest 4.4.4、GCC 16.2.0、Ninja 1.13.2、Wrangler 4.149.0。
未登录、未部署、未调用真实模型、未安装 ESP-IDF、未改 KS、未 git push。

## 实现

- backend/src/asr.ts 经 env.AI.run 使用 @cf/openai/whisper-large-v3-turbo。
  完整已校验 WAV 编成 Base64，task=transcribe；省略 language 以自动识别原语言。
- 无可用绑定：HTTP 503 / ASR_UNAVAILABLE；空转写：HTTP 502 / EMPTY_TRANSCRIPTION；
  模型异常或非法输出：HTTP 502 / ASR_FAILED。响应不包含原始异常、密钥或模型输出。
- 音频 multipart 接受 mode=rules（默认）或 ai。ASR 最多一次，不重试。
  rules 识别后调用原规则生成器；ai 再做一次 LLM 意图提取，再调用原规则生成器。
  文本 AI 保留原来的最多两次意图调用；音频 AI 最多一次。失败不改 mode、不降级。
- hint 接受但忽略，不能作为转写，也不进入生成器或 LLM。
  request_id 保留已校验的 X-Request-ID；转写文本决定原规则的 seed、BPM 与关键词。
- 所有成功结果带可选 transcript 并经过 assertMusic。
  schema 和共用 C JSON 解析器接受 1..500 个 Unicode 字符，不含 NUL/孤立代理项；
  C 解析器验证后忽略该字段，不扩充 core 数据结构、不改变合成逻辑。

## 官方文档核对

2026-10-13 实际读取官方模型页和原始输入/输出 schema，未运行模型：

- [多语言模型](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/)
- [输入 schema](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/schema-input.json)：audio 支持 Base64 字符串，task 支持 transcribe。
- [输出 schema](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/schema-output.json)：必需 text 字符串。

Context7 查询 `npx.cmd --yes ctx7@latest library cloudflare ...` 因 npm registry DNS
ENOTFOUND 失败（退出 1），因此直接核对以上官方资料，没有通过登录解决查询失败。
未选用英文专用模型。backend/wrangler.jsonc 未新增 AI 绑定，全部模型测试注入内存假客户端。

## 实际命令、退出码和证据

从项目根目录执行：

- `npm.cmd run typecheck`：退出 0。
- `npm.cmd test`：退出 0，40/40 通过。
  日志：artifacts/asr-backend-tests-20261013.txt。
- `node --import tsx scripts/review-probes.mjs`：退出 0，契约问题 0。
  日志：artifacts/asr-review-probes-20261013.txt。
- `pwsh -NoProfile -File scripts/verify-stage8.ps1`：最终退出 0，Stage 8 regression: PASS。
  CTest 20/20、固件宿主 2/2、后端 40/40、真实本地 HTTP smoke/integration 全部通过。
  日志：artifacts/asr-stage8-local-20261013.txt，分项见 artifacts/stage-8-*.txt。

第一次 verify-stage8 在文件沙盒内退出 1：C/后端/宿主均通过，本地 workerd 启动因
GetFinalPathNameByHandleW 拒绝访问而失败。失败日志保留于 artifacts/asr-stage8-20261013.txt。
通过自动审批允许的沙盒外权限重跑相同命令后退出 0，Worker 始终使用 --local。
没有改测试断言、关闭检查或用假结果代替本地 HTTP。

## 覆盖范围与限制

backend/tests/asr.test.mjs 覆盖：无/坏/不可访问绑定；一次成功转写；完整 4 秒 WAV 的
Base64 往返；空转写；模型异常与非法输出；非法 WAV、认证、过大请求、非法/重复模式
在模型调用前拒绝；hint 与转写冲突时只使用转写；rules/ai 调用顺序和上限；
LLM 失败或非法意图不重试/降级；transcript 的 Unicode/类型/长度/NUL 边界。

review-probes 将假 ASR 的 rules 和 ai 响应送入实际重新构建的 C 模拟器，均退出 0。
带 transcript、去掉 transcript、替换为 500 个 emoji 的 WAV 逐字节相同。
非法 transcript 的类型、空值、过长和 NUL 由 C 拒绝，退出 2。
生成的 JSON/WAV 保存在 artifacts/review-asr-{rules,ai}-*.{json,wav}。

合成 PCM 仅用于传输与流程测试，不是实际语音，不证明模型识别准确率。
真实“温暖轻快”“悲伤慢”“摇滚 120 BPM”语音验收：BLOCKED。
云端、ESP-IDF 平台构建和硬件验收：BLOCKED。本任务未尝试这些操作。
