# 提交与电脑现场演示指南

2026-10-13（Asia/Singapore）。先读此文件；本次状态以 docs/final-review-20261013.md 为准。

交付也可以使用公开 GitHub 仓库链接：HR 免登录阅读 README，技术面试官查看源码和测试，
点击 Code → Download ZIP 下载源码和音频样例。Cloudflare API 已正式部署，地址见下节。
如需本地 ZIP，仍可按本文最后一节打包。

## 能不能提交

可以提交为“ESP32-S3 智能音乐生成系统的软件原型与固件源码”。不应写成“ESP32 实机闭环已经验收”。
电脑可以现场演示文本需求 → 真实本地 Worker HTTP → 结构化音乐数据 → 与固件共享的 C11 KS 引擎 → WAV 播放。
电脑演示是否能满足面试要求，由面试官决定；不要自行把电脑测试等同于开发板测试。

Cloudflare 公网部署、真实 LLM 和真实 ASR 服务调用已验证：6 项接口/认证检查、12 个文本/音频音乐请求通过，
每个成功响应都经共享 C KS 生成非静音 WAV。语音样本是 Windows 中文 TTS；真人语音完整测试和 ESP32 实机仍待验证。
证据见 [云端日志](artifacts/cloud-verification-20261013.txt) 与 [结果清单](artifacts/cloud/verification.json)。
默认本地配置仍没有 AI binding，本地音频请求返回 503 是预期边界；生产配置含真实绑定。
当前生成的是四种预设下的原创拨弦伴奏和基础旋律，固定 4 小节、4/4；不检索或复现指定歌曲，不生成演唱。

## 你这台电脑现在怎么演示

1. 打开开始菜单，搜索并打开 **PowerShell 7**。使用普通终端即可，不要求管理员。
2. 复制执行：

```powershell
Set-Location 'C:\Users\Terry\Desktop\context'
pwsh -NoProfile -File scripts/demo.ps1
```

3. 等待出现 `HTTP status: 200`、`Simulator: PASS`、`EXIT_CODE=0`；默认播放器会打开约 10 秒的 WAV。
4. 展示终端中的 96 BPM、C major 和 C/G/Am/F，再打开 `artifacts/generated_music.json`，指出 `chords` 和 `events`。
5. 改一个需求，展示速度和调式确实变化：

```powershell
pwsh -NoProfile -File scripts/demo.ps1 -Text '来一段悲伤、慢的木吉他'
pwsh -NoProfile -File scripts/demo.ps1 -Text '强烈、摇滚的木吉他伴奏 120BPM'
```

悲伤预期为 72 BPM、A minor、Am/G/F/Em；摇滚为 120 BPM、E minor、Em/D/C/D。
等上一段播放完再执行下一条。每次覆盖同一对 generated_music 文件。
这个演示使用 **rules**，没有调用真实 LLM；必须如实说明。

6. 需要展示自动测试时执行：

```powershell
node --import tsx scripts/review-probes.mjs
pwsh -NoProfile -File scripts/verify-stage8.ps1
$LASTEXITCODE
```

最外层应为 0 和 `Stage 8 regression: PASS`。集成测试会刻意产生 HTTP 503、EXIT_CODE=4/5，
不能只看其中一行判定失败。总验收前关闭手动启动的 8787/8877 服务。
最后一次 smoke 会恢复标准的成功 JSON/WAV。
如果不方便现场执行，直接双击包内 `artifacts/generated_music.wav` 可试听，但应说明那是保存的样例。

## 在别人的电脑上复现

需要 Node.js >=22、PowerShell 7、CMake >=3.20、GCC/Clang、Ninja，并把工具加入 PATH。
解压到普通可写目录，进入含 SPEC.md 的根目录执行：

```powershell
pwsh -NoProfile -File scripts/setup.ps1
pwsh -NoProfile -File scripts/demo.ps1
```

setup 会用 package-lock.json 执行 npm ci，再构建 C 模拟器。包不携带 node_modules 或机器相关的缓存。
你这台电脑现有 build 保存了便携工具路径，不要为了演示先删除它。换电脑必须配置工具，不能依赖旧缓存。

## 已上线 API 与云端演示

线上地址：**https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev**。
`backend/wrangler.production.jsonc` 已正式部署，包含 AI 绑定和强制 bearer 认证；`scripts/demo-api.mjs` 可直接请求该服务。
2026-10-13 20:01（Asia/Singapore）的正式验收已通过，不需要为了演示重新部署。
只读检查：

```powershell
$api = 'https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev'
Invoke-RestMethod "$api/health"
Invoke-RestMethod "$api/api/v1/info"
```

health 应为 ok=True，info 应为 ai_configured=True、auth_required=True。
生成接口需要从项目拥有者取得 DEVICE_TOKEN；不会在公开仓库提供访问令牌。

### 云端文本与音频生成

先完成上节 setup，保证本机已构建 C 模拟器。PowerShell 7 中通过隐藏输入提供令牌：

```powershell
$api = 'https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev'
$env:MUSIC_DEVICE_TOKEN = Read-Host '输入项目拥有者提供的 DEVICE_TOKEN' -MaskInput
try {
    node --tls-max-v1.2 --import tsx scripts/demo-api.mjs --url $api --text '温暖轻快 110BPM' --mode rules
    node --tls-max-v1.2 --import tsx scripts/demo-api.mjs --url $api --text '悲伤、慢的吉他伴奏' --mode ai
} finally {
    Remove-Item Env:MUSIC_DEVICE_TOKEN -ErrorAction SilentlyContinue
}
Start-Process '.\artifacts\api_music.wav'
```

文本 rules 不调用 LLM；ai 真实调用意图模型。输出保存在 artifacts/api_music.json 与 api_music.wav，每次覆盖。
本机 Node 默认 TLS 曾遇握手错误；--tls-max-v1.2 是兼容选项，仍保留服务器证书校验，不得关闭校验。

已提供三组可复现的 TTS 输入：`artifacts/cloud/inputs/tts-{warm,sad,rock}.wav`。
要演示音频链路，把上面 try 块中的客户端命令替换为：

```powershell
node --tls-max-v1.2 --import tsx scripts/demo-api.mjs --url $api --audio '.\artifacts\cloud\inputs\tts-rock.wav' --mode ai
```

该样本实际转写为“摇滚 120 BPM”，已验证返回 E minor、120 BPM、Em/D/C/D。
语音 rules 也会调用真实 ASR；ai 在 ASR 后额外调用意图模型。TTS 来源见 inputs/provenance.json。
真人录音也可用 --audio 传入，但完整真人测试尚未通过；文件必须为 mono/16kHz/PCM16 WAV，最长 4 秒。

### 复现已完成的云端验收

脚本会调用真实模型并使用账号配额；本次结果已保存在仓库，不必无变化反复运行。

```powershell
$env:MUSIC_BACKEND_URL = 'https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev'
$env:MUSIC_DEVICE_TOKEN = Read-Host '输入 DEVICE_TOKEN' -MaskInput
try {
    node --tls-max-v1.2 --import tsx scripts/verify-cloud.mjs
    $verificationExit = $LASTEXITCODE
} finally {
    Remove-Item Env:MUSIC_DEVICE_TOKEN -ErrorAction SilentlyContinue
    Remove-Item Env:MUSIC_BACKEND_URL -ErrorAction SilentlyContinue
}
$verificationExit
```

预期退出 0、Cloud verification PASS、6 项检查和 12 个音乐请求；结果保存为 artifacts/cloud/verification.json。
输入音频、返回 JSON、合成 WAV 和 SHA256 全部可在 artifacts/cloud 下对照。
如需重新生成 TTS，Windows 上运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-speech-fixtures.ps1`，需要已安装的 zh-CN 声音。

### 仅在自行创建服务或修改后端时部署

1. 在浏览器登录你自己的 Cloudflare 账号，确认 Workers/Workers AI 可用及用量设置。
2. 在项目根执行以下命令。这些命令会修改你的云端账号，只有决定正式部署时才执行。

```powershell
npx.cmd wrangler login
npx.cmd wrangler secret put DEVICE_TOKEN --config backend/wrangler.production.jsonc
npx.cmd wrangler deploy --config backend/wrangler.production.jsonc
```

secret 命令提示输入时填一个随机长令牌，不要填 Cloudflare API Token；这里是你自己的设备访问密码。
若提示创建同名 Worker，可在确认名称属于本项目后接受。部署输出的 HTTPS origin 即 API 地址。
AUTH_REQUIRED=1 时 secret 未配置会拒绝所有生成请求，不会自动放开访问。
ALLOW_DEPLOY 是项目约定，Wrangler 本身不依据它阻止部署。

无令牌请求已实测返回 401/UNAUTHORIZED；可在同一 PowerShell 会话复现：

```powershell
Remove-Item Env:MUSIC_DEVICE_TOKEN -ErrorAction SilentlyContinue
node --tls-max-v1.2 --import tsx scripts/demo-api.mjs --url $api --text '温暖轻快'
# 应出现 API HTTP 401: UNAUTHORIZED，退出码 1。
```

再次生成时需重新输入令牌。不要把令牌或带令牌的截屏放进交付包。
官方依据：[AI binding](https://developers.cloudflare.com/workers-ai/configuration/bindings/)、
[ASR 模型](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/)、
[JSON Mode](https://developers.cloudflare.com/workers-ai/features/json-mode/)、
[Secrets](https://developers.cloudflare.com/workers/configuration/secrets/)。模型接口文档核对不等于真实调用成功。

## 没有开发板，怎么向面试官解释

可以直接说：

> 我目前只有 Windows 电脑，没有 ESP32-S3、I2S 麦克风、功放和扬声器，因此没有把开发板录音与实时播放标记为已验证。我用共享的 C11 Karplus–Strong 核心验证了电脑端音频合成，并完成 ESP32-S3 平台编译。Cloudflare API 已部署，真实 LLM 与 ASR 服务已通过测试，中文语音样本为 Windows TTS。真人语音完整测试和真实 I2S 时序、DMA 连续性、运行时内存及音质仍待验证。

电脑仿真不能产生 ESP32 上的物理 I2S 波形，也不能证明 Xtensa 上最坏情况下能持续供给 DMA。
若必须实机演示，需要借用或取得 ESP32-S3 开发板、INMP441、MAX98357A、匹配扬声器、按钮和连接线；
接线和验证顺序见 docs/hardware.md。不要把这份参考接线称为已经实测。

## 具体发什么、怎么发

1. 先按上面的 demo 命令亲自听一遍；如需录屏，用系统录屏工具录制终端结果、JSON 和实际播放声音。
   确认录屏包含系统声音；本项目没有自动为你录屏，也没有生成不存在的演示视频。
2. 执行总验收恢复标准 JSON/WAV，再运行：

```powershell
pwsh -NoProfile -File scripts/package-submission.ps1
Invoke-Item '.\out'
```

3. 找最新的 `esp32-smart-music-prototype-日期时间.zip`。这个文件才是要发送的附件。
   它包含完整源码/测试/依赖锁文件、说明、WAV/JSON 样例和本次验证日志；不含 .git、node_modules、
   build、.wrangler、真实 .env/.dev.vars、sdkconfig 和可能携带凭据的 bin。
4. 右键 ZIP → 全部解压到一个新目录，核对 START_HERE.md、core/music_core.c、firmware/main/app_main.c、
   backend/src/worker.ts、package-lock.json 和 artifacts/generated_music.wav 都在；双击 WAV 检查附件可读。
5. 在面试官指定的邮箱/聊天/招聘系统中手动添加这个 ZIP；若有录屏，另附录屏。
   没有给定收件人和渠道，本次不会替你发送或公开上传。
6. 可附上这段文字：

> 附件为 ESP32-S3 智能音乐系统的软件原型与固件源码，附复现步骤和测试证据。Cloudflare API 已正式部署，真实模型的文本/中文 TTS 语音 → 音乐数据 → 共享 C11 KS → WAV 已验证，ESP32-S3 固件已编译通过。真人语音完整测试、ESP32 实际录音、I2S 播放和实时性尚未验证；可以演示电脑端和云端的软件链路。

如果面试方要求开发板实机验收，需补齐硬件测试；不能把软件链路通过扩大为实机全部完成。
