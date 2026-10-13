#ifndef FIRMWARE_DEVICE_H
#define FIRMWARE_DEVICE_H
#include "esp_err.h"
#include "music_core.h"
#include "wire.h"

typedef struct {
    char ssid[33];
    char password[65];
    char token[257];
    char backend_url[257];
    bool allow_lan_http;
} device_config_t;
esp_err_t device_config_load(device_config_t *config);
esp_err_t device_wifi_init(const device_config_t *config);
esp_err_t device_wifi_wait(void);
esp_err_t device_time_sync(void);
/* Called once by the controller before starting audio_task; may allocate DMA. */
esp_err_t audio_init(void);
esp_err_t audio_record(int16_t *pcm);
esp_err_t audio_play(music_context *context, uint64_t *written_samples);

typedef void (*upload_done_fn)(void);
/* WAV and response may alias: response is read only after every WAV byte is sent. */
esp_err_t upload_recording(const device_config_t *config,
                           const uint8_t *wav, size_t wav_bytes,
                           char *response, size_t response_capacity,
                           size_t *response_bytes, upload_done_fn upload_done);
#endif
