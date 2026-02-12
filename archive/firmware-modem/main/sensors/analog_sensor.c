#include "sensors/analog_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "analog";
static bool s_initialized = false;
static TaskHandle_t s_log_task = NULL;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static bool s_channel_configured[10] = {0};

static esp_err_t configure_channel(adc_channel_t ch)
{
    if (ch < ADC_CHANNEL_0 || ch > ADC_CHANNEL_9) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_channel_configured[ch]) {
        return ESP_OK;
    }
    adc_oneshot_chan_cfg_t cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t err = adc_oneshot_config_channel(s_adc_handle, ch, &cfg);
    if (err == ESP_OK) {
        s_channel_configured[ch] = true;
    }
    return err;
}

esp_err_t analog_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_cfg, &s_adc_handle), TAG, "ADC oneshot init fehlgeschlagen");

    // 12 dB deckt bis ca. 3.3 V ab
    configure_channel(ADC_CHANNEL_0); // GPIO1
    configure_channel(ADC_CHANNEL_5); // GPIO6 (Batt1)
    configure_channel(ADC_CHANNEL_6); // GPIO7 (Batt2)
    configure_channel(ADC_CHANNEL_7); // GPIO8 (Tank1)
    configure_channel(ADC_CHANNEL_8); // GPIO9 (Tank2)

    s_initialized = true;
    ESP_LOGI(TAG, "ADC init done (12dB, 12bit, Vref=1100mV)");
    return ESP_OK;
}

esp_err_t analog_read_raw(int channel, int *raw_out)
{
    if (!s_initialized) {
        analog_init();
    }
    if (raw_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    adc_channel_t ch = (adc_channel_t)channel;
    if (ch < ADC_CHANNEL_0 || ch > ADC_CHANNEL_9) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t cfg_err = configure_channel(ch);
    if (cfg_err != ESP_OK) {
        return cfg_err;
    }
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, ch, &raw);
    if (err != ESP_OK) {
        return err;
    }
    *raw_out = raw;
    return ESP_OK;
}

esp_err_t analog_read_mv(int channel, int *mv_out)
{
    int raw = 0;
    esp_err_t err = analog_read_raw(channel, &raw);
    if (err != ESP_OK) {
        return err;
    }
    if (mv_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int mv = (raw * 3300) / 4095;
    *mv_out = mv;
    return ESP_OK;
}

esp_err_t analog_read_bat_gpio1_mv(int *mv_out)
{
    int mv = 0;
    esp_err_t err = analog_read_mv(ADC_CHANNEL_0, &mv);
    if (err != ESP_OK) {
        return err;
    }
    // Teiler 200k (oben) / 100k (unten): ADC sieht 1/3 der Batteriespannung
    *mv_out = mv * 3;
    return ESP_OK;
}

static void analog_log_task(void *arg)
{
    while (1) {
        int mv = 0;
        if (analog_read_bat_gpio1_mv(&mv) == ESP_OK) {
            ESP_LOGI(TAG, "GPIO1/BAT: %d mV", mv);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void analog_start_logging(void)
{
    if (s_log_task) {
        return;
    }
    BaseType_t r = xTaskCreatePinnedToCore(analog_log_task, "analog_log", 2048, NULL, 4, &s_log_task, 0);
    if (r != pdPASS) {
        ESP_LOGW(TAG, "Analog-Log-Task konnte nicht gestartet werden");
        s_log_task = NULL;
    }
}
