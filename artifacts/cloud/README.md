# 真实云端验收证据

验收时间：2026-10-13 20:01:25–20:01:57（Asia/Singapore）。
verification.json 保留原始 UTC 时间 12:01:25.406Z–12:01:57.270Z。
正式 API：https://esp32-smart-music-api.esp32-smart-music-backend.workers.dev

## 验收内容

- 6 项接口/认证检查：health、info、无令牌、错误令牌、非法 WAV、超大请求。
- 6 个文本音乐请求：温暖轻快、悲伤慢、摇滚 120 BPM，各走 rules 和 ai。
- 6 个音频音乐请求：同三类中文 TTS WAV，各走 ASR+rules 和 ASR+ai。
- 每个成功 JSON 通过 schema/跨字段检查与真实 C 解析器，再调用共享 KS 引擎渲染 WAV。
- 检查 mono/32000 Hz/PCM16、预计采样数、非静音，并保存每个 WAV 的 SHA256。

音频 ASR 调用真实 Cloudflare whisper-large-v3-turbo；ai 意图调用真实 llama-3.3-70b-instruct-fp8-fast。
文本 rules 不调用 LLM；音频 rules 仍需一次真实 ASR。
本次 12 个音乐请求和 6 项接口检查全部通过，脚本退出码 0。

## 文件对应关系

- [verification.json](verification.json)：全部请求的时间、状态、转写、音乐参数、输出路径、PCM 指标和哈希。
- [输入来源](inputs/provenance.json)：Windows System.Speech、Microsoft Huihui Desktop、mono/16kHz/PCM16。
- inputs/tts-warm.wav、tts-sad.wav、tts-rock.wav：实际上传的语音样本。
- results/text-{rules,ai}-{warm,sad,rock}.{json,wav}：文本请求返回数据及 C KS 合成音频。
- results/audio-{rules,ai}-{warm,sad,rock}.{json,wav}：音频请求返回数据及 C KS 合成音频。
- [控制台日志](../cloud-verification-20261013.txt)：实际验收执行输出。
- [正式部署日志](../cloud-deploy-20261013.txt)：Wrangler 部署成功、URL、版本 ID、退出码。

这些 WAV 是语音合成器生成的可听中文语音输入，ASR 返回值来自真实服务。
它们不是真人录音，也不能证明 ESP32 麦克风、I2S 或实机实时性已验证。
私人录音、设备令牌、Cloudflare OAuth 凭据和内部调试日志不包含在本目录。

## 复现

先按项目根 START_HERE.md 配置依赖并构建 C 模拟器，再由项目拥有者提供 DEVICE_TOKEN。

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

脚本从仓库根目录执行，调用真实模型并使用账号配额；无变更时无需反复重跑。
本机 TLS 1.2 选项保留证书验证。复现会覆盖该目录中同名结果；已提交版本可用于对照。
