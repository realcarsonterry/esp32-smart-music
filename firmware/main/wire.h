#ifndef FIRMWARE_WIRE_H
#define FIRMWARE_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RECORD_RATE 16000u
#define RECORD_SAMPLES (4u * RECORD_RATE)
#define WAV_HEADER_BYTES 44u
#define RECORD_WAV_BYTES (WAV_HEADER_BYTES + RECORD_SAMPLES * 2u)
#define RESPONSE_LIMIT 128044u

typedef enum {
    IDLE, RECORDING, UPLOADING, WAITING_FOR_MUSIC,
    VALIDATING_MUSIC, PLAYING
} device_state_t;
bool device_transition_allowed(device_state_t from, device_state_t to);
bool backend_url_allowed(const char *url, bool allow_lan_http);
void record_wav_header(uint8_t header[WAV_HEADER_BYTES], uint32_t samples);
int16_t microphone_pcm16(uint32_t slot);
#endif
