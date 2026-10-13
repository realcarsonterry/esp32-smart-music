# 阶段 7 固件

实现 SPEC.md 的 ESP32-S3 固件。已用本机安装的 ESP-IDF 5.3.5 在全新目录编译通过；未烧录或连接硬件。
当前构建、体积和限制见 `../docs/final-review-20261013.md`。下文描述源码设计，不等于硬件验收。

工程以 `EXTRA_COMPONENT_DIRS=../core` 注册 `core/CMakeLists.txt`，唯一引擎仍是
`core/music_core.c`。直接编译 `simulator/music_json.c`，使用 IDF 自带 `json`
组件的 cJSON，不复制引擎或 JSON 解码器，也不编译模拟器的 vendored cJSON 到设备。
所有组件禁用 fast-math。

在**已有且已导出环境**的 ESP-IDF 5.3.5 终端执行（下列为复现命令；本次真实命令和证据见最终审查）：

```text
idf.py -C firmware set-target esp32s3
idf.py -C firmware menuconfig
idf.py -C firmware build
```

`Stage 7 music device` 菜单设置 SSID、Wi-Fi 密码、可选 bearer token、后端基础 URL
和引脚。`sdkconfig.defaults` 的 SSID、密码、令牌和 URL 全为空。
菜单密码会进入本地 sdkconfig/编译产物，均不提交；源码没有凭据默认值。
也可以预先在 NVS namespace `music_device` 写入字符串键 `ssid`、`password`、
`token`、`backend_url`，同名 NVS 值覆盖菜单值；空值也覆盖，错误或超长值拒绝启动。
更改配置后重启。本阶段只读 NVS，不实现远程配网，不自动擦除 NVS。
Wi-Fi 驱动使用 RAM storage，不额外持久化一份密码。

URL 是不带路径、末尾斜线、查询参数和凭据的基础 origin。例：
`http://192.168.1.10:8787`。设备自动追加 `/api/v1/generate`。
设备的 localhost 不是电脑。HTTP 默认禁用；只在菜单显式启用 LAN development 后，
允许 10/8、172.16/12、192.168/16 的字面 IPv4 地址。HTTP 不接受域名或重定向。
电脑的 Worker 需监听局域网接口，并按本机防火墙策略开放所选端口；不由本固件修改
现有本地 Worker 脚本。公网使用 HTTPS：内置 CA bundle 校验服务器及主机名，
先通过菜单指定的 NTP 服务器校时，失败即停止本次请求。不支持绕过证书验证；
不信任内置 bundle 的私有 CA 会被拒绝。所有重定向均拒绝，不转发令牌。

上电保持 IDLE。独立常开按钮按下接地触发一次固定 4 秒录音。状态为：

```text
IDLE -> RECORDING -> UPLOADING -> WAITING_FOR_MUSIC
     -> VALIDATING_MUSIC -> PLAYING -> IDLE
```

任一录音、Wi-Fi、校时、HTTP、JSON、音乐或 I2S 错误回 IDLE，下一次按键可以重试。
初始化配置或资源失败时保持 IDLE，修正配置并重启。忙时按键丢弃，300 ms 去抖；
不在启动时自动录音。本次开发没有操作真实麦克风。

RX 使用 I2S0、16 kHz、Philips、双 32-bit slot，提取菜单选定的一个 slot。
INMP441 的 24-bit 二补码在 slot 高位，去掉 8 位 padding 再去掉 8 位精度，
显式符号扩展为 int16。固定 64000 采样、128000 字节 PCM 加 44 字节 RIFF/WAV。
设备只针对这种 MSB 对齐麦克风格式实现转换；换器件必须重新核对资料和测量。

控制任务在录音完成后封装 WAV，通过 multipart `audio` 上传。
上传和响应共用一个约 128 KB 的静态 arena，所有 WAV 字节发送完才复用为响应存储。
HTTP 只接受 200，限制响应为 128044 字节，要求完整的 Content-Length 或完整 chunked
响应，截断/过大/非 200 都不播放。ASR 适配已实现但默认本地配置无 AI binding，音频接口返回
503/ASR_UNAVAILABLE 时设备应回 IDLE；没有用假结果完成硬件闭环。

控制任务完整接收后调用已有严格 cJSON 解码器和 `music_validate`，校验 schema 字段、
重复字段、UTF-8、协议、BPM、tick、音高、唯一 id、最大重叠与和弦归属，再 `music_init`
复制到唯一 core 上下文。只有全部成功才给音频任务发送 PLAY 命令。chords 不再解释，
固件不计算文本哈希。

音频任务独立、固定栈，命令和结果队列也是静态分配。I2S/DMA 与 core 上下文在
创建音频任务前分配。录音/播放路径没有 HTTP、JSON、堆分配和日志。KS 每次渲染
最多 256 个 32 kHz 单声道采样，再复制到左右两个 16-bit slot。
短写/带进度超时保留当前块并继续写后缀，确认整块写完才渲染下一块；进度只累计
I2S 确认的完整 stereo frame。零进度或异常字节对齐立即停机返回错误。
结束后用 I2S 写入超过一圈 DMA 的静音排空尾音再关闭 TX，没有节拍延时。
RX/TX 不同时启用。使用的 I2S 头文件只有 `driver/i2s_std.h`。

宿主测试单独配置，不更改原有测试或根工程：

```text
cmake -S firmware/tests -B build/stage7-host
cmake --build build/stage7-host
ctest --test-dir build/stage7-host --output-on-failure -V
```

这些测试调用真实固件 audio/wire 源文件、真实 core 和解析器，用显式 fake I2S
验证 64000 采样上限、slot 转换、短读/短写、超时、双声道 PCM 逐样本一致、尾音排空
以及错误停机。另一个测试编译真实 transport 源文件，用 fake HTTP 验证完整 multipart
WAV、短写、共用 arena、已缓存/分块响应读取、截断/超限/非 200 拒绝及资源清理。
stubs 只在宿主测试 include 路径中，不能作为 IDF 编译证据。
真实内存余量、实时性、网络互操作、DMA 时序、GPIO 和音质仍需开发板验证。
