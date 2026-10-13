# 面试提交检查与 Windows 验证指南

> **此文件为较早一轮审查的历史记录，请勿按下文旧状态提交。** request_id 问题已修复，ASR 已实现并离线测试，
> ESP-IDF 平台构建也已通过。最新结论见 [最终软件审查](final-review-20261013.md)，
> 手把手演示、补云端和打包说明见 [START_HERE](../START_HERE.md)。历史复现失败不代表当前探针失败。

检查日期：2026-10-13，Asia/Singapore。检查范围包括 backend、core、simulator、firmware、schema、测试、脚本及提交说明；未连接硬件、未安装 ESP-IDF、未登录或部署 Cloudflare、未调用真实模型。

## 提交结论

不建议把当前整个文件夹直接作为“题目全部完成”的最终结果发送。可以把它作为“可运行的无硬件原型 + ESP32-S3 固件源码”提交，并明确列出限制。

已经跑通的是：中文文本 → 本地 Cloudflare Worker 的规则生成 → Music JSON → 与固件共享的 C11 KS 引擎 → 可播放 WAV。

尚未跑通的是：语音 → ASR → 音乐生成；公网 API；真正的 ESP-IDF 平台构建；ESP32 的录音、实时合成和播放。没有开发板可以解释最后一项，但不能解释 ASR、云端 API 和平台编译的缺失，这些都能用 Windows 电脑完成或验证。

项目自己的 SPEC 把“无硬件闭环”缩小为文本 rules 流程。满足这个自定范围，不等于满足面试原题的全部要求。具体歌曲名目前也不会变成歌曲检索或指定歌曲重现，而是生成原创拨弦伴奏。

## 本次实测

1. 现有完整回归：`pwsh -NoProfile -File scripts/verify-stage8.ps1`，退出 0。
   - TypeScript 类型检查通过，后端 31/31 项通过。
   - C/KS/模拟器 CTest 20/20 项通过。
   - 固件宿主测试 2/2 项通过；只编译 audio/wire/transport 等宿主可测文件，使用 fake I2S/HTTP，没有编译和运行完整 app_main/config 控制流程。
   - 真实本地 HTTP、端口回退、占用保护、错误响应和进程清理通过。
   - 最后一次 smoke 生成 96 BPM、C 大调、C/G/Am/F、4 小节；WAV 是单声道 32000 Hz、PCM16、320000 采样，10 秒，640044 字节。
   - 峰值 6503，RMS 1080.374242；四个被测单音 MIDI 52/57/64/69 的音高扫描均为 0 音分偏差。
   - 成功日志：`artifacts/review-regression-unsandboxed-20261013.txt`。

2. 全新 C 构建目录 `build/review-clean`：配置、构建、CTest 均退出 0，20/20 通过。说明源代码能重新构建，不是只运行旧 exe。
   - 证据：`artifacts/review-clean-{configure,build,tests}-20261013.txt`。
   - 本次明确指定已有 GCC 和 Ninja 的路径；没有安装新工具。

3. 新增跨语言探针：`node --import tsx scripts/review-probes.mjs`，退出 1，复现两个真实契约问题。
   - 普通 ASCII request_id：HTTP 200，C 模拟器退出 0。
   - 22 个汉字的 request_id：HTTP 200，C 模拟器退出 2 / INVALID_MUSIC_DATA。
   - 含 NUL 的 request_id：HTTP 200，C 模拟器退出 2 / INVALID_MUSIC_DATA。
   - 格式合法的合成测试 WAV：音频上传接口返回 HTTP 503 / ASR_UNAVAILABLE。它不是语音样本，不能作为识别测试。
   - 证据：`artifacts/review-probes-20261013.txt`。探针退出 1 是发现问题，不是通过。

4. 文件沙盒内第一次回归退出 1，底层原因是 workerd 的 GetFinalPathNameByHandleW 拒绝访问；取得沙盒外运行权限后，同一入口成功。失败日志保留于 `artifacts/review-regression-20261013.txt`。这不是 ASR 或固件成功的证据，也不意味着用户普通 PowerShell 必须以管理员身份运行。

本次验证更新了现有 stage-6/stage-8 部分日志。未修改 backend/core/firmware 的业务代码、SPEC、schema 或原有测试断言。新增的是本报告、独立复现脚本和 review 日志。

## 必须优先补齐的部分

### 1. 实现音频接口中的 ASR

位置：`backend/src/worker.ts:119`。代码检查 WAV 后直接抛 ASR_UNAVAILABLE，没有调用识别服务；固件只调用这个音频接口，因此按键录音主流程在这里必然中断。

建议新增 `backend/src/asr.ts`，保留现有音频格式校验，完成：

1. 未配置识别服务时继续返回 ASR_UNAVAILABLE。
2. 把已校验 WAV 按所选模型要求编码，调用一次中文/多语言 ASR。
3. 校验模型返回类型和非空转写文本；空结果返回 EMPTY_TRANSCRIPTION；识别服务异常返回 ASR_FAILED，避免暴露原始异常。
4. 将转写文本交给现有 `generateAI` 或明确选择的 `generateRules`，再执行 `assertMusic` 返回结构化数据。保留 request_id。hint 仅作为提示，不能冒充识别结果。
5. 加入无绑定、有效转写、空转写、模型失败、非法音频不调用模型、调用次数上限及“音频请求最终返回合法 Music JSON”的测试。

可核对 Cloudflare 的 `@cf/openai/whisper-large-v3-turbo`；官方模型页提供 audio、language、task 参数及 text 输出。输入编码要按该模型的当前文档实现，不能把其他 whisper 模型的参数直接照搬。真实语音至少试“温暖轻快”“悲伤慢”“摇滚 120 BPM”三类，并保存 HTTP 状态、转写和音乐结果。当前未进行这些真实调用。

官方资料：[ASR 模型](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/)。

### 2. 给出可访问的 Cloudflare API 和真实调用证据

位置：`backend/wrangler.jsonc`、`backend/src/ai.ts`。现有配置没有 AI binding，ai 模式测试只注入假客户端。`ai_configured=true` 也仅说明有绑定，不证明模型运行成功。

建议保留现有纯本地配置，新增单独的生产配置 `backend/wrangler.production.jsonc`，按官方文档配置 `ai.binding = "AI"`；公开部署时设置 AUTH_REQUIRED=1，用 Wrangler secret 保存 DEVICE_TOKEN，禁止把令牌写入 vars、源码或报告。原有 `ALLOW_DEPLOY=0` 只是变量，不能自动阻止直接执行 Wrangler 部署命令。

部署完成后保存 HTTPS URL，并验证 health、正确令牌成功、错误令牌 401、文本生成、真实 WAV 上传、非法音频及 Music JSON 能被 C 模拟器解析播放。ai 模式需要一次真实成功结果。只有 health 成功不代表整条链路成功。

现有 `scripts/run-simulator.ps1` 总是启动本地 Worker，不能用来证明远端 API。可给它增加 `-BackendUrl` 参数：提供时直接请求远端，不启动/停止本地 Worker；另增 `-AudioPath`，用同一协议上传电脑上的 WAV，再把响应送进原 C 模拟器。两种入口共用合成器。

官方资料：[AI binding](https://developers.cloudflare.com/workers-ai/configuration/bindings/)、[JSON Mode](https://developers.cloudflare.com/workers-ai/features/json-mode/)。当前 ai.ts 的 JSON Mode 请求形状与官方示例一致；仍需真实服务验证。

本报告没有执行登录、部署或云端调用。进入该阶段前，应明确更新 SPEC 中阶段 8 禁止真实调用/安装 IDF 的旧开发范围，并遵守已有部署授权要求。

### 3. 无开发板也要做一次 ESP-IDF 构建

位置：整个 `firmware/`。宿主 fake 测试无法检查真实 IDF 头文件、组件依赖、Kconfig、链接、目标内存布局或完整控制任务。

在 Windows 安装与工程一致的 ESP-IDF 5.3.x 工具链，建议固定具体补丁版本；打开 ESP-IDF 的终端后运行：

```powershell
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
idf.py -C firmware size
idf.py -C firmware size-components
```

这些是后续待执行命令，本次没有运行成功。编译不需要开发板；flash/monitor、I2S 波形和实时性测量才需要硬件。当前 SPEC 明确要求未安装 IDF 时不自动安装，因此本次只检查了环境，未安装它。

要交付构建命令、固定 SDK 版本、退出码、size 报告；如提供 bin，必须来自真实构建。保留“硬件未验证”说明。

硬件到位后重点测：4 秒录音的真实 WAV 与左右 slot；Wi-Fi/HTTPS 失败恢复；最大复音时每块最坏渲染耗时；DMA underrun；连续循环后的堆和任务栈余量。256/32000 = 8 ms 是每块音频对应的时间，不是目前已经测得的 ESP32 合成耗时。core 在 note-off 时预计算 640 采样尾音，必须包含在最坏耗时测试里。

arena 约 128 KB，另有全局 music、core context、cJSON 堆、Wi-Fi、TLS、DMA 和任务栈，不能只报延迟线内存。ESP-IDF size 能看静态占用；运行期的剩余堆/最大连续块仍需板上测量。

官方资料：[Windows 安装](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/get-started/windows-setup.html)、[内存与 size](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/system/mem_alloc.html)。

## 已确认的代码和交付问题

### request_id 的跨语言契约不一致

位置：`backend/src/protocol.ts:49`、`shared/music.schema.json`、`simulator/music_json.c:111`、`core/music_core.h`。

后端/schema 允许最多 64 个 Unicode 字符，但 C 的 `request_id[65]` 只容纳 64 个 UTF-8 字节；22 个汉字占 66 字节，后端返回的合法结果无法被设备接收。后端也允许 NUL，C 解析器主动拒绝 `\u0000`。

最简单的修法是约定 request_id 为 ASCII 标识符，统一修改 `validRequestId`、成功/错误 schema 和协议说明，例如：

```typescript
export function validRequestId(value: unknown): value is string {
  return typeof value === "string" && /^[A-Za-z0-9._:-]{1,64}$/.test(value);
}
```

对应 schema 加同样的 pattern。这个修法收紧了原协议，需同步 SPEC 和消费者。若必须支持 64 个 Unicode 字符，则扩容 C 字段到最多 256 字节加结尾 NUL，同时仍排除 NUL，并增加所有相关边界测试。不要只改后端的长度判断而保留相互矛盾的 schema。

将本次两个样例加入后端→C 的回归。修好后 `scripts/review-probes.mjs` 应退出 0：要么接口明确拒绝这些输入，要么返回结果可被 C 接受。

### 测试启动和打包不便移植

`scripts/verify-stage8.ps1:38` 写死 Terry 的 CMake 和临时 GCC 路径；这个工具版本检查包含多个命令，只看最后一条的退出码，前面某个工具缺失可能被掩盖。`check-environment.ps1` 靠已有 build 缓存恢复工具路径，别人解压源码后不一定有相同环境；全新 firmware/tests 目录也不能依赖本机旧生成器缓存。

建议为 setup/verify 增加 Compiler、Generator、BuildDir 参数或采用 CMake presets；明确配置 Ninja 和有效 C 编译器，传给根工程与宿主固件测试；版本检查逐个验证退出码。首次安装使用 `npm ci` 固定依赖，不能只要求“已有 node_modules 和 build”。版本示例应使用接收者工具路径，不应依赖 Temp 下的文件永久存在。

### 提交文档存在矛盾

`PROGRESS.md:15` 仍写阶段 7、8 未开始，与源码、README 和后面的记录矛盾。README 的开头主要在解释阶段限制，应改为产品介绍、与原题的对应关系、最快验证方式、已知限制。

建议首页明确标记：文本 rules/KS/桌面闭环已验证；ASR 未实现；AI 只做假客户端测试；ESP-IDF 未构建；硬件与云端未验证。功能补齐后依据新证据更新状态，保留历史日志，但不要用历史 PASS 替代当前全链路验收。

## 你现在这台电脑可以怎么验证

以下命令从当前项目根目录执行。已有依赖/工具可用；不要先删除 build，它目前记录了便携工具链路径。

### 一键回归和试听

```powershell
Set-Location 'C:\Users\Terry\Desktop\context'
pwsh -NoProfile -File scripts/verify-stage8.ps1
$LASTEXITCODE
Start-Process '.\artifacts\generated_music.wav'
```

预期最终出现 Stage 8 regression: PASS，退出码 0。中间的 HTTP 503 / EXIT_CODE=4、双端口占用 / EXIT_CODE=5 是集成测试刻意验证的错误场景，需看最外层结果。最后 smoke 会恢复成功配对的 JSON/WAV。

试听用于确认音色、节奏和主观可用性；文件非静音、音高测试通过不等于已经人工判断音质好。本次没有播放扬声器或做人工听感判断。

如果 PowerShell 没有 pwsh，可先分别运行现有分项脚本；现有总验收记录使用的是 PowerShell 7。若出现 workerd 路径拒绝访问，请先在电脑普通终端运行，不要把智能体文件沙盒的问题直接解释成需要管理员权限。

### 输入不同需求

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-simulator.ps1 -Text '来一段悲伤、慢的木吉他'
Start-Process '.\artifacts\generated_music.wav'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-simulator.ps1 -Text '强烈、摇滚的木吉他伴奏 120BPM'
Start-Process '.\artifacts\generated_music.wav'
```

预期悲伤为 A 小调、72 BPM、Am/G/F/Em；摇滚为 E 小调、120 BPM、Em/D/C/D。这是规则生成，不是两次真实 LLM 调用。每次覆盖同一对 generated_music 文件，需要保存多个样例时先复制到自己命名的文件。

### 单独查看 API

第一个终端在项目根启动：

```powershell
npm.cmd run dev
```

第二个终端在项目根请求：

```powershell
Invoke-RestMethod 'http://127.0.0.1:8787/health'
Invoke-RestMethod 'http://127.0.0.1:8787/api/v1/info'
$body = @{ text = '温暖轻快 110BPM'; mode = 'rules'; request_id = 'manual-001' } | ConvertTo-Json -Compress
$utf8Body = [Text.Encoding]::UTF8.GetBytes($body)
$music = Invoke-RestMethod 'http://127.0.0.1:8787/api/v1/generate-from-text' -Method Post -ContentType 'application/json; charset=utf-8' -Body $utf8Body
$music | Select-Object bpm, key, mode
```

用 Ctrl+C 结束该手动 Worker，再跑总回归；总回归的端口占用测试要求 8787/8877 最初为空。

### 重现新发现的问题

```powershell
node --import tsx scripts/review-probes.mjs
$LASTEXITCODE
```

当前预期退出 1，并显示两个 API/C 不一致。脚本直接调用本地内存中的 Worker，生成 review-* 文件，不发网络请求，不录音，不启动云服务；不要把它当成真实 HTTP 或 ASR 测试。它不会覆盖 generated_music 样例。

## 建议发送的交付包

保留：backend/src、backend/tests、backend/wrangler 配置、core、完整 simulator（包括 vendor/cjson 及许可证）、firmware、shared、tests、scripts、docs、README、SPEC、修正后的 PROGRESS、CMakeLists、package.json、package-lock.json、tsconfig、无密钥的配置样例。附上 selected 测试日志、至少一个配对的 Music JSON/WAV，以及简短演示录屏。

排除：node_modules、.git、build 缓存、.wrangler、真实 .env/.dev.vars、带凭据的 sdkconfig、内部调试日志和无关临时文件。WAV/JSON 被 .gitignore 忽略，单纯 git archive 不会包含它们，需主动加入演示包。不要把整个 Desktop/context 原样压缩后发送。

在补齐前，提交说明可以写：

> 已完成规则文本意图解析、结构化音乐协议、共享 C11 Karplus–Strong 多声部引擎、Windows 模拟器及 ESP32-S3 固件源码。桌面端真实本地 HTTP 到 WAV 闭环通过，包含后端、KS 和固件宿主测试。当前无硬件；ASR、云端部署、真实模型调用及 ESP-IDF 平台构建尚未完成，硬件录放音和实时性尚未验证。

若要按原题提交较完整的软件结果，优先顺序是：补 ASR → 真实 Cloudflare 语音/文本链路 → IDF 编译 → 修 request_id 与可移植脚本 → 更新文档和打包。实际硬件项目继续明确标注未验证，不需要伪装成已经上板。
