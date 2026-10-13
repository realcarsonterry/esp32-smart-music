# esp32-smart-music

这是 ESP32-S3 智能音乐生成系统的软件原型与固件源码。电脑端通过本地 Cloudflare Worker
生成 BPM、调式、和弦、节奏与基础旋律，再由与固件共享的 C11 Karplus–Strong 引擎输出拨弦音频。

**先读 [提交与现场演示指南](START_HERE.md)**，包含电脑演示命令、发送附件步骤、云端补齐步骤及向面试官说明硬件限制的话术。
最新实际检查见 [最终软件审查](docs/final-review-20261013.md)。

可直接下载 [Music JSON 样例](artifacts/generated_music.json) 和 [KS 合成 WAV 样例](artifacts/generated_music.wav)。
GitHub 托管的是源码与样例，Cloudflare API 的部署是独立步骤。

已验证：文本 rules、ASR/AI 离线适配、KS、本地 HTTP 到 WAV、ESP-IDF 5.3.5 的 ESP32-S3 干净构建。
未验证：真实语音识别、真实 LLM、公网部署、ESP32 实际录音/播放与实时性。

```powershell
# 这台电脑已有构建和依赖，可直接运行；会打开 WAV 播放器。
pwsh -NoProfile -File scripts/demo.ps1
```

这是 ESP32-S3 智能音乐项目的无硬件原型及固件源码。唯一契约为 [SPEC.md](SPEC.md)。
中文文本经本地 Worker rules 生成 Music JSON，再由同一份 C11 KS 引擎渲染 WAV。
ASR/AI 适配仅用假客户端验证；没有登录、真实模型调用、部署或 push。
固件源码有宿主测试，平台构建已通过；真实硬件仍待验证。生成固定 4 小节原创伴奏，不检索或复现指定歌曲。

历史阶段运行结果见 [验收报告](docs/acceptance-report.md)；当前状态以最终软件审查为准。
本次 ASR 离线适配与验证见 [ASR 验收](docs/asr-offline-verification.md)。
Windows 首次使用需要 Node.js >=22、PowerShell 7、CMake >=3.20、GCC 或 Clang 和 Ninja。
把工具放入 PATH，并从含 SPEC.md 的项目根目录执行以下准备命令。首次必须使用 `npm ci`
按 package-lock.json 安装固定依赖；不需要随源码携带 node_modules 或旧 build 缓存。

```powershell
npm ci
pwsh -NoProfile -File scripts/setup.ps1
```

setup.ps1 会执行 npm ci 并构建模拟器；首次也可直接运行 setup.ps1 完成这两步。
脚本优先使用 PATH；PATH 缺工具时仅回退到仍有效的本地 build 缓存路径，不安装工具。
便携 GCC/Clang 可用 `-Compiler 'D:/tools/bin/gcc.exe'` 显式指定；setup、check-environment
和 verify-stage8 均支持此参数，以及 `-Generator Ninja`（也支持 Ninja Multi-Config）。
根工程与全新的固件宿主测试目录使用同一编译器和生成器。切换已有生成器时使用新的源码工作目录，
不要携带其他机器的 build 缓存。workerd 若遇智能体沙盒路径访问限制，在普通本地终端重跑。

完整本地验收（包含下列测试、固件宿主构建/测试及详细 CTest）：

```powershell
node --import tsx scripts/review-probes.mjs
pwsh -NoProfile -File scripts/verify-stage8.ps1
```

review-probes 覆盖内存 Worker 到 C 模拟器的 request_id 和假 ASR 回归；中文、空格、NUL 等
request_id 必须由后端返回 400，成功和错误响应都遵守 `^[A-Za-z0-9._:-]{1,64}$`。
它验证带 transcript 的结果可渲染且 PCM 不变，不证明真实 HTTP 或真实语音识别。

分项命令：

```powershell
npm run typecheck
npm test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/verify-stage6.ps1
```

smoke-test 使用指定中文文本与 rules 模式，检查 96 BPM、C 大调、C/G/Am/F、
seed 691577472，以及 320000 个采样、整体非静音的 mono/32000 Hz/PCM16 WAV。
输出为 artifacts/generated_music.json 和 artifacts/generated_music.wav；允许覆盖
这两个文件。run-simulator 轮询 /health，确认监听进程归属，优先 8787、忙则 8877。
它只停止自己启动的 Worker，错误退出码为 JSON 2、渲染 3、HTTP 4、环境/端口 5。
Wrangler 使用实测可用的 --local；仓库未配置 AI 绑定，不会在 HTTP 测试中调用模型。

AI 请求缺少可用绑定返回 503/AI_UNAVAILABLE。假模型的意图最多尝试两次，
必须严格通过六字段及预设一致性校验，音符始终由原规则生成器产生。
音频接口校验 WAV 后调用一次多语言 ASR（无绑定仍为 503/ASR_UNAVAILABLE）。
multipart mode=rules 识别后按规则生成；mode=ai 再做一次 LLM 意图提取；失败不改 mode。
hint 不作为转写，成功含可选 transcript；最终输出经过 assertMusic。

设计、协议与测试范围分别见 [architecture](docs/architecture.md)、
[protocol](docs/protocol.md)、[testing](docs/testing.md)。
