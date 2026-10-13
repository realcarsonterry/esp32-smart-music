# 提交与电脑现场演示指南

2026-10-13（Asia/Singapore）。先读此文件；本次状态以 docs/final-review-20261013.md 为准。

交付也可以使用公开 GitHub 仓库链接：HR 免登录阅读 README，技术面试官查看源码和测试，
点击 Code → Download ZIP 下载源码和音频样例。GitHub 仓库不等于 Cloudflare API 已上线。
如需本地 ZIP，仍可按本文最后一节打包。

## 能不能提交

可以提交为“ESP32-S3 智能音乐生成系统的软件原型与固件源码”。不应写成“ESP32 实机闭环已经验收”。
电脑可以现场演示文本需求 → 真实本地 Worker HTTP → 结构化音乐数据 → 与固件共享的 C11 KS 引擎 → WAV 播放。
电脑演示是否能满足面试要求，由面试官决定；不要自行把电脑测试等同于开发板测试。

ASR 和 AI 意图适配已实现并用假客户端测试；默认配置没有 AI binding，真实音频请求会返回 503。
Cloudflare 公网部署和真实模型调用仍未验收。这些缺项能用电脑补齐，与缺少 ESP32 无关。
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

## 如果要补齐公网 API 和真实语音

已经准备 `backend/wrangler.production.jsonc`（AI 绑定、强制 bearer 认证）和 `scripts/demo-api.mjs`。
本次只验证配置可打包和客户端本地 HTTP 工作，**没有替你登录、部署或调用收费模型**。

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

3. 在 PowerShell 7 输入 API 地址，令牌通过隐藏输入写入当前进程环境：

```powershell
$api = Read-Host '粘贴部署输出的 HTTPS 基础地址，不带末尾斜线'
$env:MUSIC_DEVICE_TOKEN = Read-Host '输入刚才的 DEVICE_TOKEN' -MaskInput
Invoke-RestMethod "$api/health"
node --import tsx scripts/demo-api.mjs --url $api --text '温暖轻快 110BPM' --mode rules
node --import tsx scripts/demo-api.mjs --url $api --text '悲伤、慢的吉他伴奏' --mode ai
Start-Process '.\artifacts\api_music.wav'
```

4. 音频使用你自己录制的 **单声道、16000 Hz、16-bit PCM WAV，不超过 4 秒**。
Windows 录音机默认文件可能不是这种格式，不能只改扩展名。不要拿 generated_music.wav 上传：它是 32000 Hz 的音乐，不是语音。
准备好真实语音文件后执行（把路径替换为实际文件）：

```powershell
node --import tsx scripts/demo-api.mjs --url $api --audio 'C:\你的路径\request.wav' --mode rules
Start-Process '.\artifacts\api_music.wav'
node --import tsx scripts/demo-api.mjs --url $api --audio 'C:\你的路径\request.wav' --mode ai
```

至少试“温暖轻快”“悲伤慢”“摇滚 120 BPM”，检查实际 transcript、BPM、调式、和弦，并保存每组 JSON/WAV。
音频 rules 仍调用一次真实 ASR；ai 在 ASR 后额外调用意图模型。成功不是仅看 /health。
若出现 ASR_FAILED、MODEL_ERROR 或 AI_OUTPUT_INVALID，应保留错误并检查模型服务，不能冒充已验证。

5. 验证无令牌请求被拒绝：

```powershell
$savedToken = $env:MUSIC_DEVICE_TOKEN
Remove-Item Env:MUSIC_DEVICE_TOKEN
node --import tsx scripts/demo-api.mjs --url $api --text '温暖轻快'
# 应出现 API HTTP 401: UNAUTHORIZED，退出码 1。
$env:MUSIC_DEVICE_TOKEN = $savedToken
Remove-Variable savedToken
```

演示结束执行 `Remove-Item Env:MUSIC_DEVICE_TOKEN`。不要把令牌或带令牌的截屏放进交付包。
官方依据：[AI binding](https://developers.cloudflare.com/workers-ai/configuration/bindings/)、
[ASR 模型](https://developers.cloudflare.com/workers-ai/models/whisper-large-v3-turbo/)、
[JSON Mode](https://developers.cloudflare.com/workers-ai/features/json-mode/)、
[Secrets](https://developers.cloudflare.com/workers/configuration/secrets/)。模型接口文档核对不等于真实调用成功。

## 没有开发板，怎么向面试官解释

可以直接说：

> 我目前只有 Windows 电脑，没有 ESP32-S3、I2S 麦克风、功放和扬声器，因此没有把开发板录音与实时播放标记为已验证。我用与固件相同的 C11 Karplus–Strong 核心完成了电脑端 HTTP 到 WAV 演示，并做了 ESP32-S3 平台编译和固件宿主边界测试。它们验证了软件逻辑和可编译性，尚不能验证真实 I2S 时序、麦克风有效位对齐、DMA 欠载、运行时内存和声音表现。当前真实云端 ASR/AI 调用也尚未验收，我在提交说明里单独列出。

若已按上节真正补齐云端成功证据，再相应更新最后一句；没有做就保留。
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

> 附件为 ESP32-S3 智能音乐系统的软件原型与固件源码，附 Windows 复现步骤、结构化音乐数据、KS 合成音频样例及测试证据。文本规则 → 本地 HTTP → 共享 C11 KS → WAV 已通过验证，ESP32-S3 固件已完成平台编译。ASR/AI 适配通过离线测试，真实云端调用尚未验收。受限于当前没有开发板及音频外设，实机录音、I2S 播放和实时性暂未验证；我可以先演示电脑端的软件链路。

如果面试方要求完整云端/实机验收，当前包应标为阶段交付，并约定补验收时间；不能保证解释限制后一定被接受。
