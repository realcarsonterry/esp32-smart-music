# 阶段 3 音频引擎

状态：PASS。core 是唯一的纯 C11 合成实现，输出单声道 32000 Hz PCM16。

## C API 与内存

调用者分配 `music_context_bytes()` 字节，并满足
`music_context_alignment()` 对齐；`music_init` 校验、复制输入并重置播放。
`music_render_reset(context)` 从采样 0 重新播放同一份数据。
`music_render_next_pcm16(context, output, capacity)` 返回实际写出的采样数。
capacity 可以是 256，也可以是任意大小；0 不推进播放。
NULL context/output 返回 0；reset 的 NULL context 返回 INVALID_MUSIC_DATA。
输出长度是 bars 对应的采样数加 640，包含最后音符的完整释放。
到 EOF 后返回 0，不改写输出。

上下文包含八条独立的 512-float 延迟线、事件调度数据和
640-float 释放累加环。初始化、reset、触发和渲染均不分配堆内存。
测试中的 malloc 只用于调用者拥有的上下文和输出。

## 最终延迟与环路公式

起步公式首次通过音高测试，未作修正：

```text
frequency = 440 * pow(2, (midi - 69) / 12)
period = 32000 / frequency
delay = period - 0.5
N = floor(delay)
frac = delay - N
a = (1 - frac) / (1 + frac)
decay = pow(0.996, 1 / N)
```

触发时检查 `2 <= N <= 512`，预先计算系数。
每个采样先读当前位置的延迟线值 x，再依次经过平均滤波和一阶全通：

```text
average[n] = 0.5 * (x[n] + x[n-1])
allpass[n] = a * average[n] + average[n-1] - a * allpass[n-1]
delay_line[cursor] = decay * allpass[n]
cursor = (cursor + 1) % N
output = x[n] * (velocity / 127 * 0.12)
```

滤波历史初始为 0；平均滤波和全通均在反馈环内。
平均滤波贡献 0.5 采样相位延迟，全通补偿 frac。
每次写回乘 decay，约每圈衰减到 0.996；不按每采样直接乘 0.996。

## 随机数、调度与释放

reset 用输入 seed 初始化局部 xorshift32，按原始 events 数组顺序
为每个音抽取一次种子。触发后该音使用自己的 RNG 填充延迟线：
`(next() & 0xffff) / 32768.0 - 1`，范围为 [-1, 1)。
seed 0 初值沿用阶段 2 经用户确认的十进制 2164325893。
公开的 music_random_next 不影响音符 RNG 或 PCM。

事件按开始采样稳定排序，同一采样按原始数组顺序触发。
开始和结束位置均调用 SPEC 的 uint64 tick 换算公式。
同一采样先结束旧音，再触发新音，符合左闭右开的事件区间。

音符结束采样记为 E：E 到 E+639 的增益分别乘 `(640-k)/640`，
其中 k=0..639；从 E+640 起该音输出为 0。
结束时预计算这 640 个 KS 尾音，加入释放累加环，再释放声部槽。
因此八个新音与旧音释放重叠时也不截断尾音。
每个音的力度增益固定，不随活动音数变化。
所有声部与尾音求和后限幅到 ±0.98，再按现有 API 转换为 PCM16。
转换先处理非有限数为 0，再乘 32767、四舍五入并饱和。

## 实测验收

Windows NT 10.0.26200.0 X64 / PowerShell 7.6.6；
GCC 16.2.0（w64devkit 2.10.0）/ CMake 4.4.4 / Ninja；禁用 fast-math。
阶段 2 基线 13/13 通过，加入阶段 3 后 19/19 通过。
cmake 配置、构建与 ctest 的退出码均为 0，记录见 PROGRESS.md。

音高参数：bpm 60、duration_tick 960、velocity 100，取前 32000 采样，
丢弃前 1600；对 [1600,32000) 做 Goertzel，无额外加窗。
扫描目标频率 ±40 音分，步长 1，容差 ±20。
MIDI 52/57/64/69 的峰值分别为 164.813778456、220、
329.627556913、440 Hz，四项偏差均为 0 音分。

力度 40/100 的 RMS 为 184.654901/461.633989；
释放前 160/后 160 采样的 RMS 为 334.434196/45.978392。
八音峰值 16157，小于 PCM 限幅 32112；第九个重叠音在校验和初始化时拒绝。
整段/256/1 采样分块共 35549 采样逐样本相同。
seed 0、1、UINT32_MAX 重复渲染及播放中 reset 后的 PCM 均逐字节相同。
同一构建保证重复性，不承诺跨编译器的逐字节一致。
