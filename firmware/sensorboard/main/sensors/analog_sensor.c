#include "sensors/analog_sensor.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "analog";
static bool s_initialized = false;
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
    configure_channel(ADC_CHANNEL_3); // GPIO4 (Batt1 Kfz)
    configure_channel(ADC_CHANNEL_4); // GPIO5 (Batt2 Board)
    configure_channel(ADC_CHANNEL_0); // GPIO1 (Tank1 Frisch)
    configure_channel(ADC_CHANNEL_1); // GPIO2 (Tank2 Grau)

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
    // ADC-Spannung (0-3.3V bei 12-bit)
    int adc_mv = (raw * 3300) / 4095;

    // ALLE Kanäle nutzen Spannungsteiler 100kΩ/22kΩ
    // Faktor 122/22: ADC sieht 22/122 der Eingangsspannung
    int mv = (adc_mv * 122) / 22;
    ESP_LOGI(TAG, "ch=%d  raw=%d  adc_mv=%d mV  mv=%d mV (%.2f V)", channel, raw, adc_mv, mv, mv / 1000.0f);
    *mv_out = mv;
    return ESP_OK;
}


