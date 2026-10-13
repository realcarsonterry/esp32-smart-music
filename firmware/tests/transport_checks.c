#include "device.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static unsigned char body[RECORD_WAV_BYTES + 1024];
static unsigned char arena[RESPONSE_LIMIT + 1];
static const char reply[] = "{\"ok\":false,\"error\":\"fixture-only\"}";
static size_t sent, body_size, read_position;
static bool chunked, truncated, fail_write, cached_complete;
static int status, uploaded_count, closed_count, cleaned_count;
esp_err_t esp_crt_bundle_attach(void *p) { (void)p; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    CHECK(strcmp(c->url, "http://192.168.1.10:8787/api/v1/generate") == 0);
    CHECK(c->method == HTTP_METHOD_POST && c->disable_auto_redirect);
    CHECK(c->max_authorization_retries == -1 && c->crt_bundle_attach == esp_crt_bundle_attach);
    return body;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *k, const char *v)
{
    CHECK(h == body);
    if (strcmp(k, "Content-Type") == 0)
        CHECK(strcmp(v, "multipart/form-data; boundary=stage7-audio-boundary") == 0);
    else CHECK(strcmp(k, "Accept") == 0 && strcmp(v, "application/json") == 0);
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int n)
{
    CHECK(h == body && n > 0 && (size_t)n > RECORD_WAV_BYTES && (size_t)n < sizeof body);
    body_size = (size_t)n; return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t h, const char *p, int n)
{
    CHECK(h == body && !uploaded_count);
    if (fail_write) return 0;
    int count = n > 31 ? 31 : n;
    CHECK(sent + (size_t)count <= body_size);
    memcpy(body + sent, p, (size_t)count); sent += (size_t)count;
    return count;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h)
{
    CHECK(h == body && uploaded_count == 1 && sent == body_size);
    return chunked ? 0 : (int64_t)(sizeof reply - 1);
}
int esp_http_client_get_status_code(esp_http_client_handle_t h) { CHECK(h == body); return status; }
int esp_http_client_read(esp_http_client_handle_t h, char *p, int n)
{
    CHECK(h == body && sent == body_size && uploaded_count == 1);
    size_t limit = sizeof reply - 1 - (truncated ? 3 : 0);
    size_t count = limit - read_position;
    if (count > 7) count = 7;
    if (count > (size_t)n) count = (size_t)n;
    memcpy(p, reply + read_position, count); read_position += count;
    return (int)count;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h)
{
    CHECK(h == body);
    return !truncated && (cached_complete || read_position == sizeof reply - 1);
}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t h) { CHECK(h == body); return chunked; }
esp_err_t esp_http_client_close(esp_http_client_handle_t h) { CHECK(h == body); ++closed_count; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) { CHECK(h == body); ++cleaned_count; return ESP_OK; }
static void uploaded(void) { CHECK(sent == body_size); ++uploaded_count; }
static void reset(void)
{
    sent = body_size = read_position = 0;
    uploaded_count = closed_count = cleaned_count = 0;
    status = 200; chunked = truncated = fail_write = cached_complete = false;
    memset(arena, 0x53, sizeof arena);
    record_wav_header(arena, RECORD_SAMPLES);
}
static esp_err_t run(size_t capacity)
{
    device_config_t c = {.backend_url = "http://192.168.1.10:8787", .allow_lan_http = true};
    size_t length = 999;
    esp_err_t err = upload_recording(&c, arena, RECORD_WAV_BYTES,
                                      (char *)arena, capacity, &length, uploaded);
    CHECK(closed_count == 1 && cleaned_count == 1);
    if (err == ESP_OK) {
        CHECK(length == sizeof reply - 1 && memcmp(arena, reply, sizeof reply) == 0);
        const unsigned char *start = NULL;
        for (size_t i = 0; i + 4 <= sent; ++i)
            if (memcmp(body + i, "RIFF", 4) == 0) { start = body + i; break; }
        CHECK(start && memcmp(start + 8, "WAVEfmt ", 8) == 0);
        CHECK((size_t)(start - body) + RECORD_WAV_BYTES <= sent);
        for (size_t i = 44; i < RECORD_WAV_BYTES; ++i) CHECK(start[i] == 0x53);
        CHECK(memcmp(body + sent - 29, "\r\n--stage7-audio-boundary--\r\n", 29) == 0);
    }
    return err;
}
int main(void)
{
    reset(); CHECK(run(sizeof arena) == ESP_OK);
    reset(); chunked = true; CHECK(run(sizeof arena) == ESP_OK);
    reset(); cached_complete = true; CHECK(run(sizeof arena) == ESP_OK);
    reset(); truncated = true; CHECK(run(sizeof arena) != ESP_OK);
    reset(); chunked = true; truncated = true; CHECK(run(sizeof arena) != ESP_OK);
    reset(); status = 503; CHECK(run(sizeof arena) != ESP_OK);
    reset(); status = 302; CHECK(run(sizeof arena) != ESP_OK);
    reset(); CHECK(run(10) != ESP_OK);
    reset(); chunked = true; CHECK(run(10) != ESP_OK);
    reset(); fail_write = true; CHECK(run(sizeof arena) != ESP_OK && uploaded_count == 0);
    puts("PASS: exact multipart WAV, short HTTP writes, aliased response, cached/chunked reads,");
    puts("truncation/size/status rejection and cleanup. Fake HTTP only; no network request.");
    return 0;
}
