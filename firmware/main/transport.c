#include "device.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <stdio.h>
#include <string.h>

#if defined(CONFIG_ESP_TLS_INSECURE) || defined(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY)
#error "Firmware requires TLS server certificate verification"
#endif
#if !defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
#error "Enable the certificate bundle; do not bypass server verification"
#endif

static const char prefix[] = "--stage7-audio-boundary\r\n"
    "Content-Disposition: form-data; name=\"audio\"; filename=\"recording.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";
static const char suffix[] = "\r\n--stage7-audio-boundary--\r\n";

static esp_err_t write_all(esp_http_client_handle_t client, const void *data, size_t size)
{
    const char *p = data;
    while (size) {
        size_t chunk = size > 4096 ? 4096 : size;
        int sent = esp_http_client_write(client, p, (int)chunk);
        if (sent <= 0 || (size_t)sent > chunk) return ESP_FAIL;
        p += sent; size -= (size_t)sent;
    }
    return ESP_OK;
}

esp_err_t upload_recording(const device_config_t *c,
                           const uint8_t *wav, size_t wav_bytes,
                           char *response, size_t capacity, size_t *length,
                           upload_done_fn upload_done)
{
    *length = 0;
    if (!backend_url_allowed(c->backend_url, c->allow_lan_http) ||
        wav_bytes != RECORD_WAV_BYTES || capacity < 2) return ESP_ERR_INVALID_ARG;
    char url[320], authorization[sizeof c->token + 8];
    int size = snprintf(url, sizeof url, "%s/api/v1/generate", c->backend_url);
    if (size < 0 || (size_t)size >= sizeof url) return ESP_ERR_INVALID_SIZE;
    esp_http_client_config_t http = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 30000,
        .disable_auto_redirect = true, .max_authorization_retries = -1,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048, .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_set_header(client, "Content-Type",
                         "multipart/form-data; boundary=stage7-audio-boundary");
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Accept", "application/json");
    if (err == ESP_OK && c->token[0]) {
        snprintf(authorization, sizeof authorization, "Bearer %s", c->token);
        err = esp_http_client_set_header(client, "Authorization", authorization);
        memset(authorization, 0, sizeof authorization);
    }
    const size_t body_size = sizeof prefix - 1 + wav_bytes + sizeof suffix - 1;
    if (err == ESP_OK) err = esp_http_client_open(client, (int)body_size);
    if (err == ESP_OK) err = write_all(client, prefix, sizeof prefix - 1);
    if (err == ESP_OK) err = write_all(client, wav, wav_bytes);
    if (err == ESP_OK) err = write_all(client, suffix, sizeof suffix - 1);
    if (err == ESP_OK) {
        upload_done();
        int64_t expected = esp_http_client_fetch_headers(client);
        if (expected < 0 || expected >= (int64_t)capacity ||
            esp_http_client_get_status_code(client) != 200) err = ESP_FAIL;
        while (err == ESP_OK) {
            if (*length == capacity - 1) {
                char extra;
                int amount = esp_http_client_read(client, &extra, 1);
                if (amount != 0) err = ESP_ERR_INVALID_SIZE;
                break;
            }
            size_t remaining = capacity - 1 - *length;
            int amount = esp_http_client_read(client, response + *length,
                                              (int)(remaining > 2048 ? 2048 : remaining));
            if (amount < 0) { err = ESP_FAIL; break; }
            if (amount == 0) {
                if (!esp_http_client_is_complete_data_received(client)) err = ESP_FAIL;
                break;
            }
            *length += (size_t)amount;
        }
        if (err == ESP_OK && (!*length || !esp_http_client_is_complete_data_received(client) ||
            (!esp_http_client_is_chunked_response(client) && expected != (int64_t)*length)))
            err = ESP_FAIL;
        response[*length] = '\0';
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}
