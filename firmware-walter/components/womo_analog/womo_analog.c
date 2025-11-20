#include "sdkconfig.h"
#include "walter_config.h"

#if WALTER_ENABLE_ANALOG

#include "womo_analog.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#define ANALOG_CHANNEL_COUNT 4

typedef enum {
    ANALOG_CH_BATT1 = 0,
    ANALOG_CH_BATT2,
    ANALOG_CH_TANK1,
    ANALOG_CH_TANK2,
} analog_channel_index_t;

static const char *TAG = "womo_analog";

static adc_oneshot_unit_handle_t s_adc_handle_unit1 = NULL;
static adc_oneshot_unit_handle_t s_adc_handle_unit2 = NULL;
static adc_cali_handle_t s_cali_handles[ANALOG_CHANNEL_COUNT];
static adc_channel_t s_adc_channels[ANALOG_CHANNEL_COUNT];
static adc_unit_t s_adc_units[ANALOG_CHANNEL_COUNT];
static bool s_initialized = false;

static float clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float map_range(float value,
                       float in_min,
                       float in_max,
                       float out_min,
                       float out_max)
{
    if (in_max <= in_min) {
        return out_min;
    }
    float ratio = (clampf(value, in_min, in_max) - in_min) / (in_max - in_min);
    return out_min + ratio * (out_max - out_min);
}

static adc_atten_t get_configured_atten(void)
{
#if CONFIG_WALTER_ANALOG_ATTEN_DB_0
    return ADC_ATTEN_DB_0;
#elif CONFIG_WALTER_ANALOG_ATTEN_DB_2_5
    return ADC_ATTEN_DB_2_5;
#elif CONFIG_WALTER_ANALOG_ATTEN_DB_6
    return ADC_ATTEN_DB_6;
#else
    return ADC_ATTEN_DB_12; // 11 dB legacy option now aliases to 12 dB in ESP-IDF 5.5+
#endif
}

static adc_channel_t channel_from_cfg(int cfg_value)
{
    if (cfg_value < 0) {
        cfg_value = 0;
    }
    if (cfg_value > 9) {
        cfg_value = 9;
    }
    return (adc_channel_t)cfg_value;
}

static adc_unit_t unit_from_cfg(int cfg_value)
{
    return (cfg_value == 2) ? ADC_UNIT_2 : ADC_UNIT_1;
}

static adc_oneshot_unit_handle_t get_unit_handle(adc_unit_t unit)
{
    return (unit == ADC_UNIT_2) ? s_adc_handle_unit2 : s_adc_handle_unit1;
}

static bool init_calibration(adc_cali_handle_t *handle, adc_unit_t unit, adc_atten_t atten, adc_bitwidth_t width)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cal_cfg = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = width,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal_cfg, handle) == ESP_OK) {
        return true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cal_cfg = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = width,
    };
    if (adc_cali_create_scheme_line_fitting(&cal_cfg, handle) == ESP_OK) {
        return true;
    }
#endif
    *handle = NULL;
    return false;
}

static void deinit_calibration(adc_cali_handle_t *handle)
{
    if (!handle || !*handle) {
        return;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(*handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(*handle);
#else
    (void)handle;
#endif
    *handle = NULL;
}

static esp_err_t raw_to_voltage_mv(adc_cali_handle_t cal, int raw, int *mv_out)
{
    if (!mv_out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cal) {
        return adc_cali_raw_to_voltage(cal, raw, mv_out);
    }
    // Fallback approximation assuming 12-bit width and 3.3V reference
    *mv_out = (raw * 3300) / 4095;
    return ESP_OK;
}

esp_err_t womo_analog_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    adc_atten_t atten = get_configured_atten();
    adc_bitwidth_t width = ADC_BITWIDTH_DEFAULT;

    s_adc_units[ANALOG_CH_BATT1] = unit_from_cfg(WALTER_BATT1_ADC_UNIT);
    s_adc_units[ANALOG_CH_BATT2] = unit_from_cfg(WALTER_BATT2_ADC_UNIT);
    s_adc_units[ANALOG_CH_TANK1] = unit_from_cfg(WALTER_TANK1_ADC_UNIT);
    s_adc_units[ANALOG_CH_TANK2] = unit_from_cfg(WALTER_TANK2_ADC_UNIT);

    s_adc_channels[ANALOG_CH_BATT1] = channel_from_cfg(WALTER_BATT1_ADC_CHANNEL);
    s_adc_channels[ANALOG_CH_BATT2] = channel_from_cfg(WALTER_BATT2_ADC_CHANNEL);
    s_adc_channels[ANALOG_CH_TANK1] = channel_from_cfg(WALTER_TANK1_ADC_CHANNEL);
    s_adc_channels[ANALOG_CH_TANK2] = channel_from_cfg(WALTER_TANK2_ADC_CHANNEL);

    bool need_unit1 = false;
    bool need_unit2 = false;
    for (int i = 0; i < ANALOG_CHANNEL_COUNT; ++i) {
        if (s_adc_units[i] == ADC_UNIT_1) {
            need_unit1 = true;
        } else if (s_adc_units[i] == ADC_UNIT_2) {
            need_unit2 = true;
        }
    }

    if (need_unit1) {
        adc_oneshot_unit_init_cfg_t unit_cfg1 = {
            .unit_id = ADC_UNIT_1,
        };
        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg1, &s_adc_handle_unit1), TAG, "Failed to create ADC unit 1");
    }
    if (need_unit2) {
        adc_oneshot_unit_init_cfg_t unit_cfg2 = {
            .unit_id = ADC_UNIT_2,
        };
        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg2, &s_adc_handle_unit2), TAG, "Failed to create ADC unit 2");
    }

    for (int i = 0; i < ANALOG_CHANNEL_COUNT; ++i) {
        adc_oneshot_unit_handle_t handle = get_unit_handle(s_adc_units[i]);
        if (handle == NULL) {
            ESP_LOGE(TAG, "ADC unit handle missing for channel %d", i);
            return ESP_ERR_INVALID_STATE;
        }
        adc_oneshot_chan_cfg_t chan_cfg = {
            .bitwidth = width,
            .atten = atten,
        };
        ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(handle, s_adc_channels[i], &chan_cfg), TAG, "Chan cfg failed");
        init_calibration(&s_cali_handles[i], s_adc_units[i], atten, width);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Analog monitor initialized (atten=%d dB, samples=%d)", (int)atten, WALTER_ANALOG_SAMPLES_PER_READ);
    return ESP_OK;
}

void womo_analog_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    for (int i = 0; i < ANALOG_CHANNEL_COUNT; ++i) {
        deinit_calibration(&s_cali_handles[i]);
    }

    if (s_adc_handle_unit1) {
        adc_oneshot_del_unit(s_adc_handle_unit1);
        s_adc_handle_unit1 = NULL;
    }
    if (s_adc_handle_unit2) {
        adc_oneshot_del_unit(s_adc_handle_unit2);
        s_adc_handle_unit2 = NULL;
    }

    s_initialized = false;
}

static esp_err_t sample_channel(analog_channel_index_t index, int *raw_out)
{
    if (!raw_out) {
        return ESP_ERR_INVALID_ARG;
    }
    adc_oneshot_unit_handle_t handle = get_unit_handle(s_adc_units[index]);
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int accum = 0;
    for (int sample = 0; sample < WALTER_ANALOG_SAMPLES_PER_READ; ++sample) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(handle, s_adc_channels[index], &raw);
        if (err != ESP_OK) {
            return err;
        }
        accum += raw;
    }
    *raw_out = accum / WALTER_ANALOG_SAMPLES_PER_READ;
    return ESP_OK;
}

static uint8_t compute_percentage(int mv, int empty_mv, int full_mv)
{
    if (full_mv <= empty_mv) {
        return 0;
    }
    if (mv <= empty_mv) {
        return 0;
    }
    if (mv >= full_mv) {
        return 100;
    }
    float span = (float)(full_mv - empty_mv);
    float rel = ((float)mv - (float)empty_mv) / span;
    int pct = (int)(rel * 100.0f + 0.5f);
    if (pct < 0) {
        return 0;
    }
    if (pct > 100) {
        return 100;
    }
    return (uint8_t)pct;
}

esp_err_t womo_analog_read(womo_analog_data_t *out)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    const float battery_adc_min = (float)WALTER_BATTERY_ADC_MIN_MV;
    const float battery_adc_max = (float)WALTER_BATTERY_ADC_MAX_MV;

    int raw = 0;
    if (sample_channel(ANALOG_CH_BATT1, &raw) == ESP_OK) {
        int mv = 0;
        if (raw_to_voltage_mv(s_cali_handles[ANALOG_CH_BATT1], raw, &mv) == ESP_OK) {
            out->battery_mv[0] = mv;
            out->battery_v[0] = map_range((float)mv,
                                          battery_adc_min,
                                          battery_adc_max,
                                          WALTER_BATTERY_MIN_V,
                                          WALTER_BATTERY_MAX_V);
            out->battery_valid[0] = true;
        }
    }
    if (sample_channel(ANALOG_CH_BATT2, &raw) == ESP_OK) {
        int mv = 0;
        if (raw_to_voltage_mv(s_cali_handles[ANALOG_CH_BATT2], raw, &mv) == ESP_OK) {
            out->battery_mv[1] = mv;
            out->battery_v[1] = map_range((float)mv,
                                          battery_adc_min,
                                          battery_adc_max,
                                          WALTER_BATTERY_MIN_V,
                                          WALTER_BATTERY_MAX_V);
            out->battery_valid[1] = true;
        }
    }

    if (sample_channel(ANALOG_CH_TANK1, &raw) == ESP_OK) {
        int mv = 0;
        if (raw_to_voltage_mv(s_cali_handles[ANALOG_CH_TANK1], raw, &mv) == ESP_OK) {
            out->tank_mv[0] = mv;
            out->tank_v[0] = (float)mv / 1000.0f;
            out->tank_percent[0] = compute_percentage(mv,
                                                      WALTER_TANK_EMPTY_MV,
                                                      WALTER_TANK_FULL_MV);
            out->tank_valid[0] = true;
        }
    }
    if (sample_channel(ANALOG_CH_TANK2, &raw) == ESP_OK) {
        int mv = 0;
        if (raw_to_voltage_mv(s_cali_handles[ANALOG_CH_TANK2], raw, &mv) == ESP_OK) {
            out->tank_mv[1] = mv;
            out->tank_v[1] = (float)mv / 1000.0f;
            out->tank_percent[1] = compute_percentage(mv,
                                                      WALTER_TANK_EMPTY_MV,
                                                      WALTER_TANK_FULL_MV);
            out->tank_valid[1] = true;
        }
    }

    return ESP_OK;
}

#else

#include "womo_analog.h"
#include "esp_err.h"

esp_err_t womo_analog_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void womo_analog_deinit(void)
{
}

esp_err_t womo_analog_read(womo_analog_data_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
