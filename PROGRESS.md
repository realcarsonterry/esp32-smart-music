# PROGRESS

## 当前阶段
Cloudflare 生产 API 已部署，真实 LLM 与 ASR（中文 TTS 样本）验收 PASS：6 项接口检查、12 个音乐请求，全部通过共享 C KS 渲染。
线上地址：https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev 。证据见 artifacts/cloud/verification.json。
阶段 8 本地验收已完成：文本规则、KS、真正本地 Worker HTTP → Music JSON → 同一 C KS 引擎 → WAV — PASS。
阶段 7 固件源码、宿主测试与 ESP-IDF 5.3.5 的 ESP32-S3 干净构建已完成，硬件验收仍为 BLOCKED。
当前提交状态、最新命令与证据见 docs/final-review-20261013.md；提交步骤见 START_HERE.md。
本次修订统一 request_id ASCII 契约、补回归并改进脚本可移植性；最新命令和退出码见 docs/acceptance-report.md。
后续 ASR 适配与离线测试已完成，详情见 docs/asr-offline-verification.md；下文阶段记录保留历史状态。

## 已完成
- 写入 SPEC.md、schema、fixtures、验收表模板。
- 添加顶层 CMake，使用 C11、关闭 C 扩展；GCC 使用 -fno-fast-math，MSVC 使用 /fp:strict。
- core 使用固定数组保存协议数据，不依赖 JSON 库；调用者提供不透明 context 的内存，初始化校验并复制数据，不分配内存。
- 校验协议版本、结构与数量边界、BPM、MIDI、事件/和弦时间、唯一 id、同时发声数和伴奏音级；时间区间为左闭右开。
- 完成 uint64 采样位置换算、独立状态的 xorshift32、float 到 PCM16 转换和单声道 32000 Hz / 16-bit little-endian WAV 写出。
- CTest 20 项、后端规则与 AI 假客户端测试、固件宿主 2 项，以及本地 HTTP 集成测试已通过；历史命令见后续阶段记录。
- 阶段 7 固件控制/录音/传输/播放源码及 fake I2S/HTTP 宿主边界测试已实现。
- 阶段 8 AI 意图适配和多语言 ASR 适配已实现；离线回归使用假客户端，后续生产服务已完成真实模型调用。
- 音频 rules/ai 流程及 transcript 的 schema/C 兼容已实现，中文 TTS 输入经云端 ASR 后可由共享 C KS 渲染。

## 未完成
- 真人语音完整测试尚未完成；真实 ASR 服务的中文 TTS 样本验收已通过。
- ESP32 硬件录放音/实时性/运行时内存与音质尚未验证。

## 阻塞
- ESP-IDF 5.3.5 平台编译已通过，云端与真实模型已有验收证据；当前缺少开发板及音频外设。
- 后续为历史阶段记录，工具绝对路径仅记录当时环境，不是新机器的安装或 PATH 要求。

## 阶段 2 验收：C 校验与时间 — PASS

实际执行日期：2026-10-13（Asia/Singapore）。

项目根：C:\Users\Terry\Desktop\context。

系统：Microsoft Windows NT 10.0.26200.0，X64；PowerShell 7.6.6。

工具：CMake 4.4.4；GCC 16.2.0（w64devkit 2.10.0）；Ninja 生成器。

在 PowerShell 中为本次构建设置进程环境：

```powershell
$env:PATH = 'C:\Program Files\CMake\bin;C:\Users\Terry\AppData\Local\Temp\context-stage2-tools\w64devkit\bin;' + $env:PATH
$env:CMAKE_GENERATOR = 'Ninja'
$env:CC = 'gcc'
```

实际执行命令及退出码：

```text
cmake -S . -B build                            -> 0
cmake --build build                           -> 0
ctest --test-dir build --output-on-failure     -> 0
```

CTest 实际结果：100% tests passed out of 13，0 failed。

覆盖：SPEC 四个 tick 向量；所有允许 BPM 与 tick 的正反换算；seed 0 内部状态非零；相同 seed 序列可重复及状态隔离；valid-one-bar 的 C 结构体；BPM/MIDI 越界；超时及整数溢出；九音重叠与八音允许；非法协议版本；数据结构、伴奏音级、context 初始化、PCM 转换。

WAV 测试实际写出 16 个采样，并检查 RIFF/WAVE、PCM、单声道、32000 Hz、16-bit、data 长度 32 字节、文件总长 76 字节及每个采样的小端字节。

规格澄清：SPEC 中 seed 0 的十进制值 2164325893 与十六进制 A5A5A5A5 不一致；用户明确确认采用十进制 2164325893（0x8100FE05）。实现与测试按此确认执行，输入数据的 seed 仍保留 0，SPEC.md 未修改。

工具链来源：https://github.com/skeeto/w64devkit/releases/tag/v2.10.0。
下载文件 w64devkit-x64-2.10.0.7z.exe 的 SHA256：18d0a4c71a166f8401ab6305781bec5882b40b5e06ba9807c61cb5f3b3c6325e；与官方发布资产 digest 相同。

## 阶段 3 验收：KS 与分块 — PASS

实际执行日期：2026-10-13（Asia/Singapore）。项目根和工具环境同阶段 2：
Windows NT 10.0.26200.0 X64 / PowerShell 7.6.6 / CMake 4.4.4 /
GCC 16.2.0（w64devkit 2.10.0）/ Ninja；core 和测试禁用 fast-math。

先找到根目录 SPEC.md，并读取 C API 与 KS；未读取 build 目录中间文件。
首次 ctest 调用因进程 PATH 未包含 CMake 而无法启动；恢复阶段 2 记录的
进程 PATH 后，修改代码前执行以下命令，现有 13/13 测试全部通过：

```text
ctest --test-dir build --output-on-failure     -> 0
```

实现 music_render_reset、music_render_next_pcm16，调用者提供全部上下文内存，
渲染路径无 malloc；八条独立延迟线和固定 640-float 释放累加环。
保留起步公式 delay=32000/frequency-0.5，N=floor(delay)，
frac=delay-N，a=(1-frac)/(1+frac)，写回系数 pow(0.996,1/N)。
力度增益固定 velocity/127*0.12，混音限幅 ±0.98，释放长度 640。
未使用正弦波、Python KS 或修改 SPEC/测试容差。

首次音高测试及其余 KS 测试：

```text
ctest --test-dir build -R '^ks_' --verbose     -> 0，6/6 通过
```

结果：artifacts/stage-3-ks-attempt-1.txt。
连续音高失败次数为 0；四音在首次测试中通过，未调整分数延迟或环路顺序。
Goertzel 使用 [1600,32000) 采样，无额外加窗；±40 音分扫描，步长 1，
容差保持 ±20；单音 bpm=60、duration_tick=960、velocity=100。

```text
MIDI 52: target=164.813778456 Hz measured=164.813778456 Hz cents=+0
MIDI 57: target=220.000000000 Hz measured=220.000000000 Hz cents=+0
MIDI 64: target=329.627556913 Hz measured=329.627556913 Hz cents=+0
MIDI 69: target=440.000000000 Hz measured=440.000000000 Hz cents=+0
```

速度 40/100 的 RMS=184.654901/461.633989；比值 0.400003。
释放前 160/后 160 采样的 RMS=334.434196/45.978392；逐采样对照持续音
验证 640 采样线性包络（量化误差不超过 1），之后为 0。
八音 PCM 峰值=16157，限幅=32112；第九个重叠音校验及初始化均拒绝。
无序事件、同采样结束/触发及释放重叠场景中，整段与 256/1 采样分块的
35549 个采样逐样本一致。seed 0、1、UINT32_MAX 的两次渲染，以及
播放中 reset 后的输出逐字节相同；不同 seed 的 PCM 不同。

最终在上述进程环境中实际执行：

```text
cmake -S . -B build                            -> 0
cmake --build build                           -> 0
ctest --test-dir build --output-on-failure     -> 0，19/19 通过，0 failed
```

结果路径（相对项目根）：artifacts/stage-3-configure.txt、
artifacts/stage-3-build.txt、artifacts/stage-3-ctest.txt。
全部测试通过后才写入 docs/audio-engine.md，记录最终延迟公式，少于 120 行。
验收表的“KS 音高”和“分块一致”更新为 PASS。
阶段范围止于 3；未执行 push 或部署。

## 阶段 4 验收：模拟器 WAV — PASS

实际执行日期：2026-10-13（Asia/Singapore）。项目根：
C:\Users\Terry\Desktop\context。环境：Windows NT 10.0.26200.0 X64 /
PowerShell 7.6.6 / CMake 4.4.4 / GCC 16.2.0（w64devkit 2.10.0）/ Ninja。

先定位根目录 SPEC.md。第一次以裸命令运行 ctest 时 PATH 未包含 CMake，
命令未启动；读取已有构建缓存中的路径，使用绝对路径重跑，修改前的
19/19 测试全部通过，退出码 0。之后读取协议、C API、脚本退出码和 schema。

实际执行的预检、构建和回归命令（PowerShell）：

```powershell
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build --output-on-failure
# 退出码 0，修改前 19/19
$env:PATH = 'C:/Program Files/CMake/bin;C:/Users/Terry/AppData/Local/Temp/context-stage2-tools/w64devkit/bin;' + $env:PATH
cmake -S . -B build
# 退出码 0
cmake --build build
# 退出码 0
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build --output-on-failure
# 退出码 0，20/20
pwsh -NoProfile -File tests/verify-simulator.ps1 2>&1 | Tee-Object -FilePath artifacts/stage-4-simulator.txt
# 退出码 0
```

验收脚本实际调用的成功命令：

```text
build/simulator.exe shared/fixtures/valid-one-bar.json artifacts/valid-one-bar.wav
-> 退出码 0；samples=80000 rate=32000 channels=1 bits=16
```

独立读取文件字节确认：RIFF/WAVE、PCM format=1、32000 Hz、单声道、
16-bit little-endian，data=160000 字节、总文件 160044 字节、恰好 80000
个采样。前 32000 个采样中 20631 个非零，不是全静音。
WAV SHA256：eafd5740612c7757c9baf424fe6f3dcf15b4222b6aeffe55c84adb32498a5f49。

五个非法夹具分别调用 `build/simulator.exe <fixture.json> <独立输出.wav>`：
invalid-bpm、invalid-midi、invalid-overflow-time、invalid-polyphony、
invalid-version 均退出 2，输出路径均不存在；精确命令和结果见
artifacts/stage-4-simulator.txt。CTest 还验证已有输出不被非法输入改写，
JSON 缺字段/多字段/重复字段、类型错误、非整数、整数越界、非法数字语法、
重复 id、伴奏音级、内嵌 NUL、截断和多余 JSON，以及 seed=0/UINT32_MAX
两次 WAV 一致；CLI 参数/输入读取失败退出 5，WAV 写出失败退出 3。

simulator/vendor/cjson 为官方 v1.7.19 原始源码，附 MIT 许可证和来源哈希；
cJSON 只链接 simulator 目标。core 源码及其接口未修改。
模拟器校验整包后才渲染，以 256 采样块调用阶段 3 的 C 渲染器，再调用
music_write_wav。CLI 输出长度按 bars 的 tick 总长度换算，不额外写入
core API 提供的 640 采样释放尾部，因此单小节夹具正好 80000。

结果路径：artifacts/valid-one-bar.wav（遵循 .gitignore，仅保留本地），
artifacts/stage-4-simulator.txt、artifacts/stage-4-ctest.txt。
验收表仅将“模拟器 WAV”更新为 PASS；没有启动 HTTP、编写后端、push 或部署。

## 阶段 5 验收：后端规则 — PASS

实际执行日期：2026-10-13（Asia/Singapore）。项目根：
C:\Users\Terry\Desktop\context。环境：Windows NT 10.0.26200.0 X64 /
PowerShell 7.6.6 / Node 24.19.0 / npm 11.17.0。

定位并阅读根目录 SPEC.md，重点为“规则生成器”和“HTTP”；读取两个
schema、夹具与现有 core 校验语义。core、SPEC、schema 均未修改。

新增 TypeScript Worker，兼容日期 2026-10-13；直接依赖只有 typescript、
tsx、wrangler、ajv、ajv-formats。实际安装版本分别为 5.9.3、4.23.15、
4.149.0、8.20.0、3.0.1，保留 package-lock.json。

纯函数生成器实现四预设、准确和弦 MIDI、拨弦间隔/错开/时值/力度、
关键词优先级、显式 BPM 与 FNV-1a 缺省 seed。旋律固定每小节四次 RNG
调用，按小节/拍顺序共 16 次，候选和弦音在 MIDI 60..76；顺序注释在代码中。
seed=0 内部状态继续采用阶段 2 用户已确认的十进制 2164325893。

实现 health/info、文本生成、multipart WAV 内容校验、实际字节流大小限制、
可选 POST bearer 认证与错误封装。ai 始终 AI_UNAVAILABLE / 503；通过音频
校验后 ASR_UNAVAILABLE / 503，不把 hint 当成转写。未调用任何真实模型。
成功 Music JSON 在响应前通过 Ajv 2020-12 schema 及独立跨字段校验。

首次沙盒内 npm 安装与 Context7 查询因 DNS 失败/等待未完成而停止；随后
在允许网络的执行环境安装成功。文档查询退回 Cloudflare 官方文档，未登录。
首轮类型检查退出 2（Uint8Array 的 ArrayBufferLike 标注），首轮 Node 测试
退出 1（22 项断言通过，但 Node multipart 出站编码流在取消后产生异步异常）。
已将 readBody 返回类型精确标注为 ArrayBuffer，并先把超大 multipart 测试
输入编码为字节再模拟入站请求；服务端实际流字节限制与取消逻辑保留。
修正后最终实际命令（PowerShell）：

```powershell
npm install --no-fund --no-audit --fetch-retries=0 --fetch-timeout=15000 2>&1 | Tee-Object -FilePath artifacts/stage-5-install.txt
# 退出码 0
npm run typecheck 2>&1 | Tee-Object -FilePath artifacts/stage-5-typecheck.txt
# 退出码 0
npm test 2>&1 | Tee-Object -FilePath artifacts/stage-5-tests.txt
# 退出码 0；22 tests，22 pass，0 fail
npm ls --depth=0
# 退出码 0；仅指定的五项直接依赖
git diff --exit-code -- core shared SPEC.md
# 退出码 0；core、schema、SPEC 未改动
git diff --check
# 退出码 0
```

执行时均保存 `$LASTEXITCODE`，日志末尾记录真实 EXIT_CODE。安装提示 esbuild
和 workerd 的 postinstall 脚本未批准；未运行批准命令，现有平台可选包已让
tsx 的全部测试正常执行。本阶段不要求或声称验证 Wrangler/workerd 进程。

测试直接调用 handleRequest / 默认 fetch，无监听端口：四指定中文句子固定
seed=1 的调式/BPM/和弦顺序/每个伴奏 MIDI/tick/力度/时值；四个 FNV 十进制
向量 691577472、3350900807、1415375574、3245418374；ASCII 首尾空白与无
Unicode 规范化；独立 RNG、seed 0 和两次完整 JSON 相同；空文本、500 字边界、
非法 seed、BPM 10、关键词优先级、合法 BPM 仅覆盖速度、九音重叠夹具及八音
同 tick 切换、重复 id、事件/和弦超界、伴奏跨和弦/间隙、允许旋律和弦外音；
request_id 回显；health/info；AI/网络调用禁止；mock 双重开关；WAV 格式、
损坏/截断/静音/空文件/四秒边界/超长/未知 chunk；1000000 字节限制与伪造
Content-Length；认证与错误 schema。所有测试中的 Music 成功结果均检查
shared/music.schema.json 和跨字段合法性。

证据：artifacts/stage-5-install.txt、artifacts/stage-5-typecheck.txt、
artifacts/stage-5-tests.txt、artifacts/stage-5-environment.txt。
新增 .env.example，没有创建真实 .env。验收表仅更新“后端规则”为 PASS。
仅完成阶段 5；未启动 Wrangler、登录、部署、push 或推进阶段 6/8。

## 阶段 6 验收：本地 HTTP 端到端 — PASS

实际执行日期：2026-10-13（Asia/Singapore）。项目根：
C:\Users\Terry\Desktop\context。环境：Windows NT 10.0.26200.0 X64 /
Windows PowerShell 5.1.26100.9444 / Node 24.19.0 / npm 11.17.0 /
Wrangler 4.149.0 / workerd 1.20261006.1 / CMake 4.4.4 / GCC 16.2.0 / Ninja。
五个新 ps1 和集成脚本均为 UTF-8 BOM，并均在 PowerShell 5.1 实际执行。
外层 PowerShell 7.6.6 保存 UTF-8 验收日志。

先定位 SPEC.md，核对阶段边界、协议、HTTP、退出码。修改前阶段 4 的构建、
20/20 CTest、WAV 格式/80000 采样/非静音/五个非法夹具验收全部退出 0；阶段 5
npm run typecheck 与 npm test 均退出 0、22/22，通过后才编写阶段 6。
首次裸 cmake 因 PATH 缺 CMake 未执行测试；恢复已有工具链 PATH 后才获得
上述结果。预检证据见 artifacts/stage-6-precheck-{build,ctest,wav}.txt。

新增 scripts/check-environment.ps1、setup.ps1、test.ps1、run-simulator.ps1、
smoke-test.ps1、WorkerJob.cs 和 tests/verify-stage6.ps1。环境检查复用 CMake
缓存中的工具链路径。run 接收 -Text/-Mode，选择空闲的 8787 或 8877，启动
自己的 Wrangler，等待 /health 并验证监听 PID 属于自己的 Windows Job Object；
请求 generate-from-text，保存 UTF-8 无 BOM 原始响应 JSON，调用阶段 4 的
simulator.exe 整包校验后用同一 C core 渲染，打印所需字段和 WAV 绝对路径。

进程以 CREATE_SUSPENDED | CREATE_NO_WINDOW 创建，先加入独立 Job，再恢复执行。
Job 设置 KILL_ON_JOB_CLOSE，所以子进程也被约束；finally 只关闭自己的 Job。
没有按端口或进程名终止进程。stdin 为 NUL、CI=true、遥测关闭，检测登录提示即
记录 BLOCKED 并停止；结束后恢复相关环境变量。默认只覆盖 generated_music.json
和 generated_music.wav 及本次 Worker 日志，不删除其他文件。

实际遇到并解决的问题：Context7 查询 DNS ENOTFOUND，改查 Cloudflare 官方文档
和本机 dev --help。首轮使用 `wrangler dev --offline --local`，真实报
`Unknown argument: offline`，run/smoke 退出 5；用户明确同意改用 `--local`
禁用远程绑定。未改 SPEC、未更换依赖或用其他 HTTP 框架替代 Worker。
随后沙盒内真正 workerd 报 GetFinalPathNameByHandleW access denied，退出 5；
在获准的沙盒外运行后通过。没有出现登录要求，没有登录、部署或 push。
npm ci 提示 esbuild/workerd postinstall 未批准，未运行 approve-scripts；
已安装的平台包仍实际运行了测试和 Worker。

最终实际命令（项目根），退出码均为 0：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/check-environment.ps1 -RequireBuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/setup.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/verify-stage6.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-test.ps1
```

setup 后再运行 test：20/20 CTest、阶段 4 WAV、typecheck、22/22 后端测试均通过。
集成验收持有 8787 的 TcpListener，run 使用 8877 成功生成，原监听存活，8877
释放。双端口占用时 run 退出 5，两个原监听存活且已有 JSON/WAV 哈希不变。
mode=ai 的真实 HTTP 返回 503 / AI_UNAVAILABLE，run 退出 4，保存错误 JSON，
已有 WAV 哈希不变，两个端口释放。最后再运行 smoke 恢复 rules 成功产物。

最终 smoke 内部实际启动：

```text
node node_modules/wrangler/bin/wrangler.js dev --local --config backend/wrangler.jsonc --ip 127.0.0.1 --port 8787 --inspector-port 0 --show-interactive-dev-session=false
```

POST http://127.0.0.1:8787/api/v1/generate-from-text，text 为
生成一段温暖、轻快、适合夏日海边的木吉他伴奏，mode=rules。
HTTP 200；request_id=stage-6-bc7b1295d2f74bdf9ec3db8d44663257；
seed=691577472；C major；bpm=96；bars=4；chords=C,G,Am,F，独立断言均通过。
实际 simulator 命令与退出码：

```text
C:\Users\Terry\Desktop\context\build\simulator.exe C:\Users\Terry\Desktop\context\artifacts\generated_music.json C:\Users\Terry\Desktop\context\artifacts\generated_music.wav
-> 0
```

独立读取 WAV：RIFF/WAVE、format=1、单声道、32000 Hz、16-bit little-endian PCM，
字节率 64000、块对齐 2、data=640000 字节、总长 640044 字节、320000 采样
（10 秒）均通过。非零采样 296729，首秒非零采样 30547，峰值 6503，
RMS=1080.37424241556。SHA256：
d3358f8cb9dba6f5a25aba1d0030d8578f4d4b9c2ed1c8cd326736f245a94bba。
整首和弦没有执行单音 ±20 音分测试；原有 CTest 音高测试仍只针对四个单音。

WAV 绝对路径：C:\Users\Terry\Desktop\context\artifacts\generated_music.wav。
JSON 为同目录 generated_music.json；二者按既有 .gitignore 保留本地。
证据：artifacts/stage-6-environment.txt、stage-6-setup.txt、stage-6-tests.txt、
stage-6-integration.txt、stage-6-smoke.txt、stage-6-worker.log；日志记录真实退出码。
仅更新“本地 HTTP 端到端”为 PASS；未推进阶段 7/8。

Wrangler 官方参数参考：https://developers.cloudflare.com/workers/wrangler/commands/workers/。

## 阶段 7：ESP32-S3 固件源码（2026-10-13）

只执行阶段 7。已阅读 SPEC.md 全文，重点核对“固件”、协议、HTTP、C API 与阶段边界。
原 KS 源码、规则生成器、模拟器解析器、根 CMake 和所有原测试断言未修改。
最初裸 ctest 因 PATH 缺 CMake 未执行；复用已有 build/CMakeCache.txt 指定的
`C:/Program Files/CMake/bin/ctest.exe`，实际预检退出 0、20/20，通过后才写固件。
记录见 artifacts/stage-7-preflight-ctest.txt；没有失败测试后继续实施。

新增 firmware ESP-IDF 工程，目标 esp32s3，通过 EXTRA_COMPONENT_DIRS 引用 ../core。
core 只新增 IDF 注册 CMakeLists.txt，没有复制或更改 music_core.c/h。
直接编译原 simulator/music_json.c，链接 IDF json/cJSON 组件；firmware/components 不存在。
唯一引擎 core/music_core.c SHA256：
02425FD70DF5147504B050F3A016DBC9345ED2BED5F3A92A318F11959C1BE4D4。

实现 IDLE→RECORDING→UPLOADING→WAITING_FOR_MUSIC→VALIDATING_MUSIC→PLAYING→IDLE。
独立按钮触发，错误回 IDLE；初始化失败要求修正配置后重启。64000 个 16 kHz 样本、
32-bit stereo I2S slot 提取 INMP441 高 24 位，再缩至 int16，44 字节 WAV 头。
控制任务上传 multipart audio；URL、Wi-Fi 密码与令牌从菜单/NVS 读，defaults 留空。
LAN HTTP 显式启用且限制 RFC1918 字面地址；公网 HTTPS 用 CA bundle、主机名检查和
NTP 校时，拒绝不安全 TLS 配置和重定向。完整接收并限制响应大小、校验后才交音频任务。
音频任务静态栈/队列，每块最多 256 个 32 kHz 样本，左右复制；按实际 I2S 写入数量
续写当前块，尾部写静音排空 DMA；没有 HTTP/JSON/malloc/日志或 vTaskDelay 节拍。
唯一直接包含的 I2S 头为 driver/i2s_std.h。没有实际录音、烧录、硬件操作或模型调用。

按 find-docs 技能尝试 npx ctx7 library 查询，因 registry.npmjs.org DNS ENOTFOUND
退出 1；改读 Espressif 5.3.2 官方 I2S/HTTP 文档、i2s_common.h、esp_http_client.h、
esp_netif_sntp.h 和 INMP441 官方资料。未登录、未安装 ESP-IDF。
链接见 docs/hardware.md；接线全部明确为未验证建议。

独立宿主工程 firmware/tests 编译真实 audio.c、wire.c、transport.c 和原 core/解析器。
显式 fake I2S/HTTP 只在该测试工程使用，不能充当 IDF 编译或硬件证据。
验证 64000 采样、符号和 slot 转换、WAV 头、状态回退与 URL 限制；以 13 帧短读、
7 帧短写和带进度超时确认不丢采样。80640 个音乐/释放样本两声道逐样本等于原 core，
随后 5×256 帧静音，零进度错误关闭通道。HTTP 测试以 31 字节短写确认 multipart WAV
不被响应覆盖，覆盖缓存/分块响应读取、截断、超限、503/302 拒绝和清理。
开发中修复新 wire 函数声明的 array-parameter 编译警告与新 HTTP 测试的 signedness
警告；没有放宽测试或更改已有断言。这些均为宿主编译，不是 IDF 构建尝试。

实际执行命令（在项目根；现有 CMake/GCC bin 加入当前进程 PATH）：

```powershell
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build --output-on-failure
& 'C:/Program Files/CMake/bin/cmake.exe' -S firmware/tests -B build/stage7-host -G Ninja '-DCMAKE_C_COMPILER=C:/Users/Terry/AppData/Local/Temp/context-stage2-tools/w64devkit/bin/gcc.exe'
& 'C:/Program Files/CMake/bin/cmake.exe' --build build/stage7-host
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build/stage7-host --output-on-failure -V
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build --output-on-failure
git diff --exit-code -- SPEC.md core/music_core.c core/music_core.h simulator backend tests CMakeLists.txt
git diff --check
```

最终上述命令全部退出 0。原 CTest 20/20，新增宿主测试 2/2。
环境 Windows NT 10.0.26200.0 X64 / PowerShell 7.6.6 / GCC 16.2.0 / CMake 4.4.4。
详细退出码在 artifacts/stage-7-verification.txt；其余证据为 stage-7-host-configure.txt、
stage-7-host-build.txt、stage-7-host-tests.txt、stage-7-final-ctest.txt、stage-7-source-audit.txt。

`Get-Command idf.py -ErrorAction SilentlyContinue` 未找到；IDF_PATH/IDF_TOOLS_PATH 未设置，
C:/Espressif、C:/Users/Terry/esp、C:/Users/Terry/.espressif 均不存在；环境检查命令退出 0。
证据 artifacts/stage-7-idf-environment.txt。依要求未安装、未运行 set-target/build，
没有 firmware bin。由于不存在 idf.py，最多 3 轮 IDF 编译修复规则没有触发。
firmware/README.md 中的 idf.py 命令仅为以后执行的说明，不能作为成功证据。

验收：固件源码 PARTIAL（实现及宿主边界测试完成，完整平台编译和网络互操作未验证）；
固件构建 BLOCKED；真实硬件 BLOCKED。docs/hardware.md 记录待测 GPIO、供电、slot、
实时性、内存余量及音质。后端阶段 7 音频请求仍可能按契约返回 ASR_UNAVAILABLE，
未提前实现阶段 8。按用户要求准备本地提交 stage-7-firmware，不 push。

## 阶段 8：AI 适配、文档与总回归（2026-10-13）

只执行阶段 8。完整读取 SPEC.md 和原验收报告。没有新产品端点，没有登录、
真实模型调用、部署或 push；ASR 保持未实现。环境中账户 ID/API Token 均不存在，
仅记录布尔存在性；即使存在，本阶段也禁止真实模型调用。

新增 backend/src/ai.ts，ModelClient.run 通过绑定注入，所有测试为内存假客户端。
官方核对在实现前完成：find-docs 的 Context7 CLI 查询因 registry.npmjs.org DNS
ENOTFOUND 失败；改读取 Cloudflare 模型页、绑定 API 和 JSON Mode 官方文档。
旧 llama-3.1-8b-instruct 模型页重定向至 unavailable，选用仍在目录且 JSON Mode
支持列表列出的 @cf/meta/llama-3.3-70b-instruct-fp8-fast。官方链接见 architecture.md。
没有 ASR 模型代码，不使用英文专用模型。wrangler.jsonc 未配置 AI 绑定。

无/坏绑定返回 AI_UNAVAILABLE；只解析 response 内严格六字段意图。
语法/字段/预设关系非法重试一次，第二次非法 AI_OUTPUT_INVALID；run 异常 MODEL_ERROR。
模型原文不进入错误响应、重试提示或 events。有效意图映射到 SPEC 四个既有预设，
调用原 generateRules 生成 chords/events，原文 FNV seed、显式 BPM 优先级不变。
rules 只抽取了共享输入校验，预设、音符算法和 RNG 调用顺序不变。
旧 22 项测试断言全部保留，仅改一个过时的测试标题；增加 9 项假客户端测试。

实际执行 npm run typecheck、npm test：0，31/31。
新增 scripts/verify-stage8.ps1 运行现有全部验收，并按命令保存环境/退出码/输出。
第一次沙盒内配置、C/后端及固件宿主测试通过，但 workerd 本地 SQLite 路径访问
GetFinalPathNameByHandleW 被拒，smoke 退出 5。失败证据保留为
stage-8-smoke-attempt-1.txt、stage-8-verification-attempt-1.txt。
Wrangler 版本查询的缺省用户日志路径也曾 EPERM；验收入口将日志指向忽略的工作区路径。
通过 require_escalated 审核后在沙盒外重跑同一完整本地测试，没有登录或模型请求。

最终命令：pwsh -NoProfile -File scripts/verify-stage8.ps1，退出 0。
内部配置/构建、scripts/test.ps1、详细 CTest、固件宿主配置/构建/测试、smoke-test、
verify-stage6 和最后恢复成功 JSON/WAV 的 smoke-test 均退出 0。
原 CTest 20/20；KS 四个目标 MIDI 均 +0 音分，35549 样本 whole/256/1 一致；
后端 31/31（含原 22）；固件宿主 2/2（fake I2S/HTTP，不是 IDF 编译）。
真实本地 HTTP 200，C major/96/C,G,Am,F，WAV 320000 样本/640044 字节，
mono/32000 Hz/PCM16 LE，非零 296729；SHA256
D3358F8CB9DBA6F5A25ABA1D0030D8578F4D4B9C2ED1C8CD326736F245A94BBA。
端口 8787→8877、双占用内部退出 5、AI 未配置 503 内部退出 4、原文件与监听器保留、
仅清理自有进程全部通过。最终 JSON/WAV 恢复为成功配对，忽略文件不提交。

环境为 Windows NT 10.0.26200.0 X64 / PowerShell 7.6.6（测试子脚本 5.1.26100.9444）/
Node 24.19.0 / npm 11.17.0 / CMake 4.4.4 / GCC 16.2.0 / Wrangler 4.149.0。
每个命令和绝对结果路径见 artifacts/stage-8-verification.txt；结果来自本次输出。
新增 architecture/protocol/testing 文档（58/59/57 行），根/后端 README 更新为真实状态。
审计命令 pwsh -NoProfile -File scripts/audit-stage8.ps1：0；规格六项通过，原 core、
固件、模拟器、C 测试、schema、配置无修改，原后端断言完整保留。
验收表逐行替换为本次证据，AI 真实调用、固件构建、真实硬件、云端部署 BLOCKED；
固件源码仍 PARTIAL。没有 idf.py，不安装 ESP-IDF，不生成 bin。
本地提交消息 stage-8-acceptance，不 push。
