# 当前软件审查与交付状态

检查日期：2026-10-13（Asia/Singapore）。本文件是当前验收状态；其他阶段报告保留为历史证据。

## 提交判断

可以提交为“可复现的软件原型与 ESP32-S3 固件源码”，并提供电脑现场演示。
Cloudflare 正式部署、真实 LLM 与真实 ASR 服务调用已通过验收，语音样本为中文 Windows TTS。
真人语音完整测试和 ESP32 实机仍未通过验收，不能声称开发板录音到播放的全链路已完成。

线上 API：**https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev**。
[health](https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev/health) 与
[info](https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev/api/v1/info) 可匿名检查；生成接口要求 bearer 令牌。

检查了 backend 的 Worker/ASR/AI/规则/校验器、schema、共享 C11 KS、JSON 解析器、
固件控制/Wi-Fi/HTTPS/I2S 源码，以及测试、脚本和提交文档。已正式部署并调用真实模型，未连接 ESP32 硬件。
真人录音仅开始了一个样本，完整真人验收未完成；没有人工音质评分或实机音质测量。
本次未发现现有测试和已审代码中必须修改才能通过本地文本闭环的阻断问题；这不等于代码在实机上没有问题。

## 已有实现与实际验证

- **后端 PASS（本地与离线）**：TypeScript 类型检查成功，40/40 后端测试通过。
  rules 根据关键词和显式 BPM 选择四种预设，再生成和弦、拨弦事件和基础旋律。
  ASR 经多语言 whisper-large-v3-turbo 适配调用，LLM 只提取六字段意图，音乐事件仍由规则产生。
  假客户端测试验证合法/非法模型响应、认证、音频格式、调用次数、错误处理和无静默降级。
- **C/KS/模拟器 PASS**：20/20 CTest，通过音高、力度、释放、多声部上限、确定性、
  分块一致性与严格 JSON 接收检查。音高只测 MIDI 52/57/64/69，四项测得 0 音分偏差，
  不能将其扩大为全 MIDI 范围或整个 ESP32 播放音准的实测结论。
- **固件宿主 PASS**：2/2，使用真实 audio/wire/transport/core 源码与 fake I2S/HTTP，
  验证 slot/WAV、短读写、超时、传输完整性和错误恢复。宿主测试不执行完整设备控制/Wi-Fi 实机流程。
- **真实本地 HTTP PASS**：本地 workerd 接收中文文本，返回 Music JSON，由共享 C KS 生成
  96 BPM、C major、C/G/Am/F、4 小节 WAV。32000 Hz、单声道 PCM16、320000 样本、640044 字节。
  WAV 峰值 6503、RMS 1080.37424241556；非静音不代表已人工评估音质。
- **跨语言探针 PASS**：request_id 边界正常；假 ASR rules/ai 输出可被 C 接收，
  transcript 的有无不改变 PCM；发现契约问题 0。此探针不是实际识别测试。
- **ESP-IDF 平台构建 PASS**：已安装的 ESP-IDF v5.3.5、Xtensa GCC 13.2.0、目标 esp32s3，
  在全新 build/final-idf 目录，以单独的空凭据配置完成 build、size、size-components，退出 0。
  已编译完整 app_main/config/网络/I2S/共享核心，不只是宿主 stubs。
  本机产物 `build/final-idf/stage_7_firmware.bin`：957040 字节；应用分区 1048576 字节，
  剩余 91536 字节（工具显示 9%）。SHA256：
  `04A6DEFDD0EF91136FA813DAED1A816B750DF6C1ED4957A8BC16E3A2D876A241`。
  size 报告 DIRAM 静态使用 266911/341760 字节，剩余 74849 字节；这不是开机后的空闲堆，
  不能据此承诺 Wi-Fi/TLS、JSON 解析与 KS 上下文同时运行时内存充足。
- **新增电脑演示 PASS**：scripts/demo.ps1 -NoPlay 实际生成摇滚 120 BPM、E minor、Em/D/C/D、
  256000 样本 WAV；自动打开播放器的分支交由用户现场操作，未模拟人工听感。
- **API 客户端 PASS（本地与云端）**：demo-api.mjs 通过真实 loopback HTTP 生成 110 BPM，
  严格校验后交给 C 渲染；缺令牌得到 401，非法 WAV 在上传前被拒绝。
  随后已请求生产 HTTPS API 的真实 LLM 模式并经共享 C KS 输出 WAV。
- **Cloudflare 正式部署 PASS**：wrangler.production.jsonc 已上线，AI 绑定启用、AUTH_REQUIRED=1、mock 禁用。
  Worker 为 esp32-smart-music-api，部署版本 925721b1-5edf-4993-8875-ada3d1afe54e，部署命令退出 0。
  DEVICE_TOKEN 作为 Cloudflare Secret 保存，不在源码、公开日志或仓库中。
- **真实云端验收 PASS（限定样本）**：2026-10-13 20:01:25 至 20:01:57（Asia/Singapore），
  health/info、无令牌 401、错令牌 401、非法音频 400、超大请求 413 共 6 项通过；
  温暖轻快/悲伤慢/摇滚 120 BPM 三组文本各走 rules/ai，三组中文 TTS WAV 各走 ASR+rules/ASR+ai，
  共 12 个音乐请求通过。每个 JSON 都经严格校验和实际 C 解析/KS 渲染，WAV 格式、长度、非静音与 SHA256 均记录。
  实际转写分别为“温暖轻快的吉他伴奏”“悲伤慢的吉他伴奏”“摇滚 120 BPM”；速度为 96/72/120 BPM，
  调性为 C major/A minor/E minor。输入来源是 Microsoft Huihui Desktop TTS，不是真人或 ESP32 麦克风录音。

标准 WAV SHA256：`D3358F8CB9DBA6F5A25ABA1D0030D8578F4D4B9C2ED1C8CD326736F245A94BBA`。

## 证据与命令

以下日志均来自本次实际运行，成功命令退出 0：

- `node node_modules/wrangler/bin/wrangler.js deploy --config backend/wrangler.production.jsonc` →
  [正式部署日志](../artifacts/cloud-deploy-20261013.txt)。
- 设置 MUSIC_BACKEND_URL 与私有 MUSIC_DEVICE_TOKEN 后，
  `node --tls-max-v1.2 --import tsx scripts/verify-cloud.mjs` →
  [真实云端验收日志](../artifacts/cloud-verification-20261013.txt)、
  [逐项结果/时间/音频哈希](../artifacts/cloud/verification.json)、
  [输入来源](../artifacts/cloud/inputs/provenance.json)。全部配对 JSON/WAV 在 artifacts/cloud/results。
  TLS 1.2 选项用于本机 Node 的握手兼容，保留服务器证书校验；没有禁用 TLS 验证。

- `pwsh -NoProfile -File scripts/verify-stage8.ps1` →
  [完整本地回归](../artifacts/final-review-regression-local-20261013.txt)。
- `node --import tsx scripts/review-probes.mjs` →
  [跨语言探针](../artifacts/final-review-probes-20261013.txt)。
- `idf.py --version` → [IDF 版本](../artifacts/final-review-idf-version-20261013.txt)。
- 下列 IDF 命令 → [干净平台构建与体积](../artifacts/final-review-idf-build-20261013.txt)。
- `pwsh -NoProfile -File scripts/demo.ps1 -NoPlay -Text '强烈、摇滚的木吉他伴奏 120BPM'` →
  [电脑演示](../artifacts/final-review-demo-20261013.txt)。
- 本地带认证 Worker → API 客户端 → C 模拟器（成功/缺令牌/非法音频）→
  [API 客户端集成](../artifacts/final-review-api-client-20261013.txt)。
- `node node_modules/wrangler/bin/wrangler.js deploy --dry-run --config backend/wrangler.production.jsonc --outdir build/worker-production` →
  [生产配置历史打包](../artifacts/final-review-production-dry-run-20261013.txt)。dry-run 早于正式部署，不能单独作为上线证据。

本机干净 IDF 构建实际使用以下命令；只适用于本机已有安装。其他电脑应使用自己的 ESP-IDF 5.3.5 环境。

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\python_env\idf5.3_py3.11_env'
$env:PATH="$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1
& "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" C:\Espressif\frameworks\esp-idf-v5.3.5\tools\idf.py -C firmware -B C:/Users/Terry/Desktop/context/build/final-idf -D SDKCONFIG=C:/Users/Terry/Desktop/context/build/final-idf-sdkconfig build size size-components
```

未运行 set-target：全新配置已经由工程固定为 esp32s3。没有安装工具，没有烧录。
检查四项 FW_WIFI_SSID/FW_WIFI_PASSWORD/FW_DEVICE_TOKEN/FW_BACKEND_URL 均为空，仅记录空值检查结果，未打印用户凭据。

第一次沙盒内 HTTP 运行遇 workerd GetFinalPathNameByHandleW 拒绝访问，IDF 工具遇 DLL 限制；
切换至本机正常执行权限后，固定已有 Python 3.11 虚拟环境，再完成上述验证。
较早初始化曾选中不存在的 Python 3.13 IDF 环境；这是终端环境选择问题，不是固件编译错误。
一次 IDF 相对构建路径解析到了工作区外，已改用上述项目内绝对路径重新干净构建；本报告只引用项目内最终产物。
失败尝试日志保留在本机 artifacts，不作为成功证据，也不要求用户普通终端必须用管理员权限。

## 仍需补齐的部分

1. **真人语音完整测试 BLOCKED**：真实 ASR/LLM 和公网链路已通过上述 TTS 样本验收，
   真人麦克风三组完整验证尚未完成，不能将 TTS 结果当作真人识别准确率评估。
   默认本地配置无 AI binding 时仍返回 503/ASR_UNAVAILABLE；这不代表生产服务未配置。
2. **ESP32 实机 BLOCKED**：没有开发板、I2S 麦克风、功放和扬声器，无法验证实际采样、
   引脚、位对齐、Wi-Fi/TLS 运行、DMA 连续性、最坏多声部算力、运行时堆/栈和真实声音。
   电脑 WAV、fake I2S、可烧录格式的 bin 都不能替代这些物理测量。
3. **生成能力有限 PARTIAL**：四种预设、固定 4 小节/4/4，具体歌曲名不会触发歌曲搜索或重现。
   LLM 是意图适配，不能称为任意编曲模型。是否满足出题人对“想要的歌曲”的定义，应如实解释。

## 本次修改

统一当前文档与历史状态，补 Windows 演示入口、独立生产配置、支持文本/WAV 的 API 客户端、
白名单打包脚本与 GitHub 提交说明。KS 算法、原规则与测试断言没有改变，也未伪造真实识别或硬件结果。
提交包排除缓存与凭据；重新打包会包含当前工作区中的最新源码，不依赖它们是否已被 Git 跟踪。
本次同步正式云端部署及真实调用证据，补入复现脚本、TTS 来源、输入音频、12 组返回 JSON/WAV；
私人录音、访问令牌和 OAuth 配置仍不公开。GitHub 初次推送早于云端验收，此次更新修正这一状态差异。
