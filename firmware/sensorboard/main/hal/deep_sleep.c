#include "deep_sleep.h"
#include "sensor_config.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/touch_pad.h"
#include "sensors/hx711_sensor.h"
#include "sensors/bno055_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

static const char *TAG = "deep_sleep";

#define WAKEUP_TOUCH_PAD  TOUCH_PAD_NUM6

/* Initiale Baseline (einmalig beim Boot gespeichert). */
static uint32_t s_initial_benchmark = 0;
static uint32_t s_threshold         = 0;

static void set_output_gpio(gpio_num_t pin, int level)
{
    gpio_config_t cfg = {
        .pin_bit_mask   = BIT64(pin),
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, level);
}

esp_err_t deep_sleep_init(void)
{
    ESP_ERROR_CHECK(touch_pad_init());
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    ESP_ERROR_CHECK(touch_pad_config(WAKEUP_TOUCH_PAD));

    touch_filter_config_t filter_cfg = {
        .mode         = TOUCH_PAD_FILTER_IIR_16,
        .debounce_cnt = 1,
        .noise_thr    = 0,
        .jitter_step  = 4,
        .smh_lvl      = TOUCH_PAD_SMOOTH_IIR_2,
    };
    touch_pad_filter_set_config(&filter_cfg);
    touch_pad_filter_enable();

    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    vTaskDelay(pdMS_TO_TICKS(500));

    /* Rohwert als Baseline verwenden — der Hardware-Filter (smooth/benchmark)
     * liefert auf diesem ESP32-S3 0x3FFFFF. Rohwert ist stabil (~16000). */
    touch_pad_read_raw_data(WAKEUP_TOUCH_PAD, &s_initial_benchmark);
    s_threshold = s_initial_benchmark * SENSOR_WAKEUP_TOUCH_THRESHOLD_PCT / 100;
    touch_pad_set_thresh(WAKEUP_TOUCH_PAD, s_threshold);

    ESP_LOGI(TAG, "Touch bereit: GPIO%d, Baseline=%" PRIu32 ", Schwelle=+%" PRIu32,
             SENSOR_WAKEUP_GPIO, s_initial_benchmark, s_threshold);
    return ESP_OK;
}

static void touch_monitor_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Touch-Monitor aktiv (GPIO%d).", SENSOR_WAKEUP_GPIO);

    while (true) {
        uint32_t raw = 0;
        touch_pad_read_raw_data(WAKEUP_TOUCH_PAD, &raw);

        int32_t delta = (int32_t)raw - (int32_t)s_initial_benchmark;
        bool touched  = (delta > (int32_t)s_threshold);

        if (touched) {
            ESP_LOGW(TAG, "<<< TOUCH! >>> GPIO%d | roh=%" PRIu32
                     " delta=+%" PRId32 " schwelle=+%" PRIu32,
                     SENSOR_WAKEUP_GPIO, raw, delta, s_threshold);
        } else {
            ESP_LOGI(TAG, "kein Touch     GPIO%d | roh=%" PRIu32
                     " delta=%" PRId32 " schwelle=+%" PRIu32,
                     SENSOR_WAKEUP_GPIO, raw, delta, s_threshold);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void deep_sleep_start_monitor(void)
{
    xTaskCreate(touch_monitor_task, "touch_mon", 3072, NULL, 3, NULL);
}

bool deep_sleep_wakeup_by_touch(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TOUCHPAD;
}

void deep_sleep_enter(void)
{
    ESP_LOGI(TAG, "Deep Sleep. Touch-Wakeup auf GPIO%d (HW, Schwelle=+%" PRIu32 ").",
             SENSOR_WAKEUP_GPIO, s_threshold);

    /* ── Peripherie abschalten ─────────────────────────────────────── */

    /* HX711: Power-Down (SCK HIGH > 60 µs → ~0,1 µA statt 1,5 mA) */
    hx711_app_sleep();

    /* BNO055: Suspend-Modus (~0,2 µA statt 12,3 mA) */
    bno055_app_sleep();

    /* RS485-Transceiver: DE/RTS LOW halten (kein Empfangsstrom).
     * gpio_hold_en() friert den Pegel ein, auch wenn UART im Sleep aus ist. */
    gpio_set_direction(SENSOR_RS485_DE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_RS485_DE_GPIO, 0);
    gpio_hold_en(SENSOR_RS485_DE_GPIO);

    /* Display 5V abschalten */
    set_output_gpio(SENSOR_DISPLAY_PWR_GPIO, 0);

    /* ── Sleep einleiten ───────────────────────────────────────────── */

    /* RTC_PERIPH eingeschaltet lassen, damit die Touch-FSM im Deep Sleep
     * weiter läuft.  Ohne diese Zeile fährt ESP-IDF die RTC-Peripherie
     * herunter → Touch-FSM stoppt → kein Wakeup möglich. */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    esp_sleep_enable_touchpad_wakeup();
    esp_deep_sleep_start();
    /* Kehrt nicht zurück */
}
