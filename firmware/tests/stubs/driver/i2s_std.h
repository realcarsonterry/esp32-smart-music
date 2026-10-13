#ifndef TEST_I2S_H
#define TEST_I2S_H
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void *i2s_chan_handle_t;
typedef struct { int id, role, dma_desc_num, dma_frame_num; bool auto_clear_after_cb; } i2s_chan_config_t;
typedef struct { unsigned sample_rate_hz; } test_clock_t;
typedef struct { unsigned width, mode; } test_slot_t;
typedef struct { int mclk, bclk, ws, dout, din; } test_gpio_t;
typedef struct { test_clock_t clk_cfg; test_slot_t slot_cfg; test_gpio_t gpio_cfg; } i2s_std_config_t;
#define I2S_NUM_0 0
#define I2S_NUM_1 1
#define I2S_ROLE_MASTER 1
#define I2S_GPIO_UNUSED (-1)
#define I2S_DATA_BIT_WIDTH_32BIT 32
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2
#define I2S_CHANNEL_DEFAULT_CONFIG(n, r) {.id = (n), .role = (r)}
#define I2S_STD_CLK_DEFAULT_CONFIG(r) {.sample_rate_hz = (r)}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(w, m) {.width = (w), .mode = (m)}
esp_err_t i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *, i2s_chan_handle_t *);
esp_err_t i2s_del_channel(i2s_chan_handle_t);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t, const void *, size_t, size_t *);
esp_err_t i2s_channel_write(i2s_chan_handle_t, const void *, size_t, size_t *, uint32_t);
esp_err_t i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *, uint32_t);
#endif
