/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * power_mode.c
 *
 * Eco-Modus: Der Zustand wird bei jedem Tick neu abgeglichen (keine Flanken-
 * logik), alle Setter sind idempotent. Dadurch greift z. B. die WLAN-Pause
 * auch, wenn eine laufende Probeverbindung (wifi_trial) erst später endet.
 */

#include "power_mode.h"
#include "sensor_config.h"
#include "network/ble_manager.h"
#include "network/gasbee_ble_scanner.h"
#include "network/ruuvi_ble_scanner.h"
#include "network/wifi/sensor_wifi.h"
#include "sensors/bno055_sensor.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <sdkconfig.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

static const char *TAG = "power_mode";

// Wunsch kommt aus dem ESP-NOW-RX-Task, ausgewertet im TX-Task
static portMUX_TYPE     s_req_lock   = portMUX_INITIALIZER_UNLOCKED;
static bool             s_req_eco    = false;
static int64_t          s_req_us     = 0;
static bool             s_eco        = false;

// Ruuvi-Fenster: Start des aktuellen Fensters, Start des nächsten
static int64_t s_ruuvi_win_us  = 0;
static int64_t s_ruuvi_next_us = 0;

esp_err_t power_mode_init(void)
{
#if SENSOR_PM_DFS_ENABLE && CONFIG_PM_ENABLE
    esp_pm_config_t cfg = {
        .max_freq_mhz       = SENSOR_PM_CPU_MAX_MHZ,
        .min_freq_mhz       = SENSOR_PM_CPU_MIN_MHZ,
        .light_sleep_enable = false,   // würde ESP-NOW-Empfang und BLE-Scan lückenhaft machen
    };
    esp_err_t err = esp_pm_configure(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Taktanpassung aktiv: %d–%d MHz", SENSOR_PM_CPU_MIN_MHZ, SENSOR_PM_CPU_MAX_MHZ);
    } else {
        ESP_LOGW(TAG, "esp_pm_configure fehlgeschlagen: %s", esp_err_to_name(err));
    }
    return err;
#elif SENSOR_PM_DFS_ENABLE
    ESP_LOGW(TAG, "SENSOR_PM_DFS_ENABLE gesetzt, aber CONFIG_PM_ENABLE fehlt (sdkconfig)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    return ESP_OK;
#endif
}

void power_mode_set_requested(bool eco)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_req_lock);
    s_req_eco = eco;
    s_req_us  = now;
    taskEXIT_CRITICAL(&s_req_lock);
}

bool power_mode_is_eco(void)
{
    return s_eco;
}

#if SENSOR_ECO_BLE_WINDOWS
static bool ruuvi_window_open(int64_t now_us)
{
    if (now_us >= s_ruuvi_next_us) {
        s_ruuvi_win_us  = now_us;
        s_ruuvi_next_us = now_us + (int64_t)SENSOR_ECO_RUUVI_PERIOD_S * 1000000LL;
    }
    if (now_us - s_ruuvi_win_us >= (int64_t)SENSOR_ECO_RUUVI_WINDOW_S * 1000000LL) return false;
    return !ruuvi_ble_scanner_all_seen_since(s_ruuvi_win_us);
}
#endif

bool power_mode_tick(bool ac_present)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_req_lock);
    bool requested = s_req_eco &&
                     (now - s_req_us) < (int64_t)SENSOR_ECO_REQUEST_TIMEOUT_MS * 1000LL;
    taskEXIT_CRITICAL(&s_req_lock);
    bool eco = requested && !ac_present;

    bool changed = (eco != s_eco);
    if (changed) {
        s_eco = eco;
        ESP_LOGI(TAG, "Modus: %s%s", eco ? "Eco" : "Normal",
                 (!eco && requested && ac_present) ? " (Landstrom)" : "");
        if (eco) s_ruuvi_next_us = now;   // gleich ein Ruuvi-Fenster, dann im Takt
    }

#if SENSOR_ECO_WIFI_OFF
    // Probeverbindung neuer Zugangsdaten nicht abbrechen
    sensor_wifi_set_paused(eco && !sensor_wifi_is_trial_active());
#endif
#if SENSOR_ECO_IMU_SUSPEND
    bno055_app_set_suspended(eco);
#endif
#if SENSOR_ECO_BLE_WINDOWS
    bool scan = !eco || gasbee_ble_scanner_burst_expected(now) || ruuvi_window_open(now);
    ble_manager_set_scan_enabled(scan);
#endif
    return changed;
}
