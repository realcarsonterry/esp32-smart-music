#include "device.h"
#include "sdkconfig.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include <string.h>

#define DMA_DESCRIPTORS 4
static i2s_chan_handle_t rx, tx;
static uint32_t slots[MUSIC_BLOCK_SIZE * 2];
static int16_t mono[MUSIC_BLOCK_SIZE];
static int16_t stereo[MUSIC_BLOCK_SIZE * 2];
static const int16_t silence[MUSIC_BLOCK_SIZE * 2];

esp_err_t audio_init(void)
{
    const int pins[] = {CONFIG_FW_MIC_BCLK, CONFIG_FW_MIC_WS, CONFIG_FW_MIC_DIN,
        CONFIG_FW_AMP_BCLK, CONFIG_FW_AMP_WS, CONFIG_FW_AMP_DOUT, CONFIG_FW_TRIGGER_GPIO};
    for (size_t i = 0; i < sizeof pins / sizeof *pins; ++i) {
        if (!GPIO_IS_VALID_GPIO(pins[i]) || pins[i] == 0 || pins[i] == 3 ||
            pins[i] == 45 || pins[i] == 46 || pins[i] == 19 || pins[i] == 20)
            return ESP_ERR_INVALID_ARG;
        if (i != 2 && i != 6 && !GPIO_IS_VALID_OUTPUT_GPIO(pins[i]))
            return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j)
            if (pins[j] == pins[i]) return ESP_ERR_INVALID_ARG;
    }
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = DMA_DESCRIPTORS;
    channel.dma_frame_num = MUSIC_BLOCK_SIZE;
    channel.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&channel, NULL, &rx);
    if (err != ESP_OK) return err;
    channel.id = I2S_NUM_1;
    err = i2s_new_channel(&channel, &tx, NULL);
    if (err != ESP_OK) goto fail;
    i2s_std_config_t mic = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RECORD_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = CONFIG_FW_MIC_BCLK,
            .ws = CONFIG_FW_MIC_WS, .dout = I2S_GPIO_UNUSED, .din = CONFIG_FW_MIC_DIN},
    };
    err = i2s_channel_init_std_mode(rx, &mic);
    if (err != ESP_OK) goto fail;
    i2s_std_config_t amp = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = CONFIG_FW_AMP_BCLK,
            .ws = CONFIG_FW_AMP_WS, .dout = CONFIG_FW_AMP_DOUT, .din = I2S_GPIO_UNUSED},
    };
    err = i2s_channel_init_std_mode(tx, &amp);
    if (err == ESP_OK) return ESP_OK;
fail:
    if (tx) { i2s_del_channel(tx); tx = NULL; }
    if (rx) { i2s_del_channel(rx); rx = NULL; }
    return err;
}

esp_err_t audio_record(int16_t *pcm)
{
    esp_err_t err = i2s_channel_enable(rx);
    if (err != ESP_OK) return err;
    size_t captured = 0;
#ifdef CONFIG_FW_MIC_RIGHT
    const unsigned slot = 1;
#else
    const unsigned slot = 0;
#endif
    while (captured < RECORD_SAMPLES) {
        size_t frames = RECORD_SAMPLES - captured;
        if (frames > MUSIC_BLOCK_SIZE) frames = MUSIC_BLOCK_SIZE;
        size_t bytes = 0;
        err = i2s_channel_read(rx, slots, frames * 2 * sizeof *slots, &bytes, 1000);
        if (bytes > frames * 2 * sizeof *slots || bytes % (2 * sizeof *slots) != 0) {
            err = ESP_ERR_INVALID_SIZE; break;
        }
        const size_t received = bytes / (2 * sizeof *slots);
        for (size_t i = 0; i < received; ++i)
            pcm[captured + i] = microphone_pcm16(slots[2 * i + slot]);
        captured += received;
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) break;
        if (received == 0) { err = ESP_ERR_TIMEOUT; break; }
        err = ESP_OK;
    }
    esp_err_t stopped = i2s_channel_disable(rx);
    return err == ESP_OK ? stopped : err;
}

/* Retain the current rendered block until all of it is acknowledged by I2S.
 * No next render, dropped suffix, wall-clock tempo, allocation or networking. */
static esp_err_t write_frames(const int16_t *pcm, size_t frames, uint64_t *written)
{
    size_t offset = 0, total = frames * 2 * sizeof *pcm;
    while (offset < total) {
        size_t bytes = 0;
        esp_err_t err = i2s_channel_write(tx, (const uint8_t *)pcm + offset,
                                         total - offset, &bytes, 1000);
        if (bytes > total - offset || bytes % (2 * sizeof *pcm) != 0)
            return ESP_ERR_INVALID_SIZE;
        offset += bytes;
        *written += bytes / (2 * sizeof *pcm);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return err;
        if (!bytes) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t audio_play(music_context *context, uint64_t *written_samples)
{
    *written_samples = 0;
    if (music_render_reset(context) != MUSIC_OK) return ESP_ERR_INVALID_ARG;
    /* Clear all DMA buffers while stopped, including stale data after an error. */
    for (unsigned i = 0; i < DMA_DESCRIPTORS; ++i) {
        size_t loaded = 0;
        esp_err_t err = i2s_channel_preload_data(tx, silence, sizeof silence, &loaded);
        if (err != ESP_OK) return err;
    }
    esp_err_t err = i2s_channel_enable(tx);
    if (err != ESP_OK) return err;
    size_t count;
    while ((count = music_render_next_pcm16(context, mono, MUSIC_BLOCK_SIZE)) != 0) {
        for (size_t i = 0; i < count; ++i)
            stereo[2 * i] = stereo[2 * i + 1] = mono[i];
        err = write_frames(stereo, count, written_samples);
        if (err != ESP_OK) break;
    }
    if (err == ESP_OK) {
        /* Block on > one complete DMA ring of silence. This drains the last
         * musical sample before disabling TX, without a timed task delay. */
        uint64_t padding = 0;
        for (unsigned i = 0; i <= DMA_DESCRIPTORS && err == ESP_OK; ++i)
            err = write_frames(silence, MUSIC_BLOCK_SIZE, &padding);
    }
    esp_err_t stopped = i2s_channel_disable(tx);
    return err == ESP_OK ? stopped : err;
}
