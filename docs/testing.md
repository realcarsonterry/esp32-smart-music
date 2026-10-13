# 测试与证据（阶段 8）

所有 PASS 必须来自实际命令输出、退出码和环境记录；当前汇总见 final-review-20261013.md。
acceptance-report.md 保留历史阶段证据。
原有测试保留，原 KS 源码、容差和测试断言没有改变。

## 实际运行入口

在项目根运行过 `pwsh -NoProfile -File scripts/verify-stage8.ps1`。
首次先 npm ci（或 scripts/setup.ps1），并将 CMake、GCC/Clang、Ninja 放入 PATH。
验收脚本优先 PATH，仅在工具缺失时兼容有效缓存路径；明确将同一编译器/生成器传给根工程与宿主测试，不安装工具、不登录、不部署、不配置 AI 绑定。
其真实子命令和退出码逐条记录在 artifacts/stage-8-verification.txt。

- cmake -S . -B build；scripts/test.ps1 构建后运行 CTest、verify-simulator、typecheck、npm test。
- ctest --test-dir build --output-on-failure -V：保留音分、分块和 seed 输出。
- cmake -S firmware/tests -B build/stage7-host；构建后运行宿主 CTest。
- scripts/smoke-test.ps1：真实本地 Worker 中文 rules → Music JSON → 原 C 引擎 → WAV。
- tests/verify-stage6.ps1：端口回退、双端口占用、HTTP 503、文件保留与进程清理。
- 再次 smoke-test：将集成错误用例留下的 JSON 恢复为成功的 JSON/WAV 对。

子脚本使用当前 PowerShell 宿主；入口使用 PowerShell 7。每个工具的版本命令单独检查退出码，分别见 stage-8-tools-*.txt。
README 的首次准备命令是使用说明；本次成功证据以 acceptance-report.md 和实际日志为准。

## 覆盖范围

C/CTest：协议/结构、BPM/MIDI、时间/跨度/复音、RNG、WAV、KS 音高/力度/释放、
分块逐样本一致及 reset/seed 确定性；模拟器覆盖合法与非法夹具。
音高仅测试 MIDI 52/57/64/69，按 SPEC 的 Goertzel 窗口与 ±20 音分容差；不测整首音分。

后端原测试：四个预设精确和弦/拨弦 tick、FNV 向量、关键词优先级、显式 BPM、
确定性、schema/跨字段校验、请求 id、认证、大小限制、WAV 和错误协议。

request_id 回归包含审查中的 22 个汉字和嵌入 NUL，以及空格、换行、空串、65 字节等。
非法请求必须 HTTP 400 / INVALID_REQUEST，错误标识符仍合法；合法 1/64 字节和标点可回显。
node --import tsx scripts/review-probes.mjs 验证内存 Worker 到 C；不替代真实 HTTP 测试。

新增 AI 假客户端测试：

- 无/坏绑定返回 503/AI_UNAVAILABLE。
- 非法 JSON 恰好重试一次；两次非法返回 502/AI_OUTPUT_INVALID。
- 第一次非法、第二次合法可成功；字段非法也执行同样重试上限。
- 六字段严格校验，拒绝多/缺字段、events/chords、错误类型/范围/预设组合。
- 四种有效意图与原规则生成器结果完全一致，仅 mode=ai；原文 seed 和显式 BPM 保留。
- 模型异常返回 502/MODEL_ERROR，错误脱敏，失败没有 rules 回退。
- 非法文本请求在模型调用前拒绝；文本 rules/default/mock/health/info 假模型调用次数为 0。

ASR 离线测试见 backend/tests/asr.test.mjs：无绑定、一次成功识别、空转写、
异常/非法输出脱敏、非法 WAV/认证/大小/模式在调用前拒绝、hint 不替代文本。
音频 rules 最多一次 ASR；ai 最多一次 ASR 加一次 LLM，不重试/降级。
review-probes 用假客户端串起音频请求与真实 C 模拟器，带/不带 transcript 的 WAV
逐字节相同；同时验证 Unicode 元数据边界和类型/长度非法被拒绝。
本次命令与结果见 asr-offline-verification.md，合成 PCM 不是实际语音证据。

固件宿主测试编译真实 audio/wire/transport/core 源码，使用显式 fake I2S/HTTP。
它验证短读写、状态恢复、slot/WAV、原 core PCM 与传输边界，不等于 IDF 或硬件验收。

## 失败与重跑

首次沙盒内 HTTP 测试因 workerd 的 GetFinalPathNameByHandleW 拒绝访问退出 5。
失败保留在 stage-8-smoke-attempt-1.txt、stage-8-verification-attempt-1.txt。
通过沙盒外执行权限重跑相同 --local 测试；最终结果以本次重跑输出为准。
不删除失败用例、不放宽断言。内部预期错误退出 2/4/5 与测试入口失败区分记录。

## 仍未验证

AI 真实调用 BLOCKED：阶段 8 禁止，凭据仅检查存在性，不输出值。
本地配置没有 AI 绑定；ASR 适配已实现，真实语音与模型调用仍为 BLOCKED。
固件平台构建 PASS：本次使用已有 ESP-IDF 5.3.5 在全新目录完成 build/size/size-components，未烧录。
真实硬件与云端部署 BLOCKED：本阶段不连接/录音/烧录，不登录/部署/push。
可选阶段 9 如另获授权，真实调用还须同时具有账户 ID 与 API Token。
