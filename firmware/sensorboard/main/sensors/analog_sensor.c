/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensors/analog_sensor.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sensor_config.h"
#include <stdlib.h>

static const char *TAG = "analog";
static bool s_initialized = false;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static bool s_channel_configured[10] = {0};
static bool s_cali_initialized = false;
static bool s_cali_enabled = false;
static adc_cali_handle_t s_cali_handle = NULL;

static bool is_battery_channel(int channel)
{
    return (channel == SENSOR_BATT1_ADC_CHANNEL || channel == SENSOR_BATT2_ADC_CHANNEL);
}

static bool is_tank_channel(int channel)
{
    return (channel == SENSOR_TANK1_ADC_CHANNEL || channel == SENSOR_TANK2_ADC_CHANNEL);
}

static int apply_battery_calibration_mv(int channel, int mv)
{
    float scale = 1.0f;
    int offset_mv = 0;

    if (channel == SENSOR_BATT1_ADC_CHANNEL) {
        scale = SENSOR_BATT1_CAL_SCALE;
        offset_mv = SENSOR_BATT1_CAL_OFFSET_MV;
    } else if (channel == SENSOR_BATT2_ADC_CHANNEL) {
        scale = SENSOR_BATT2_CAL_SCALE;
        offset_mv = SENSOR_BATT2_CAL_OFFSET_MV;
    } else {
        return mv;
    }

    int calibrated = (int)((mv * scale) + offset_mv + 0.5f);
    return (calibrated < 0) ? 0 : calibrated;
}

static int apply_tank_calibration_mv(int channel, int mv)
{
    float scale = 1.0f;
    int offset_mv = 0;

    if (channel == SENSOR_TANK1_ADC_CHANNEL) {
        scale = SENSOR_TANK1_CAL_SCALE;
        offset_mv = SENSOR_TANK1_CAL_OFFSET_MV;
    } else if (channel == SENSOR_TANK2_ADC_CHANNEL) {
        scale = SENSOR_TANK2_CAL_SCALE;
        offset_mv = SENSOR_TANK2_CAL_OFFSET_MV;
    } else {
        return mv;
    }

    int calibrated = (int)((mv * scale) + offset_mv + 0.5f);
    return (calibrated < 0) ? 0 : calibrated;
}

static void init_adc_calibration(void)
{
    if (s_cali_initialized) {
        return;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (err == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC calibration enabled (curve fitting)");
    } else {
        s_cali_enabled = false;
        s_cali_handle = NULL;
        ESP_LOGW(TAG, "ADC calibration unavailable, fallback to raw formula");
    }
    s_cali_initialized = true;
}

static int raw_to_adc_mv(int raw)
{
    int adc_mv = 0;
    if (s_cali_enabled && s_cali_handle) {
        if (adc_cali_raw_to_voltage(s_cali_handle, raw, &adc_mv) == ESP_OK) {
            return adc_mv;
        }
    }
    return (raw * 3300) / 4095;
}

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

    init_adc_calibration();

    s_initialized = true;
    ESP_LOGI(TAG, "ADC init done (12dB, 12bit)");
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
    // ADC-Pinspannung in mV (kalibriert wenn verfuegbar).
    int adc_mv = raw_to_adc_mv(raw);

    // Nur Batteriemesskanäle laufen über den 100k/22k Spannungsteiler.
    int mv = adc_mv;
    if (is_battery_channel(channel)) {
        // Faktor 122/22: ADC sieht 22/122 der Eingangsspannung.
        mv = (adc_mv * 122) / 22;
    }
    int mv_cal = mv;
    if (is_battery_channel(channel)) {
        mv_cal = apply_battery_calibration_mv(channel, mv);
    } else if (is_tank_channel(channel)) {
        mv_cal = apply_tank_calibration_mv(channel, mv);
    }
    ESP_LOGI(TAG, "ch=%d  raw=%d  adc_mv=%d mV  mv=%d mV  cal=%d mV (%.2f V)",
             channel, raw, adc_mv, mv, mv_cal, mv_cal / 1000.0f);
    *mv_out = mv_cal;
    return ESP_OK;
}

static int compare_int(const void *a, const void *b)
{
    return (*(const int *)a) - (*(const int *)b);
}

esp_err_t analog_read_mv_avg(int channel, int *mv_out, int samples)
{
    if (mv_out == NULL || samples < 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (samples > 16) {
        samples = 16;
    }

    int values[16];
    int ok_count = 0;

    for (int i = 0; i < samples; i++) {
        int mv = 0;
        esp_err_t err = analog_read_mv(channel, &mv);
        if (err == ESP_OK) {
            values[ok_count++] = mv;
        }
    }

    if (ok_count == 0) {
        return ESP_ERR_TIMEOUT;
    }

    // Bei 3+ Werten: Median (robust gegen Ausreißer)
    if (ok_count >= 3) {
        qsort(values, ok_count, sizeof(int), compare_int);
        *mv_out = values[ok_count / 2];
    } else {
        // 1 oder 2 Werte: Mittelwert
        int sum = 0;
        for (int i = 0; i < ok_count; i++) {
            sum += values[i];
        }
        *mv_out = sum / ok_count;
    }

    return ESP_OK;
}


