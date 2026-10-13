#ifndef TEST_HTTP_H
#define TEST_HTTP_H
#include "esp_err.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 4
#define HTTP_METHOD_POST 1
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int method, timeout_ms;
    bool disable_auto_redirect;
    int max_authorization_retries;
    esp_err_t (*crt_bundle_attach)(void *);
    int buffer_size, buffer_size_tx;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char *, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
bool esp_http_client_is_chunked_response(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
#endif
