#include "device.h"
#include "music_json.h"
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

/* One shared, aligned arena: 4-second PCM/WAV is dead before response reception.
 * Both static storage and the one core context are reserved before audio starts. */
static union {
    uint32_t alignment;
    struct { uint8_t header[WAV_HEADER_BYTES]; int16_t pcm[RECORD_SAMPLES]; } record;
    uint8_t wav[RECORD_WAV_BYTES];
    char response[RESPONSE_LIMIT + 1];
} arena;
static music_data_t music;
static device_config_t config;
static music_context *context;
static device_state_t state = IDLE;
static const char *const state_names[] = {"IDLE", "RECORDING", "UPLOADING",
    "WAITING_FOR_MUSIC", "VALIDATING_MUSIC", "PLAYING"};

typedef enum { CAPTURE_JOB, PLAY_JOB } job_t;
typedef struct { esp_err_t error; uint64_t samples; } audio_result_t;
static QueueHandle_t commands, results, buttons;
static StaticQueue_t command_meta, result_meta, button_meta;
static uint8_t command_storage[sizeof(job_t)], result_storage[sizeof(audio_result_t)];
static uint8_t button_storage[sizeof(uint8_t)];
static StaticTask_t audio_task_meta;
static StackType_t audio_stack[4096 / sizeof(StackType_t)];

static void change_state(device_state_t next)
{
    if (!device_transition_allowed(state, next)) {
        state = IDLE;
        ESP_LOGE("device", "Illegal transition; IDLE");
        return;
    }
    state = next;
    ESP_LOGI("device", "%s", state_names[state]);
}

static void audio_task(void *arg)
{
    (void)arg;
    job_t job;
    for (;;) {
        xQueueReceive(commands, &job, portMAX_DELAY);
        audio_result_t result = {0};
        if (job == CAPTURE_JOB) {
            result.error = audio_record(arena.record.pcm);
            if (result.error == ESP_OK) result.samples = RECORD_SAMPLES;
        } else {
            result.error = audio_play(context, &result.samples);
        }
        xQueueSend(results, &result, portMAX_DELAY);
    }
}

static esp_err_t run_audio(job_t job)
{
    audio_result_t result;
    xQueueSend(commands, &job, portMAX_DELAY);
    xQueueReceive(results, &result, portMAX_DELAY);
    if (job == PLAY_JOB)
        ESP_LOGI("device", "I2S acknowledged %llu mono samples",
                 (unsigned long long)result.samples);
    return result.error;
}

static void uploaded(void) { change_state(WAITING_FOR_MUSIC); }

static esp_err_t run_cycle(void)
{
    change_state(RECORDING);
    esp_err_t err = run_audio(CAPTURE_JOB);
    if (err != ESP_OK) return err;
    record_wav_header(arena.wav, RECORD_SAMPLES);
    change_state(UPLOADING);
    err = device_wifi_wait();
    if (err != ESP_OK) return err;
    if (strncmp(config.backend_url, "https://", 8) == 0) {
        err = device_time_sync();
        if (err != ESP_OK) return err;
    }
    size_t length = 0;
    err = upload_recording(&config, arena.wav, RECORD_WAV_BYTES,
                           arena.response, sizeof arena.response, &length, uploaded);
    if (err != ESP_OK) return err;
    change_state(VALIDATING_MUSIC);
    /* Reuse the strict simulator cJSON decoder, including UTF-8, exact keys,
     * duplicate detection and bounded arrays, then core cross-field validation.
     * No chords are interpreted by firmware and no text hash is calculated. */
    music_status_t status = music_json_parse(arena.response, length, &music);
    if (status == MUSIC_OK) status = music_validate(&music);
    if (status == MUSIC_OK)
        status = music_init(context, music_context_bytes(), &music, &context);
    if (status != MUSIC_OK) {
        ESP_LOGW("device", "Music rejected: %s", music_status_code(status));
        return ESP_ERR_INVALID_ARG;
    }
    change_state(PLAYING);
    return run_audio(PLAY_JOB);
}

static void button_isr(void *arg)
{
    (void)arg;
    uint8_t pressed = 1;
    BaseType_t wake = pdFALSE;
    xQueueSendFromISR(buttons, &pressed, &wake);
    if (wake) portYIELD_FROM_ISR();
}

static esp_err_t button_init(void)
{
    gpio_config_t gpio = {.pin_bit_mask = UINT64_C(1) << CONFIG_FW_TRIGGER_GPIO,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_NEGEDGE};
    esp_err_t err = gpio_config(&gpio);
    if (err == ESP_OK) err = gpio_install_isr_service(0);
    if (err == ESP_OK) err = gpio_isr_handler_add(CONFIG_FW_TRIGGER_GPIO, button_isr, NULL);
    return err;
}

void app_main(void)
{
    ESP_LOGI("device", "IDLE; recording requires a button press");
    esp_err_t err = nvs_flash_init();
    /* Never erase NVS automatically on a version/partition error. */
    if (err == ESP_OK) err = device_config_load(&config);
    if (err == ESP_OK) err = audio_init();
    if (err == ESP_OK) err = device_wifi_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE("device", "Initialization failed (%s); IDLE. Configure and reboot.",
                 esp_err_to_name(err));
        return;
    }
    context = heap_caps_aligned_alloc(music_context_alignment(), music_context_bytes(),
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!context) {
        ESP_LOGE("device", "Context allocation failed; IDLE");
        return;
    }
    commands = xQueueCreateStatic(1, sizeof(job_t), command_storage, &command_meta);
    results = xQueueCreateStatic(1, sizeof(audio_result_t), result_storage, &result_meta);
    buttons = xQueueCreateStatic(1, sizeof(uint8_t), button_storage, &button_meta);
    if (!xTaskCreateStatic(audio_task, "audio", sizeof audio_stack, NULL, 6,
                           audio_stack, &audio_task_meta)) {
        ESP_LOGE("device", "Audio task creation failed; IDLE");
        return;
    }
    err = button_init();
    if (err != ESP_OK) {
        ESP_LOGE("device", "Button initialization failed (%s); IDLE", esp_err_to_name(err));
        return;
    }
    int64_t last_press = -300000;
    for (;;) {
        uint8_t pressed;
        xQueueReceive(buttons, &pressed, portMAX_DELAY);
        const int64_t now = esp_timer_get_time();
        if (now - last_press < 300000 || gpio_get_level(CONFIG_FW_TRIGGER_GPIO) != 0)
            continue;
        last_press = now;
        err = run_cycle();
        if (err != ESP_OK) ESP_LOGW("device", "Cycle failed (%s)", esp_err_to_name(err));
        /* Discard presses/bounce during a cycle; require a new physical edge. */
        xQueueReset(buttons);
        change_state(IDLE);
    }
}
