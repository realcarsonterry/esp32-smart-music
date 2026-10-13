#include "device.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdatomic.h>
#include <string.h>

static StaticEventGroup_t wifi_group_storage;
static EventGroupHandle_t wifi_group;
static atomic_uint retries;
static bool time_initialized, time_ready;
#define CONNECTED BIT0
#define FAILED BIT1

static esp_err_t config_string(nvs_handle_t nvs, bool opened, const char *key,
                              const char *fallback, char *out, size_t capacity)
{
    if (opened) {
        size_t size = capacity;
        esp_err_t err = nvs_get_str(nvs, key, out, &size);
        if (err == ESP_OK) return ESP_OK;
        if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    }
    if (strlen(fallback) >= capacity) return ESP_ERR_INVALID_SIZE;
    memcpy(out, fallback, strlen(fallback) + 1);
    return ESP_OK;
}

esp_err_t device_config_load(device_config_t *c)
{
    memset(c, 0, sizeof *c);
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open("music_device", NVS_READONLY, &nvs);
    bool opened = err == ESP_OK;
    if (!opened && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = config_string(nvs, opened, "ssid", CONFIG_FW_WIFI_SSID, c->ssid, sizeof c->ssid);
    if (err == ESP_OK) err = config_string(nvs, opened, "password", CONFIG_FW_WIFI_PASSWORD,
                                          c->password, sizeof c->password);
    if (err == ESP_OK) err = config_string(nvs, opened, "token", CONFIG_FW_DEVICE_TOKEN,
                                          c->token, sizeof c->token);
    if (err == ESP_OK) err = config_string(nvs, opened, "backend_url", CONFIG_FW_BACKEND_URL,
                                          c->backend_url, sizeof c->backend_url);
    if (opened) nvs_close(nvs);
#ifdef CONFIG_FW_ALLOW_LAN_HTTP
    c->allow_lan_http = true;
#endif
    if (err != ESP_OK) return err;
    if (!c->ssid[0] || !backend_url_allowed(c->backend_url, c->allow_lan_http))
        return ESP_ERR_INVALID_ARG;
    /* Prevent header injection without ever printing the token. */
    for (const unsigned char *p = (const unsigned char *)c->token; *p; ++p)
        if (*p < 0x21 || *p > 0x7e) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_group, CONNECTED);
        if (atomic_fetch_add(&retries, 1) < 5 && esp_wifi_connect() == ESP_OK) return;
        xEventGroupSetBits(wifi_group, FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&retries, 0);
        xEventGroupClearBits(wifi_group, FAILED);
        xEventGroupSetBits(wifi_group, CONNECTED);
    }
}

esp_err_t device_wifi_init(const device_config_t *c)
{
    wifi_group = xEventGroupCreateStatic(&wifi_group_storage);
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
    if (err != ESP_OK) return err;
    /* Do not persist an extra Wi-Fi credential copy in the Wi-Fi driver's NVS. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, c->ssid, strlen(c->ssid));
    memcpy(wifi.sta.password, c->password, strlen(c->password));
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    memset(&wifi, 0, sizeof wifi);
    return err == ESP_OK ? esp_wifi_start() : err;
}

esp_err_t device_wifi_wait(void)
{
    if (!(xEventGroupGetBits(wifi_group) & CONNECTED)) {
        atomic_store(&retries, 0);
        xEventGroupClearBits(wifi_group, FAILED);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) return err;
        EventBits_t bits = xEventGroupWaitBits(wifi_group, CONNECTED | FAILED,
                                              pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
        if (!(bits & CONNECTED)) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t device_time_sync(void)
{
    if (time_ready) return ESP_OK;
    if (!time_initialized) {
        esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_FW_NTP_SERVER);
        esp_err_t err = esp_netif_sntp_init(&ntp);
        if (err != ESP_OK) return err;
        time_initialized = true;
    }
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (err == ESP_OK) time_ready = true;
    return err;
}
