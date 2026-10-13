# 阶段 7 参考硬件：全部为未验证建议

真实硬件状态 **BLOCKED**。没有连接开发板、录音、烧录或测量。本页接线、供电、
时钟和声音表现均未实测，不代表经过验证的原理图。

参考器件为 ESP32-S3-DevKitC-1、INMP441、MAX98357A 模块和扬声器。
两个独立 I2S：I2S0 只接麦克风，I2S1 只接功放；先录后播，不做全双工。
默认避开 GPIO 0、3、45、46、19、20；程序也拒绝这些引脚和引脚复用冲突。
具体开发板的 Flash/PSRAM 占用、排针和模块电压仍须按实际型号核对。

未验证接线建议（均可在菜单调整）：

- INMP441 VDD 接 **3.3V**，GND 接公共地；禁止将麦克风 VDD 接 5V。
- INMP441 SCK 接 GPIO5，WS 接 GPIO6，SD 接 GPIO7。
- INMP441 L/R 接地，默认读取左 slot；若接 3.3V，启用菜单的 right slot。
- 若裸器件有 CHIPEN，应按厂商手册处理；不要把模块的悬空脚当作已上拉。
- 按 INMP441 官方建议，电源就近放 0.1 µF 去耦，SD 可加约 100 kΩ 下拉；
  模块可能已装这些元件，先检查模块资料。
- MAX98357A BCLK 接 GPIO15，LRC/WS 接 GPIO16，DIN 接 GPIO17，GND 接公共地。
- MAX98357A 的 VIN、SD/EN、GAIN 和声道选择按实际模块手册处理；供电电流须足够。
  不将功放电源或扬声器输出接到 ESP GPIO。固件发送相同的左右声道。
- 扬声器接功放的 SPK+、SPK−；不要把差分扬声器输出任一端直接接地。
- 独立常开录音按钮接 GPIO4 与 GND，使用内部上拉；不使用板上的 BOOT 按钮。

INMP441 官方数据手册规定 24-bit MSB-first 二补码、每个 stereo frame 64 个 BCLK，
Philips I2S 数据相对 WS 延后一个时钟。因此固件设为 16 kHz、双 32-bit slot：
BCLK 预期 1.024 MHz，读取 slot bits 31:8 后再缩至 int16；没有把 32-bit 原始 DMA
数据强转成 int16。TX 预期 LRCLK 32 kHz、双 16-bit slot、BCLK 1.024 MHz。

待硬件具备后的验证事项（全部尚未执行）：

1. 先断电核对模块型号、供电与公共地，再测 3.3V、电流和引脚冲突。
2. 用逻辑分析仪验证两组 WS/BCLK、Philips 延迟、左右 slot 和有效位对齐。
3. 明确按键后验证录音恰好 64000 个单声道样本、WAV 128044 字节、极性和幅度；
   确认不存在全静音或削波。
4. 测试局域网 HTTP、HTTPS 可信与不可信证书、截断响应、非法事件和断网恢复。
5. 只有整包校验通过才出现 TX 播放；检查最坏 512 events、8 声部时的堆余量、
   栈水位、欠载、短写恢复、尾音和重复播放，确认没有 RX/TX 同时开启。

官方参考（API 与器件资料核对不等于接线实测）：

- [ESP-IDF 5.3.2 ESP32-S3 I2S](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/peripherals/i2s.html)
- [INMP441 数据手册，I²S DATA INTERFACE](https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf)
- [MAX98357A 厂商产品资料](https://www.analog.com/en/products/max98357a.html)
- [ESP-IDF 5.3.2 HTTP client](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/protocols/esp_http_client.html)
