# 当前软件审查与交付状态

检查日期：2026-10-13（Asia/Singapore）。本文件是当前验收状态；其他阶段报告保留为历史证据。

## 提交判断

可以提交为“可复现的软件原型与 ESP32-S3 固件源码”，并提供电脑现场演示。
尚不能声称原题要求的云端语音与 ESP32 实机全链路已经通过验收。
公开 GitHub 仓库能方便 HR 与技术面试官阅读，但不会自动补齐这些验收项，也不会自动部署 Worker。

检查了 backend 的 Worker/ASR/AI/规则/校验器、schema、共享 C11 KS、JSON 解析器、
固件控制/Wi-Fi/HTTPS/I2S 源码，以及测试、脚本和提交文档。没有连接硬件、录音、人工试听或调用真实模型。
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
- **新增 API 客户端 PASS（仅本地）**：demo-api.mjs 通过真实 loopback HTTP 生成 110 BPM，
  严格校验后交给 C 渲染；缺令牌得到 401，非法 WAV 在上传前被拒绝。
- **生产配置 PARTIAL**：独立 wrangler.production.jsonc 有 AI 绑定、强制认证、禁用 mock，
  Wrangler deploy --dry-run 打包退出 0。dry-run 没有上传代码，没有公网 URL，也不验证模型服务。

标准 WAV SHA256：`D3358F8CB9DBA6F5A25ABA1D0030D8578F4D4B9C2ED1C8CD326736F245A94BBA`。

## 证据与命令

以下日志均来自本次实际运行，成功命令退出 0：

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
  [生产配置打包](../artifacts/final-review-production-dry-run-20261013.txt)。

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

1. **真实 ASR / LLM / 云端 BLOCKED**：默认本地配置没有 AI binding，音频路由返回 503/ASR_UNAVAILABLE。
   新配置只完成打包。还需本人账号部署、真实中文语音上传、核对 transcript 与 Music JSON，并验证认证和公网 C 渲染。
   缺少开发板不是这些项目未完成的原因。具体步骤见 START_HERE.md。
2. **ESP32 实机 BLOCKED**：没有开发板、I2S 麦克风、功放和扬声器，无法验证实际采样、
   引脚、位对齐、Wi-Fi/TLS 运行、DMA 连续性、最坏多声部算力、运行时堆/栈和真实声音。
   电脑 WAV、fake I2S、可烧录格式的 bin 都不能替代这些物理测量。
3. **生成能力有限 PARTIAL**：四种预设、固定 4 小节/4/4，具体歌曲名不会触发歌曲搜索或重现。
   LLM 是意图适配，不能称为任意编曲模型。是否满足出题人对“想要的歌曲”的定义，应如实解释。

## 本次修改

统一当前文档与历史状态，补 Windows 演示入口、独立生产配置、支持文本/WAV 的 API 客户端、
白名单打包脚本与 GitHub 提交说明。KS 算法、原规则与测试断言没有改变，也未伪造真实识别或硬件结果。
提交包排除缓存与凭据；重新打包会包含当前工作区中的最新源码，不依赖它们是否已被 Git 跟踪。
