# SPEC 1.0

本文件是唯一契约。后续阶段如果与本文件冲突，停止并报告，禁止私下改数字、字段名和容差。

## 产品边界
设备录音并上传。后端把文字变成 Music JSON。ESP32-S3 只用 events 做 Karplus–Strong 合成后播放。chords 只是说明，合成器禁止再解释 chords。

没有硬件时，必须完成的闭环是：中文文本，本地 Worker 的 rules 模式，Music JSON，同一份 C 引擎，WAV，自动检查。

禁止下载或模仿具体版权歌曲，禁止 SoundFont、MP3、正弦波代替 KS，禁止 Python 或 TypeScript 再写一套 KS。用户点名某首商业歌曲时，仍只按情绪关键词生成原创伴奏。

不要求 GUI、全双工、账号系统和 6/8。v1.0 只生成 4/4。

## 诚实规则
状态只允许 PASS、PARTIAL、BLOCKED、FAIL。PASS 必须留下真实命令、退出码、环境和结果路径。禁止 git push。禁止部署，除非同时满足用户明确授权和 ALLOW_DEPLOY=1。禁止登录、绕过 2FA、发邮件、录音、打印密钥。不要把 Wi-Fi 密码写进源码或 sdkconfig.defaults。

当前环境不一定是 Windows。实际跑过的命令和“仅写了 ps1、待 Windows 验证”必须分开。

## 目录
core 是纯 C11，禁止包含 ESP-IDF、FreeRTOS、HTTP、JSON、Python、Node 头文件。固件用 EXTRA_COMPONENT_DIRS 链接 core，禁止复制第二份引擎到 firmware/components。simulator 只解析 JSON 并调用 core。backend 只生成 JSON，不合成音频。

## 协议
成功对象符合 shared/music.schema.json。失败对象符合 shared/error.schema.json。

成功和失败对象的 request_id 都必须是 ASCII 标识符，完整匹配 ^[A-Za-z0-9._:-]{1,64}$（1..64 字节）。禁止中文、空格、NUL 和换行。请求优先使用 JSON request_id，其次 X-Request-ID，否则使用 request。非法标识符返回 HTTP 400 / INVALID_REQUEST；错误响应不得回显非法值，使用已验证的请求头标识符或 request。请求头若非法则直接拒绝，使用 request。

有效单小节夹具：shared/fixtures/valid-one-bar.json。

时间单位只有 tick。PPQ 固定 480。4/4 每小节 1920 tick。总长度是 bars 乘 1920。事件必须满足 start_tick + duration_tick 不超过总长度。同一 tick 上重叠的事件不超过 8。accompaniment 的音级必须属于当时和弦的音级，允许换八度。melody 只检查音高范围，允许和弦外音。id 在一次响应内唯一。

错误码只用：INVALID_REQUEST、INVALID_TEXT、INVALID_AUDIO、INVALID_BPM、INVALID_MUSIC_DATA、UNSUPPORTED_PROTOCOL_VERSION、ASR_UNAVAILABLE、ASR_FAILED、EMPTY_TRANSCRIPTION、AI_UNAVAILABLE、AI_OUTPUT_INVALID、MODEL_ERROR、REQUEST_TOO_LARGE、UNAUTHORIZED、INTERNAL_ERROR。

HTTP 状态：400 表示请求或音乐非法；413 表示过大；401 表示生产模式缺令牌；503 表示 AI 或 ASR 未配置；502 表示模型失败或输出两次仍非法；500 表示内部错误。错误消息禁止包含令牌和堆栈。

## 时间与随机数
采样位置使用 uint64，且 bpm 已经校验非零：

sample = (tick * 32000 * 60 + (bpm * 480) / 2) / (bpm * 480)

向量：tick 0、bpm 96 得到 0。tick 480、bpm 60 得到 32000。tick 1920、bpm 96 得到 80000。tick 1、bpm 96 得到 42。反向只要求 tick 转 sample 再转回 tick 时恢复原 tick。

C 引擎的 xorshift32：state 为 uint32。seed 0 时内部初值用 2164325893，也就是十六进制 A5A5A5A5，JSON 里仍保存 0。步进是 x ^= x << 13；x ^= x >> 17；x ^= x << 5。禁止 rand() 和全局随机状态。

缺省 seed 只由后端计算。输入是去掉两端空格、TAB、CR、LF 后的 UTF-8 字节，不做 Unicode 规范化。FNV-1a 32 的 offset 是 2166136261，prime 是 16777619，按 uint32 回绕。向量：

- 生成一段温暖、轻快、适合夏日海边的木吉他伴奏 → 691577472
- 来一段悲伤、慢的木吉他 → 3350900807
- 强烈、摇滚的木吉他伴奏 → 1415375574
- 随便来一段伴奏 → 3245418374

固件和 C 核心禁止再算文本哈希。

## C API
头文件对外至少包含这些常量：采样率 32000，PPQ 480，每小节 1920，最大小节 16，最大事件 512，最大和弦 64，最大同时发声 8，块大小 256，MIDI 40 到 84，BPM 40 到 220，每条延迟线容量 512 float，释放 640 个采样。

music_data_t 保存 bpm、bars、seed、root、mode、chords 和 events，使用固定数组，不用链表。music_context 不透明，由调用者提供 music_context_bytes() 大小的内存。初始化、校验、tick 换算、写 WAV 在阶段 2 完成。阶段 3 才能加入 music_render_reset 和 music_render_next_pcm16。渲染路径禁止 malloc。整段渲染就是连续取块后再拼接，必须与一次次取 256 个采样的 int16 序列逐样本相同。

WAV 是单声道、32000 Hz、16-bit little-endian PCM。float 到 int16 先把非有限数变成 0，再乘 32767，四舍五入，饱和到 int16 范围。

## KS
频率是 440 乘以 2 的 (midi-69)/12 次方。每个音独立延迟线。触发时用该音自己的 RNG 状态填入 [-1,1) 噪声，RNG 映射为 (next() 的低 16 位 / 32768.0) - 1。

设 period = 32000 / frequency。delay = period - 0.5。N 为 delay 的整数部分，frac 为小数部分。N 小于 2 时拒绝。一阶全通系数起点是 (1-frac)/(1+frac)。平均滤波器与全通都在反馈环里。写回系数为 pow(0.996, 1/N)，在触发时算好，每个采样乘一次。这相当于大约每一圈延迟线乘 0.996。禁止每个采样直接乘 0.996，否则 1 秒后是静音。

力度增益是 velocity/127 乘 0.12，整首固定，禁止按当前活动音数改变增益。输出求和后硬限制到正负 0.98，再转 int16。音符结束后用 640 个采样把输出增益线性降到 0，然后释放声部。

音高测试只测单音 MIDI 52、57、64、69。bpm 60，duration_tick 960，velocity 100，渲染至少 32000 个采样。丢掉前 1600 个采样，对剩余到第 32000 个采样做目标频率附近的 Goertzel，从低 40 音分扫到高 40 音分，步长 1 音分。峰值必须在正负 20 音分以内。容差禁止放宽。起步公式不准时只允许调整分数延迟，不允许改测试。

同一构建内，相同输入和 seed 的 PCM 必须逐字节相同。不承诺换编译器后仍逐字节相同。编译禁止 fast-math。

## 规则生成器
默认 mode 是 rules。文本最长 500。空文本返回 INVALID_TEXT。显式 BPM 用十进制数字后接 BPM、bpm 或拍；越界返回 INVALID_BPM，合法则只覆盖速度，不改和弦。

关键词优先级：包含摇滚或强烈就是摇滚；否则包含悲伤、忧伤或慢就是悲伤；否则包含温暖、轻快或夏日就是轻快；否则默认。生成结果固定 4 小节。

轻快：C 大调，96，和弦 C、G、Am、F。MIDI 依次是 48 52 55 60、43 47 50 55、45 48 52 57、41 45 48 53。力度 80。每拍 480 tick 拨一次，四个音依次错开 10 tick，时值 400。

悲伤：A 自然小调，72，Am、G、F、Em。MIDI 依次是 45 48 52 57、43 47 50 55、41 45 48 53、40 47 52 55。力度 64。节奏同轻快。

摇滚：E 小调，128，Em、D、C、D。MIDI 依次是 40 47 52 59、42 45 50 54、48 52 55 60、42 45 50 54。力度 112。每 240 tick 拨一次，时值 200，错开仍是 10 tick。

默认：G 大调，100，G、Em、C、D。MIDI 依次是 43 47 50 55、40 47 52 55、48 52 55 60、50 54 57 62。力度 84。节奏同轻快。

旋律每拍一个音，时值 360，力度 88，音高在 60 到 76。强拍，也就是 start_tick 能被 480 整除时，必须是当前和弦音级。后端使用同一个 xorshift32 选择和弦音，调用顺序固定并写进代码注释。测试锁定调式、BPM、和弦 symbol 顺序、事件合法和两次调用完全相同。不要把整份事件列表手写死成唯一快照，除非 seed 也固定并且生成器没有未记录的随机调用。

## HTTP
本地地址 http://127.0.0.1 ，默认端口 8787。端口忙时换 8877，禁止杀掉别人的进程。

GET /health 返回 ok 和协议版本，禁止调用模型。
GET /api/v1/info 返回协议、规则可用、是否配置了 AI 绑定、是否要求认证、录音格式和合成格式。配置了绑定也不算模型调用成功。
POST /api/v1/generate-from-text 的 JSON 是 text、可选 seed、可选 request_id、可选 mode。
POST /api/v1/generate 使用 multipart，文件字段名 audio，可选 hint、mode（rules 或 ai）。request_id 使用 X-Request-ID。hint 不作为转写文本。

没有 mode 就是 rules。文本 rules 禁止任何网络模型调用。音频在 WAV 校验后调用一次 ASR：无绑定返回 HTTP 503 / ASR_UNAVAILABLE，失败或非法输出返回 HTTP 502 / ASR_FAILED，空转写返回 HTTP 502 / EMPTY_TRANSCRIPTION。音频 rules 识别后按规则生成；音频 ai 识别后只做一次 LLM 意图提取，再按规则生成。mock 仅用于文本，只有环境变量 ALLOW_MOCK=1 且请求写明 mock 才可用。ai 失败时禁止改标成 rules。

成功对象可含 transcript：非空 UTF-8 转写文本，最多 500 个 Unicode 字符，禁止 NUL 和孤立代理项。schema 和 C 解析器接受该可选字段，模拟器验证后忽略，不参与合成；所有成功输出仍经过 assertMusic。真实语音调用未验证时写 BLOCKED。

录音只接受 WAV、单声道、16-bit PCM、16000 Hz、最长 4 秒、请求体最大 1000000 字节。必须检查内容而不是扩展名。空文件或全静音返回 INVALID_AUDIO。本地 AUTH_REQUIRED 默认不开启。只有 AUTH_REQUIRED=1 时才要求 Authorization: Bearer，令牌来自环境变量 DEVICE_TOKEN。

AI 适配层在阶段 8 才实现。在那之前，请求 mode=ai 直接返回 AI_UNAVAILABLE。真实模型调用最多 ASR 一次、意图解析两次。意图 JSON 只有 mood、bpm、root、mode、bars、style。最终音符仍走本文件的规则生成器。实现时核对 Cloudflare 官方文档，禁止使用只支持英文的 whisper-tiny-en。

## 固件
目标是 ESP32-S3，ESP-IDF 5.3 或同代 5.x。I2S 只包括 driver/i2s_std.h，禁止包括 driver/i2s.h。

状态顺序：IDLE、RECORDING、UPLOADING、WAITING_FOR_MUSIC、VALIDATING_MUSIC、PLAYING，然后回 IDLE。错误必须能回 IDLE。播放前整包校验。录音最长 4 秒，16 kHz 单声道 int16 WAV。若麦克风是 32-bit slot，先按官方资料移位，禁止假定它直接给出 int16。

播放内部 32000 Hz 单声道，功放需要立体声时复制左右声道。音频任务只按实际写出的采样数推进，禁止用 vTaskDelay 当节拍器。音频路径禁止 HTTP、JSON、malloc 和长时间日志。先录音再播放，不做全双工。

设备上的 localhost 是设备自己。后端地址必须可配置成电脑的局域网地址。http 只允许用于局域网开发。公网必须 HTTPS 且校验证书，禁止用关闭证书校验来通过编译。

没有 idf.py 时，固件构建写 BLOCKED，禁止伪造 bin，禁止安装 ESP-IDF。没有开发板时，真实硬件永远是 BLOCKED。

参考硬件只作为未验证建议：ESP32-S3-DevKitC-1、INMP441 使用 3.3V、MAX98357、两个独立 I2S。避开 GPIO 0、3、45、46、19、20。

## 脚本退出码
0 成功。2 JSON 非法。3 渲染失败。4 HTTP 失败。5 环境或端口失败。run-simulator.ps1 只能停止自己启动的 Worker，先等 /health，禁止固定 sleep 后盲目请求。默认输出 artifacts/generated_music.json 和 artifacts/generated_music.wav。重复运行可以覆盖这两个文件，禁止删除其他文件。

## 阶段
2：校验、时间、RNG、WAV。3：KS 和音高。4：模拟器渲染夹具。5：后端 rules 和单测。6：真实本地 HTTP 闭环。7：固件源码。8：AI 适配代码、文档和总回归。每个阶段禁止提前做后面的阶段。
