# 协议 1.0（阶段 8）

以 SPEC.md、shared/music.schema.json、shared/error.schema.json 为准。
本地地址 127.0.0.1:8787；忙则 8877，不停止其他进程。

## HTTP

- GET /health：ok=true、protocol_version=1.0；不访问模型。
- GET /api/v1/info：协议、rules_available、ai_configured、auth_required、录音/合成格式。
- POST /api/v1/generate-from-text：application/json，text、可选 seed/request_id/mode。
- POST /api/v1/generate：multipart，唯一 audio 文件、可选字符串 hint、mode（rules/ai）；request_id 来自 X-Request-ID。

text 为 1..500 字符，去首尾 SPACE/TAB/CR/LF；空文本 INVALID_TEXT。
seed 为 uint32；缺省对原文按 UTF-8/FNV-1a 计算，不做 Unicode 规范化。
显式 BPM 支持数字后接 BPM、bpm、拍，整数 40..220，否则 INVALID_BPM。
成功和失败响应的 request_id 都必须完整匹配 ^[A-Za-z0-9._:-]{1,64}$，仅允许这 1..64 个 ASCII 字节。
中文、空格、NUL、换行和长度越界均返回 HTTP 400 / INVALID_REQUEST，不生成成功音乐响应。
优先 JSON，其次 X-Request-ID，否则使用确定值 request；请求头若非法则直接拒绝。
成功和错误仅回显合法标识符；JSON 标识符非法时，错误使用已验证的请求头或 request；请求头非法时使用 request。
schema 额外排除所有非许可字符，避免正则 $ 在末尾换行前匹配的语义差异。
未知字段、模式和非法 JSON 返回 INVALID_REQUEST。

mode 缺省 rules。文本 rules 永不访问模型；文本 mock 只有 mode=mock 且 ALLOW_MOCK=1 可用。
ai 无可用绑定返回 AI_UNAVAILABLE；成功标记 mode=ai，失败不会改标为 rules。
音频先检查 RIFF/chunk 边界及 PCM：mono、16000 Hz、16-bit、最长 4 秒。
空文件、全静音或格式非法返回 INVALID_AUDIO；不按文件扩展名判断。
校验通过后经 env.AI 调用一次多语言 ASR，无绑定为 503/ASR_UNAVAILABLE。
识别失败或输出类型/长度非法为 502/ASR_FAILED；空转写为 502/EMPTY_TRANSCRIPTION。
hint 接受但忽略，不充当识别文本。识别出的文本进入现有生成器；
音频 rules 不调用 LLM，音频 ai 只做一次 LLM 意图提取，失败不重试、不改 mode。
请求体按实际流字节限制为 1000000；Content-Length 不能绕过限制。

## 模型意图（内部，非 Music JSON）

意图对象必须恰好有以下六字段，不允许 events、chords 或其他字段：

- mood：bright、sad、rock、default。
- bpm：整数 40..220。
- root：bright=C、sad=A、rock=E、default=G。
- mode：bright/default=major；sad/rock=minor。
- bars：固定整数 4。
- style：rock=rock；其余为 acoustic。

预设组合不一致也算非法，不做隐式转换或静默丢弃字段。
从模型结果的 response 读取字符串 JSON 或对象；最多两次意图解析调用。
其中音频请求最多一次，文本请求保留最多两次。
首次非法重试一次；第二次仍非法返回 AI_OUTPUT_INVALID；模型异常返回 MODEL_ERROR。
有效意图通过原规则生成器形成 chords/events；原文哈希和显式 BPM 优先级保留。

## Music JSON

ok、protocol_version、request_id、mode、seed、bpm、time_signature、ppq、bars、key、chords、events。
可选 transcript 为 1..500 个 Unicode 字符，不含 NUL 或孤立代理项。
C 解析器验证并忽略它；同一音乐去掉 transcript 后 PCM 完全相同。
固定 4/4，PPQ=480，每小节 1920 tick；规则与 AI 固定 4 小节。
事件 id 唯一，MIDI 40..84、力度 1..127，start_tick+duration_tick 不超出 bars*1920。
同 tick 最多 8 音；伴奏持续区间音级属于当前和弦，允许八度变化。
旋律允许和弦外音，但规则生成器仍按 SPEC 生成强拍和弦音。
接收端只用 events 合成，不重新解释 chords。
合成输出 mono、32000 Hz、PCM16 little-endian；录音格式与合成格式不同。

## 错误与认证

错误对象为 ok=false、protocol_version、request_id、error={code,message}。
状态码：400 请求/音乐非法，413 过大，401 缺生产令牌，503 AI/ASR 未配置，
502 模型失败或两次意图非法，500 内部错误。响应不含令牌、原始模型文本或堆栈。
错误码只使用 SPEC 允许的集合，详见 shared/error.schema.json。
仅 AUTH_REQUIRED=1 时两个 POST 检查 Authorization: Bearer，令牌来自 DEVICE_TOKEN。
health/info 为公开诊断接口；本地认证缺省关闭。公网必须 HTTPS 并校验证书。
