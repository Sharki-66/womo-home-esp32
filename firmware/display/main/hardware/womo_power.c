/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_power.c – Strom sparen bei dunklem Hintergrundlicht
 *
 * Eco an:  Pixeltakt WOMO_ECO_PCLK_HZ, eigene CPU_FREQ_MAX-Sperre frei → DFS darf
 *          bis WOMO_PM_CPU_MIN_MHZ heruntertakten.
 * Eco aus: Sperre holen (sofort WOMO_PM_CPU_MAX_MHZ), Pixeltakt zurück, DMA-Restart
 *          gegen ein im Eco verschobenes Bild (Bounce-Buffer-Unterlauf bei 80 MHz).
 * Pixeltakt und Restart greifen laut ESP-IDF erst im nächsten VSYNC.
 */
#include "womo_power.h"
#include "waveshare_rgb_lcd_port.h"
#include "display_config.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "sdkconfig.h"

static const char *TAG = "womo_power";

#if WOMO_PM_DFS_ENABLE && !CONFIG_PM_ENABLE
#error "WOMO_PM_DFS_ENABLE braucht CONFIG_PM_ENABLE=y in sdkconfig"
#endif

static bool s_eco = false;
#if WOMO_PM_DFS_ENABLE
static esp_pm_lock_handle_t s_cpu_max_lock = NULL;
#endif

esp_err_t womo_power_init(void)
{
#if WOMO_PM_DFS_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz       = WOMO_PM_CPU_MAX_MHZ,
        .min_freq_mhz       = WOMO_PM_CPU_MIN_MHZ,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "womo_light", &s_cpu_max_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_lock_create: %s", esp_err_to_name(err));
        return err;
    }
    esp_pm_lock_acquire(s_cpu_max_lock);   /* Normalbetrieb: fest auf Maximum */
    ESP_LOGI(TAG, "DFS %d–%d MHz, bei Licht fest %d MHz",
             WOMO_PM_CPU_MIN_MHZ, WOMO_PM_CPU_MAX_MHZ, WOMO_PM_CPU_MAX_MHZ);
#else
    ESP_LOGI(TAG, "DFS deaktiviert (WOMO_PM_DFS_ENABLE 0), nur Pixeltakt");
#endif
    return ESP_OK;
}

void womo_power_set_eco(bool eco)
{
    if (eco == s_eco) return;
    s_eco = eco;

    if (eco) {
        esp_err_t err = wavesahre_rgb_lcd_set_pclk(WOMO_ECO_PCLK_HZ);
        if (err != ESP_OK) ESP_LOGW(TAG, "Pixeltakt Eco: %s", esp_err_to_name(err));
#if WOMO_PM_DFS_ENABLE
        if (s_cpu_max_lock) esp_pm_lock_release(s_cpu_max_lock);
#endif
        ESP_LOGI(TAG, "Eco an: Pixeltakt %u kHz, CPU %d–%d MHz",
                 (unsigned)(WOMO_ECO_PCLK_HZ / 1000), WOMO_PM_CPU_MIN_MHZ, WOMO_PM_CPU_MAX_MHZ);
    } else {
#if WOMO_PM_DFS_ENABLE
        if (s_cpu_max_lock) esp_pm_lock_acquire(s_cpu_max_lock);
#endif
        esp_err_t err = wavesahre_rgb_lcd_set_pclk(EXAMPLE_LCD_PIXEL_CLOCK_HZ);
        if (err != ESP_OK) ESP_LOGW(TAG, "Pixeltakt normal: %s", esp_err_to_name(err));
        err = wavesahre_rgb_lcd_restart();
        if (err != ESP_OK) ESP_LOGW(TAG, "LCD-Restart: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "Eco aus: Pixeltakt %u kHz, CPU %d MHz (jetzt %d MHz)",
                 (unsigned)(EXAMPLE_LCD_PIXEL_CLOCK_HZ / 1000), WOMO_PM_CPU_MAX_MHZ,
                 esp_clk_cpu_freq() / 1000000);
    }
}

bool womo_power_is_eco(void)
{
    return s_eco;
}
