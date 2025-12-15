#include "modem_manager.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "freertos/task.h"
#include "WalterModem.h"
#include "walter_config.h"
#include <atomic>

// Bereits vorhandene Symbole aus main.cpp
extern esp_err_t lte_runtime_set_enabled(bool enable);
extern esp_err_t lte_runtime_set_quiet(bool quiet);
extern esp_err_t lte_runtime_restart(void);
extern std::atomic<bool> s_lte_runtime_active;
extern WalterModem modem;

static const char *TAG = "modem_mgr";

static esp_err_t wait_for_lte_state(bool want_active, TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        const bool active = s_lte_runtime_active.load();
        const WalterModemNetworkRegState reg = modem.getNetworkRegState();
        const bool reg_ok = (reg == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
                             reg == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING);

        if (want_active) {
            if (active || reg_ok) {
                return ESP_OK;
            }
        } else {
            // Für „OFF“ reicht: Runtime wirklich aus ODER das Modem meldet keinen registrierten Zustand.
            if (!active || (!reg_ok && reg != WALTER_MODEM_NETWORK_REG_SEARCHING)) {
                return ESP_OK;
            }
        }

        ESP_LOGD(TAG,
                 "Waiting for LTE state %s (active=%s reg=%d)",
                 want_active ? "ON" : "OFF",
                 active ? "ON" : "OFF",
                 (int)reg);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t modem_manager_set_lte_enabled(bool enable, TickType_t timeout_ticks)
{
    esp_err_t err = lte_runtime_set_enabled(enable);
    if (err != ESP_OK) {
        return err;
    }
    err = wait_for_lte_state(enable, timeout_ticks);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LTE state transition timeout (target=%s)", enable ? "on" : "off");
        if (enable && lte_runtime_restart() == ESP_OK) {
            err = wait_for_lte_state(true, timeout_ticks);
        }
    }
    return err;
}

esp_err_t modem_manager_run_gnss_cycle(TickType_t timeout_ticks, womo_gps_data_t *out_fix)
{
    if (!out_fix) {
        return ESP_ERR_INVALID_ARG;
    }

    const TickType_t lte_wait = pdMS_TO_TICKS(30000);
    const TickType_t fix_poll_delay = pdMS_TO_TICKS(200);

    // 1) LTE an und warten (für Clock/Assistance)
    ESP_LOGI(TAG, "Enable LTE for GNSS prep");
    ESP_ERROR_CHECK_WITHOUT_ABORT(modem_manager_set_lte_enabled(true, lte_wait));

    // Merke aktuellen Fix-TS, um neuen Fix zu erkennen
    womo_gps_data_t prev_fix = {};
    bool have_prev = (womo_gps_get_last_fix(&prev_fix) == ESP_OK);
    int64_t prev_ts = have_prev ? prev_fix.timestamp : 0;

    // 2) LTE nur abschalten, wenn explizit gewünscht (PAUSE_LTE=1)
#if WALTER_GPS_PAUSE_LTE
    ESP_LOGI(TAG, "Disable LTE for GNSS fix window");
    modem_manager_set_lte_enabled(false, pdMS_TO_TICKS(10000));

    // Safety delay to ensure LTE task has fully released the modem
    vTaskDelay(pdMS_TO_TICKS(1000));
#else
    ESP_LOGI(TAG, "Keeping LTE enabled during GNSS request (PAUSE_LTE=0)");
    esp_err_t quiet_err = lte_runtime_set_quiet(true);
    bool quiet_applied = (quiet_err == ESP_OK);
    if (!quiet_applied) {
        ESP_LOGW(TAG, "LTE quiet-mode request failed (err=%s)", esp_err_to_name(quiet_err));
    }
#endif

    // 3) GNSS-Fix anfordern (jetzt ohne laufenden LTE-Link)
    ESP_LOGI(TAG, "Request GNSS fix");
    esp_err_t req_err = womo_gps_request_fix();
    if (req_err != ESP_OK) {
        ESP_LOGE(TAG, "GNSS request failed: %s", esp_err_to_name(req_err));
#if !WALTER_GPS_PAUSE_LTE
        if (quiet_applied) {
            esp_err_t qoff = lte_runtime_set_quiet(false);
            if (qoff != ESP_OK) {
                ESP_LOGW(TAG, "Failed to leave LTE quiet mode after GNSS request error");
            }
        }
        (void)modem.setOpState(WALTER_MODEM_OPSTATE_FULL);
#endif
        modem_manager_set_lte_enabled(true, lte_wait);
        return req_err;
    }

    // 4) Auf neuen Fix warten
    TickType_t start = xTaskGetTickCount();
    // Fallback-Wiederaktivierung nur relevant, wenn LTE tatsächlich pausiert wird.
#if WALTER_GPS_PAUSE_LTE
    const TickType_t reenable_after = pdMS_TO_TICKS(30000); // Fallback: LTE nach 30s wieder anschalten
    bool lte_reenabled_during_wait = false;
#endif
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        womo_gps_data_t fix = {};
        if (womo_gps_get_last_fix(&fix) == ESP_OK) {
            if (!have_prev || fix.timestamp != prev_ts) {
                *out_fix = fix;
                ESP_LOGI(TAG, "GNSS fix received, re-enabling LTE");
                // Ensure GNSS is stopped before re-enabling LTE
                womo_gps_cancel_fix();
#if WALTER_GPS_PAUSE_LTE
                modem_manager_set_lte_enabled(true, lte_wait);
#else
                if (quiet_applied) {
                    esp_err_t qoff = lte_runtime_set_quiet(false);
                    if (qoff != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to leave LTE quiet mode after GNSS fix");
                    }
                }
                (void)modem.setOpState(WALTER_MODEM_OPSTATE_FULL);
#endif
                return ESP_OK;
            }
        }

#if WALTER_GPS_PAUSE_LTE
        if (!lte_reenabled_during_wait && (xTaskGetTickCount() - start) >= reenable_after) {
            ESP_LOGW(TAG, "Re-enabling LTE during GNSS wait to avoid long outage");
            modem_manager_set_lte_enabled(true, lte_wait);
            lte_reenabled_during_wait = true;
        }
#endif

        vTaskDelay(fix_poll_delay);
    }

    ESP_LOGW(TAG, "GNSS fix timeout after %lu ms", (unsigned long)(timeout_ticks * portTICK_PERIOD_MS));
    // Ensure GNSS is stopped before re-enabling LTE
    womo_gps_cancel_fix();
#if WALTER_GPS_PAUSE_LTE
    modem_manager_set_lte_enabled(true, lte_wait);
#else
    if (quiet_applied) {
        esp_err_t qoff = lte_runtime_set_quiet(false);
        if (qoff != ESP_OK) {
            ESP_LOGW(TAG, "Failed to leave LTE quiet mode after GNSS timeout");
        }
    }
    (void)modem.setOpState(WALTER_MODEM_OPSTATE_FULL);
#endif
    return ESP_ERR_TIMEOUT;
}
