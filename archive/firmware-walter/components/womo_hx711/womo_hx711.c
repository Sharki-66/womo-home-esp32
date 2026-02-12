#include "womo_hx711.h"

#if WALTER_ENABLE_HX711

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "womo_hx711";

static hx711_gain_t to_hx_gain(womo_hx711_gain_t gain)
{
    switch (gain) {
    case WOMO_HX711_GAIN_A_128:
        return HX711_GAIN_A_128;
    case WOMO_HX711_GAIN_B_32:
        return HX711_GAIN_B_32;
    case WOMO_HX711_GAIN_A_64:
        return HX711_GAIN_A_64;
    default:
        return HX711_GAIN_A_128;
    }
}

static esp_err_t apply_gain_change(womo_hx711_t *dev)
{
    if (dev->current_gain == dev->target_gain) {
        return ESP_OK;
    }

    esp_err_t err = hx711_set_gain(&dev->hx, to_hx_gain(dev->target_gain));
    if (err == ESP_OK) {
        // HX711 needs time to stabilize after gain change
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Extra dummy read - first read after gain change contains old data
        int32_t dummy;
        hx711_wait(&dev->hx, WALTER_HX711_READY_TIMEOUT_MS);
        hx711_read_data(&dev->hx, &dummy);
        
        dev->current_gain = dev->target_gain;
    }
    return err;
}

static esp_err_t wait_until_ready(womo_hx711_t *dev)
{
    for (int attempt = 0; attempt <= (int)WALTER_HX711_READY_RETRY_COUNT; ++attempt) {
        esp_err_t err = hx711_wait(&dev->hx, WALTER_HX711_READY_TIMEOUT_MS);
        if (err != ESP_ERR_TIMEOUT) {
            return err;
        }

        if (attempt == (int)WALTER_HX711_READY_RETRY_COUNT) {
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(WALTER_HX711_READY_BACKOFF_MS));
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t womo_hx711_init(womo_hx711_t *dev, gpio_num_t dout_gpio, gpio_num_t sck_gpio, womo_hx711_gain_t gain)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->hx.dout = dout_gpio;
    dev->hx.pd_sck = sck_gpio;
    dev->hx.gain = to_hx_gain(gain);

    esp_err_t err = hx711_init(&dev->hx);
    if (err != ESP_OK) {
        dev->initialized = false;
        return err;
    }

    dev->dout_gpio = dout_gpio;
    dev->sck_gpio = sck_gpio;
    dev->current_gain = gain;
    dev->target_gain = gain;
    dev->offset = (int32_t)WALTER_HX711_OFFSET_A;
    dev->scale = WALTER_HX711_SCALE_A;
    dev->initialized = true;

    ESP_LOGI(TAG, "HX711 ready on DOUT=%d SCK=%d", (int)dout_gpio, (int)sck_gpio);
    return ESP_OK;
}

bool womo_hx711_is_ready(const womo_hx711_t *dev)
{
    if (!dev || !dev->initialized) {
        return false;
    }

    bool ready = false;
    // Cast away const since hx711_is_ready doesn't actually modify the device state
    if (hx711_is_ready((hx711_t *)&dev->hx, &ready) != ESP_OK) {
        return false;
    }
    return ready;
}

esp_err_t womo_hx711_read_raw(womo_hx711_t *dev, int32_t *out_value)
{
    if (!dev || !dev->initialized || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = apply_gain_change(dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update HX711 gain (%s)", esp_err_to_name(err));
        return err;
    }

    err = wait_until_ready(dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HX711 not ready before timeout (%s)", esp_err_to_name(err));
        return err;
    }

    err = hx711_read_data(&dev->hx, out_value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HX711 read failed (%s)", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t womo_hx711_read_average(womo_hx711_t *dev, size_t samples, int32_t *out_value)
{
    if (!dev || !dev->initialized || !out_value || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Minimum 3 samples for median calculation
    if (samples < 3) {
        samples = 3;
    }

    // Apply gain change ONCE before sampling (if needed)
    esp_err_t err = apply_gain_change(dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set gain: %s", esp_err_to_name(err));
        return err;
    }

    int32_t values[samples];
    for (size_t i = 0; i < samples; ++i) {
        // Wait for HX711 to be ready with NEW data
        // HX711 updates at 10Hz or 80Hz, we must wait for DOUT=LOW before each read
        err = wait_until_ready(dev);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Sample %zu: HX711 timeout waiting for data", i);
            return err;
        }
        
        int32_t raw = 0;
        err = hx711_read_data(&dev->hx, &raw);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Sample %zu: Read failed", i);
            return err;
        }
        values[i] = raw;
    }

    // Simple bubble sort for median calculation (robust against outliers)
    for (size_t i = 0; i < samples - 1; i++) {
        for (size_t j = i + 1; j < samples; j++) {
            if (values[i] > values[j]) {
                int32_t tmp = values[i];
                values[i] = values[j];
                values[j] = tmp;
            }
        }
    }

    // Return median value
    *out_value = values[samples / 2];
    return ESP_OK;
}

esp_err_t womo_hx711_set_gain(womo_hx711_t *dev, womo_hx711_gain_t gain)
{
    if (!dev || !dev->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (gain != WOMO_HX711_GAIN_A_128 &&
        gain != WOMO_HX711_GAIN_B_32 &&
        gain != WOMO_HX711_GAIN_A_64) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->target_gain = gain;
    return apply_gain_change(dev);
}

esp_err_t womo_hx711_power_down(womo_hx711_t *dev)
{
    if (!dev || !dev->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    return hx711_power_down(&dev->hx, true);
}

esp_err_t womo_hx711_power_up(womo_hx711_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = hx711_power_down(&dev->hx, false);
    if (err == ESP_OK) {
        dev->initialized = true;
    }
    return err;
}

void womo_hx711_set_offset(womo_hx711_t *dev, int32_t offset)
{
    if (dev) {
        dev->offset = offset;
    }
}

void womo_hx711_set_scale(womo_hx711_t *dev, float scale)
{
    if (dev) {
        dev->scale = scale;
    }
}

int32_t womo_hx711_get_offset(const womo_hx711_t *dev)
{
    return dev ? dev->offset : 0;
}

float womo_hx711_get_scale(const womo_hx711_t *dev)
{
    return dev ? dev->scale : 1.0f;
}

float womo_hx711_convert_to_units(const womo_hx711_t *dev, int32_t raw)
{
    if (!dev) {
        return 0.0f;
    }
    return (raw - dev->offset) * dev->scale;
}
#endif
