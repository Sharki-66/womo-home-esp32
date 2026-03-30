#include "sensors/hx711_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "hx711.h"
#include "sensor_config.h"

static const char *TAG = "hx711_app";

static hx711_t s_hx = {0};
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static hx711_snapshot_t s_last = {0};

static const uint32_t HX711_LOG_INTERVAL_MS = SENSOR_HX711_POLL_INTERVAL_MS;
static const size_t HX711_SAMPLES = SENSOR_HX711_AVG_SAMPLES;
static const uint32_t HX711_WAIT_TIMEOUT_MS = SENSOR_HX711_READY_TIMEOUT_MS;
static const uint32_t HX711_WAIT_BACKOFF_MS = SENSOR_HX711_READY_BACKOFF_MS;
static const uint32_t HX711_WAIT_RETRY = SENSOR_HX711_READY_RETRY_COUNT;

void hx711_app_sleep(void)
{
    /* SCK HIGH > 60 µs → HX711 Power-Down (~0,1 µA statt 1,5 mA). */
    hx711_power_down(&s_hx, true);
    ESP_LOGD(TAG, "HX711 Power-Down");
}

static float hx711_convert_to_kg(int32_t raw, int32_t offset, float scale)
{
    return (raw - offset) * scale / 1000.0f;
}

static esp_err_t hx711_apply_gain(hx711_gain_t gain)
{
    esp_err_t err = hx711_set_gain(&s_hx, gain);
    if (err == ESP_OK) {
        // kurze Settling-Zeit nach Gain-Umschaltung
        vTaskDelay(pdMS_TO_TICKS(50));

        // Dummy-Read nach optionalen Ready-Retries
        int32_t dummy = 0;
        esp_err_t wait_err = ESP_ERR_TIMEOUT;
        for (uint32_t r = 0; r <= HX711_WAIT_RETRY; r++) {
            wait_err = hx711_wait(&s_hx, HX711_WAIT_TIMEOUT_MS);
            if (wait_err == ESP_OK) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(HX711_WAIT_BACKOFF_MS));
        }

        if (wait_err == ESP_OK) {
            hx711_read_data(&s_hx, &dummy);
        } else {
            return wait_err;
        }
    }
    return err;
}

static void hx711_read_channel(hx711_gain_t gain, const char *label,
                               int32_t offset, float scale,
                               int32_t *raw_out, float *kg_out, bool *ok_out)
{
    *ok_out = false;
    *raw_out = 0;
    *kg_out = 0.0f;

    esp_err_t err = hx711_apply_gain(gain);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: gain switch fehlgeschlagen (%s)", label, esp_err_to_name(err));
        return;
    }

    int64_t t_start = esp_timer_get_time();
    int64_t sum = 0;
    for (size_t i = 0; i < HX711_SAMPLES; i++) {
        esp_err_t wait_err = ESP_ERR_TIMEOUT;
        int64_t t_before_wait = esp_timer_get_time();
        for (uint32_t r = 0; r <= HX711_WAIT_RETRY; r++) {
            err = hx711_wait(&s_hx, HX711_WAIT_TIMEOUT_MS);
            if (err == ESP_OK) {
                wait_err = ESP_OK;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(HX711_WAIT_BACKOFF_MS));
            t_before_wait = esp_timer_get_time();
        }

        if (wait_err != ESP_OK) {
            ESP_LOGW(TAG, "%s: wait timeout nach %lld us (sample %u)", label, (long long)(esp_timer_get_time() - t_before_wait), (unsigned)i);
            return;
        }

        int32_t raw_sample = 0;
        err = hx711_read_data(&s_hx, &raw_sample);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s: read failed (%s) bei sample %u", label, esp_err_to_name(err), (unsigned)i);
            return;
        }

        sum += raw_sample;
        ESP_LOGI(TAG, "%s: sample %u raw=%ld", label, (unsigned)i, (long)raw_sample);
    }

    int32_t avg = (int32_t)(sum / (int64_t)HX711_SAMPLES);
    float kg = hx711_convert_to_kg(avg, offset, scale);

    *raw_out = avg;
    *kg_out = kg;
    *ok_out = true;

    ESP_LOGI(TAG, "%s: avg=%ld (%.2f kg) samples=%u dur=%lld us", label, (long)avg, kg, (unsigned)HX711_SAMPLES, (long long)(esp_timer_get_time() - t_start));
}

static void hx711_task(void *arg)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(HX711_LOG_INTERVAL_MS);
    while (1) {
        int32_t raw_a = 0;
        int32_t raw_b = 0;
        float kg_a = 0.0f;
        float kg_b = 0.0f;
        bool ok_a = false;
        bool ok_b = false;

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            hx711_read_channel(HX711_GAIN_A_128, "HX711 A",
                               SENSOR_HX711_OFFSET_A, SENSOR_HX711_SCALE_A,
                               &raw_a, &kg_a, &ok_a);
            if (SENSOR_HX711_ENABLE_CHANNEL_B) {
                hx711_read_channel(HX711_GAIN_B_32, "HX711 B",
                                   SENSOR_HX711_OFFSET_B, SENSOR_HX711_SCALE_B,
                                   &raw_b, &kg_b, &ok_b);
            }

            s_last.raw_a = raw_a;
            s_last.raw_b = raw_b;
            s_last.kg_a = kg_a;
            s_last.kg_b = kg_b;
            s_last.valid_a = ok_a;
            s_last.valid_b = ok_b;
            s_last.timestamp_us = esp_timer_get_time();
            xSemaphoreGive(s_mutex);
        }

        if (ok_a || ok_b) {
            ESP_LOGI(TAG, "HX711 A=%ld (%.2f kg) %s | B=%ld (%.2f kg) %s",
                     (long)raw_a, kg_a, ok_a ? "ok" : "fail",
                     (long)raw_b, kg_b, ok_b ? "ok" : "fail");
        } else {
            ESP_LOGW(TAG, "HX711 read failed");
        }

        vTaskDelay(delay_ticks);
    }
}

esp_err_t hx711_app_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Mutex konnte nicht erstellt werden");
    }

    s_hx.dout = SENSOR_HX711_DOUT_GPIO;
    s_hx.pd_sck = SENSOR_HX711_SCK_GPIO;
    s_hx.gain = HX711_GAIN_A_128;

    esp_err_t err = hx711_init(&s_hx);
    ESP_RETURN_ON_ERROR(err, TAG, "HX711 init fehlgeschlagen");

    // DOUT intern hochziehen, damit "kein Sensor" erkannt werden kann
    gpio_set_pull_mode(SENSOR_HX711_DOUT_GPIO, GPIO_PULLUP_ONLY);

    // Präsenz-Check mit Re-Trys und Backoff, nach Startup-Delay – bricht nicht ab, falls Sensor später kommt
    vTaskDelay(pdMS_TO_TICKS(SENSOR_HX711_STARTUP_DELAY_MS));

    esp_err_t pres_err = ESP_ERR_TIMEOUT;
    for (uint32_t r = 0; r <= HX711_WAIT_RETRY; r++) {
        pres_err = hx711_wait(&s_hx, HX711_WAIT_TIMEOUT_MS);
        if (pres_err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(HX711_WAIT_BACKOFF_MS));
    }

    if (pres_err != ESP_OK) {
        ESP_LOGW(TAG, "HX711 nicht gefunden (DOUT bleibt high)");
        // nicht abbrechen – Sensor darf später erscheinen
    }

    BaseType_t r = xTaskCreatePinnedToCore(hx711_task, "hx711_task", 4096, NULL, 4, &s_task, 0);
    if (r != pdPASS) {
        s_task = NULL;
        ESP_RETURN_ON_ERROR(ESP_FAIL, TAG, "task start failed");
    }

    ESP_LOGI(TAG, "HX711 gestartet (DOUT=%d, SCK=%d)", SENSOR_HX711_DOUT_GPIO, SENSOR_HX711_SCK_GPIO);
    return ESP_OK;
}

esp_err_t hx711_app_get_snapshot(hx711_snapshot_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out ist NULL");
    ESP_RETURN_ON_FALSE(s_task != NULL, ESP_ERR_INVALID_STATE, TAG, "HX711 nicht gestartet");

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        *out = s_last;
        xSemaphoreGive(s_mutex);
    } else {
        *out = s_last;
    }

    return (out->valid_a || out->valid_b) ? ESP_OK : ESP_FAIL;
}
