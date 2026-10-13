# 验收报告（2026-10-13，Asia/Singapore）

> 历史阶段报告。当前状态以 [最终软件审查](final-review-20261013.md) 为准：ASR 已实现并离线测试，
> ESP-IDF 5.3.5 已在干净目录完成 ESP32-S3 构建、size、size-components。下文的“ASR 未实现”、
> “IDF 未安装/构建”等属于较早阶段记录，不代表当前状态。发送与演示请读 [START_HERE](../START_HERE.md)。

本文保留 request_id/可移植性修订时的历史验收。后续 ASR 已实现并离线验证，最新状态见 [ASR 验收](asr-offline-verification.md)。

## ESP32-S3 固件构建检查（2026-10-13，Asia/Singapore）

项目根目录：`C:\Users\Terry\Desktop\context`（含 `SPEC.md`）。当前 PowerShell 环境执行 `Get-Command idf.py -ErrorAction SilentlyContinue | Select-Object Name,Source,CommandType` 无输出，未找到可用的 `idf.py`；该检查所在命令块退出码为 1，此值不是 ESP-IDF 命令退出码。按要求立即停止，未尝试安装或寻找其他安装环境。

## ESP32-S3 固件构建检查（2026-10-13，Asia/Singapore）

在 ESP-IDF PowerShell 中执行 `idf.py -C firmware build`，输出包含 `Project build complete`，随后 `$LASTEXITCODE` 为 0。产物为 `firmware\build\stage_7_firmware.bin`，文件大小 957040 字节（0xe9a70）。应用分区 0x100000 字节，剩余 0x16590 字节（9%）。烧录提示中的目标芯片为 esp32s3。未执行 flash，未连接开发板。本次日志没有 `idf.py --version` 的输出，因此不记录具体 IDF 版本号。

| 验收项 / 命令 | 状态 | 退出码 | IDF 版本 / bin 路径 / 说明 |
| --- | --- | --- | --- |
| 固件构建 | PASS | 0 | firmware\build\stage_7_firmware.bin，957040 字节；Project build complete；尚未烧录 |
| `idf.py --version` | BLOCKED | 未执行 | 本次日志没有版本输出，不猜测版本号 |
| `idf.py -C firmware set-target esp32s3` | PASS | 未单独记录 | 本次 build 的烧录命令已写明 --chip esp32s3，目标此前已设置 |
| `idf.py -C firmware build` | PASS | 0 | Project build complete；firmware\build\stage_7_firmware.bin |
| `idf.py -C firmware size` | BLOCKED | 未执行 | 本次未运行，不影响 build 结果 |
| `idf.py -C firmware size-components` | BLOCKED | 未执行 | 本次未运行，不影响 build 结果 |
| 真实硬件 | BLOCKED | 未执行 | 未连接开发板，未执行 flash 或播放 |

本次仅补充验收记录，未修改固件、KS 或测试，未下载或安装包、部署、提交 sdkconfig 或执行 git push。

本次范围仅为 request_id 契约、可移植脚本与提交文档。已验证文本规则、KS 和本地 HTTP 到 WAV；ASR 尚未实现，云端、真实模型、ESP-IDF 平台构建与硬件尚未验证。

项目根目录为含 SPEC.md 的目录。本次环境：Windows NT 10.0.26200.0 X64，PowerShell 7.6.6（验收及子脚本），Node 24.19.0，npm 11.17.0，CMake/CTest 4.4.4，GCC 16.2.0，Ninja 1.13.2，Wrangler 4.149.0。每个工具单独运行版本命令并检查退出码，见 artifacts/stage-8-tools-*.txt。

## 指定命令与退出码

以下两条命令按此顺序实际执行，最终均为 PASS：

1. `node --import tsx scripts/review-probes.mjs` — **退出码 0**。日志：[request-id-probes-20261013.txt](../artifacts/request-id-probes-20261013.txt)。
2. `pwsh -NoProfile -File scripts/verify-stage8.ps1` — **退出码 0**。日志：[request-id-stage8-20261013.txt](../artifacts/request-id-stage8-20261013.txt)。各子命令、环境、退出码和结果路径见 [stage-8-verification.txt](../artifacts/stage-8-verification.txt)。

第一次沙盒内运行：探针退出 0，总验收退出 1（smoke 子脚本退出 5）。workerd 的 GetFinalPathNameByHandleW 拒绝访问导致本地服务无法启动。经执行权限审核，在沙盒外依次重跑同样两条命令后均退出 0。失败记录保留于 artifacts/request-id-stage8-sandbox-attempt-20261013.txt、request-id-worker-sandbox-attempt-20261013.txt 和 request-id-verification-sandbox-attempt-20261013.txt；没有跳过测试或放宽断言。

## 验证结果

- request_id 契约 — PASS。成功/失败 schema、后端、SPEC、协议说明和 C JSON 接收端统一为 `^[A-Za-z0-9._:-]{1,64}$`，只允许完整的 1..64 字节 ASCII 标识符。后端拒绝中文、空格、NUL 等，HTTP 400 / INVALID_REQUEST；错误响应使用合法的已验证标识符或 request。
- 审查样例回归 — PASS。`'任'.repeat(22)` 和 `'review\0id'` 已收进后端测试及跨语言探针，必须由后端返回 400。另覆盖空格、换行、空串、65 字节、合法标点及 64 字节边界；错误 schema 也检查非法值不可回显。schema 额外排除非许可字符，避免 `$` 在末尾换行前匹配。
- 后端 — PASS。TypeScript 类型检查退出 0，后端 32/32 项通过，含原 31 项和新增契约回归；见 artifacts/stage-8-tests.txt。AI 适配仍只使用假客户端。
- C/KS/模拟器 — PASS。CTest 20/20；KS MIDI 52/57/64/69 均 +0 音分，whole/256/1 的 35549 样本一致。C JSON 接收端新增非法 request_id 覆盖；见 artifacts/stage-8-ctest.txt。KS 引擎、音分容差、和弦规则没有改变。
- 固件宿主测试 — PASS。2/2 通过；见 artifacts/stage-8-host-tests.txt。fake I2S/HTTP 仅证明宿主边界，固件源码整体仍为 PARTIAL，不能当作 ESP-IDF 或硬件验收。
- 本地 HTTP 到 WAV — PASS。HTTP 200，rules、96 BPM、C 大调、C/G/Am/F、4 小节；mono/32000 Hz/PCM16 LE，320000 采样、640044 字节、非零 296729、峰值 6503、RMS 1080.374242。见 artifacts/stage-8-smoke-final.txt。
- 端口回退、双端口占用、错误响应和进程清理 — PASS。见 artifacts/stage-8-integration.txt。预期失败场景内部退出 5/4，外层集成入口退出 0。最终 smoke 恢复了配对成功的 JSON/WAV。
- 可移植性 — PASS（本机验证）。脚本优先使用 PATH；有效旧缓存仅作兼容回退，没有写死用户的 CMake/GCC/Temp 路径；根工程和宿主测试显式使用同一编译器、Ninja 生成器。无缓存的环境检查、全新构建目录配置/构建及 CTest 20/20、宿主 2/2 均退出 0；逐条命令见 artifacts/request-id-portability-20261013.txt。首次使用说明要求 npm ci，不携带 node_modules/build 缓存。

最终 WAV：artifacts/generated_music.wav；SHA256：
`D3358F8CB9DBA6F5A25ABA1D0030D8578F4D4B9C2ED1C8CD326736F245A94BBA`。

## 未完成与边界

ASR 未实现；音频路由仍校验 WAV 后返回 503 / ASR_UNAVAILABLE。真实模型与云端未验证，ESP-IDF 未安装/构建，硬件未连接/录音/烧录。没有部署，没有 git push。PROGRESS 顶部已与阶段 7/8 的实际源码和本地验证进度一致；后续阶段日志保留为历史记录，不替代本次验收。
