#include "device.h"
#include "music_json.h"
#include "driver/i2s_std.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int rx_tag, tx_tag;
static bool rx_enabled, tx_enabled, stall_write, stall_read;
static size_t captured, sent_bytes, write_calls;
static unsigned char output[400000];

esp_err_t i2s_new_channel(const i2s_chan_config_t *c, i2s_chan_handle_t *tx,
                          i2s_chan_handle_t *rx)
{
    CHECK(c->dma_frame_num == 256 && c->dma_desc_num == 4);
    if (rx) { CHECK(c->id == 0); *rx = &rx_tag; }
    if (tx) { CHECK(c->id == 1); *tx = &tx_tag; }
    return ESP_OK;
}
esp_err_t i2s_del_channel(i2s_chan_handle_t h) { (void)h; return ESP_OK; }
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h, const i2s_std_config_t *c)
{
    CHECK(c->slot_cfg.mode == 2);
    CHECK(c->clk_cfg.sample_rate_hz == (h == &rx_tag ? 16000u : 32000u));
    CHECK(c->slot_cfg.width == (h == &rx_tag ? 32u : 16u));
    return ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t h)
{
    CHECK(!rx_enabled && !tx_enabled);
    if (h == &rx_tag) rx_enabled = true; else tx_enabled = true;
    return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t h)
{
    if (h == &rx_tag) rx_enabled = false; else tx_enabled = false;
    return ESP_OK;
}
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t h, const void *p, size_t n, size_t *loaded)
{
    CHECK(h == &tx_tag && !tx_enabled);
    for (size_t i = 0; i < n; ++i) CHECK(((const unsigned char *)p)[i] == 0);
    *loaded = n; return ESP_OK;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t h, const void *p, size_t n,
                            size_t *written, uint32_t timeout)
{
    CHECK(h == &tx_tag && tx_enabled && !rx_enabled && timeout == 1000);
    ++write_calls;
    *written = stall_write ? 0 : (n > 28 ? 28 : n); /* Seven stereo frames. */
    CHECK(sent_bytes + *written <= sizeof output);
    memcpy(output + sent_bytes, p, *written); sent_bytes += *written;
    return (write_calls % 2 || stall_write) ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t i2s_channel_read(i2s_chan_handle_t h, void *p, size_t n,
                           size_t *received, uint32_t timeout)
{
    CHECK(h == &rx_tag && rx_enabled && !tx_enabled && timeout == 1000);
    const size_t frames = stall_read ? 0 : (n / 8 > 13 ? 13 : n / 8);
    uint32_t *slots = p;
    for (size_t i = 0; i < frames; ++i) {
        slots[2 * i] = ((captured + i) % 2) ? 0x80000000u : 0x7fffff00u;
        slots[2 * i + 1] = 0x12340000u; /* Other slot must be ignored. */
    }
    captured += frames; *received = frames * 8;
    return ESP_ERR_TIMEOUT; /* Progress on timeout still counts. */
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    for (int from = IDLE; from <= PLAYING; ++from)
        for (int to = IDLE; to <= PLAYING; ++to)
            CHECK(device_transition_allowed(from, to) == (to == IDLE || to == from + 1));
    CHECK(!device_transition_allowed(99, IDLE));
    const char *allowed[] = {"https://music.example.com", "https://music.example.com:443",
        "http://192.168.1.10:8787", "http://10.1.2.3", "http://172.16.0.1:8877", "http://172.31.1.1"};
    for (size_t i = 0; i < sizeof allowed / sizeof *allowed; ++i) CHECK(backend_url_allowed(allowed[i], true));
    const char *denied[] = {"", "http://localhost:8787", "http://127.0.0.1", "http://8.8.8.8",
        "http://172.15.0.1", "http://172.32.0.1", "http://192.168.1.10.evil.com",
        "http://192.168.01.10", "http://192.168.1.10:65536", "http://192.168.1.10:0",
        "http://192.168.1.10/path", "https://a@b", "https://host?token=x", "https://host/#x",
        "https://host\r\nInjected:yes", "ftp://host", "http://2130706433"};
    for (size_t i = 0; i < sizeof denied / sizeof *denied; ++i) CHECK(!backend_url_allowed(denied[i], true));
    CHECK(!backend_url_allowed("http://192.168.1.10", false));
    unsigned char header[44]; record_wav_header(header, RECORD_SAMPLES);
    CHECK(memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVEfmt ", 8) == 0);
    CHECK(le32(header + 4) == 128036 && le32(header + 24) == 16000);
    CHECK(le32(header + 28) == 32000 && le32(header + 40) == 128000);
    CHECK(header[20] == 1 && header[22] == 1 && header[32] == 2 && header[34] == 16);
    CHECK(microphone_pcm16(0) == 0 && microphone_pcm16(0x7fffff00u) == 32767);
    CHECK(microphone_pcm16(0x80000000u) == -32768 && microphone_pcm16(0xffffff00u) == -1);
    CHECK(audio_init() == ESP_OK);
    int16_t *record = malloc(RECORD_SAMPLES * sizeof *record);
    CHECK(record && audio_record(record) == ESP_OK && captured == 64000 && !rx_enabled);
    for (size_t i = 0; i < RECORD_SAMPLES; ++i) CHECK(record[i] == (i % 2 ? -32768 : 32767));
    stall_read = true;
    CHECK(audio_record(record) == ESP_ERR_TIMEOUT && !rx_enabled);
    free(record);

    FILE *file = fopen(argv[1], "rb"); CHECK(file);
    char json[16000]; size_t len = fread(json, 1, sizeof json - 1, file);
    CHECK(!ferror(file) && feof(file)); fclose(file); json[len] = 0;
    music_data_t *data = malloc(sizeof *data); CHECK(data);
    CHECK(music_json_parse(json, len, data) == MUSIC_OK);
    void *memory = malloc(music_context_bytes()); music_context *ctx;
    CHECK(memory && music_init(memory, music_context_bytes(), data, &ctx) == MUSIC_OK);
    uint64_t acknowledged = 0;
    CHECK(audio_play(ctx, &acknowledged) == ESP_OK && !tx_enabled);
    CHECK(acknowledged == 80640 && sent_bytes == (80640 + 5 * 256) * 4);
    CHECK(music_render_reset(ctx) == MUSIC_OK);
    int16_t block[256]; size_t offset = 0, count;
    while ((count = music_render_next_pcm16(ctx, block, 256)) != 0) {
        for (size_t i = 0; i < count; ++i) {
            int16_t left, right;
            memcpy(&left, output + (offset + i) * 4, 2);
            memcpy(&right, output + (offset + i) * 4 + 2, 2);
            CHECK(left == block[i] && right == block[i]);
        }
        offset += count;
    }
    for (size_t i = offset * 4; i < sent_bytes; ++i) CHECK(output[i] == 0);
    stall_write = true;
    CHECK(audio_play(ctx, &acknowledged) == ESP_ERR_TIMEOUT && acknowledged == 0 && !tx_enabled);
    CHECK(music_json_parse("{\"ok\":false}", 12, data) != MUSIC_OK);
    free(memory); free(data);
    puts("PASS: state recovery, URL policy, WAV/slot conversion, 64000-frame recording,");
    puts("partial I2S writes/timeouts, exact shared-core stereo PCM, tail drain, error stop.");
    puts("Host fake I2S only; ESP-IDF build and physical hardware were not tested.");
    return 0;
}
