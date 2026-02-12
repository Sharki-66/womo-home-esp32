#include "time/gnss_time.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/time.h>
#include "iot_usbh_modem.h"
#include "at_3gpp_ts_27_007.h"
#include "time/time_sync.h"

static const char *TAG = "gnss_task";
static TaskHandle_t s_gnss_task = NULL;
static gnss_snapshot_t s_gnss_snapshot = {0};
static portMUX_TYPE s_gnss_lock = portMUX_INITIALIZER_UNLOCKED;

static void gnss_poll_task(void *arg)
{
    at_handle_t at = usbh_modem_get_atparser();
    if (at == NULL) {
        ESP_LOGW(TAG, "GNSS: kein AT-Parser verfügbar");
        vTaskDelete(NULL);
        s_gnss_task = NULL;
        return;
    }

    bool gnss_powered = (at_cmd_cgnss_power(at, true) == ESP_OK);
    if (gnss_powered) {
        ESP_LOGI(TAG, "GNSS eingeschaltet (AT+CGNSSPWR=1)");
        esp_err_t mod_res = at_cmd_cgnss_mode(at, 3);
        ESP_LOGI(TAG, "GNSS Modus setzen: %s", (mod_res == ESP_OK) ? "OK" : "FEHLER");
    } else {
        ESP_LOGW(TAG, "GNSS (CGNSSPWR) konnte nicht eingeschaltet werden");
    }

    int repower_tries = 0;

    while (1) {
        esp_modem_at_gnss_info_t info = {0};
        esp_err_t err = at_cmd_cgnss_info(at, &info);
        if (err != ESP_OK || info.latitude == 0.0 || info.longitude == 0.0) {
            err = at_cmd_cgps_info(at, &info);
        }
        if (err == ESP_OK) {
            if (info.run_status == 0 && repower_tries < 3) {
                repower_tries++;
                ESP_LOGD(TAG, "GNSS run_status=0 → repower Versuch #%d", repower_tries);
                gnss_powered = (at_cmd_cgnss_power(at, true) == ESP_OK);
                at_cmd_cgnss_mode(at, 3);
            }

            if (info.fix_valid && info.latitude != 0.0 && info.longitude != 0.0) {
                ESP_LOGI(TAG, "GNSS-Fix: lat=%.6f lon=%.6f alt=%.1fm hdop=%.1f sats=%d/%d v=%.1fkm/h kurs=%.1f°",
                         info.latitude, info.longitude, info.altitude_m, info.hdop, info.sats_in_use, info.sats_in_view,
                         info.speed_kph, info.course_deg);

                // Snapshot sichern
                taskENTER_CRITICAL(&s_gnss_lock);
                s_gnss_snapshot.valid = true;
                s_gnss_snapshot.latitude = info.latitude;
                s_gnss_snapshot.longitude = info.longitude;
                s_gnss_snapshot.altitude_m = info.altitude_m;
                s_gnss_snapshot.speed_kph = info.speed_kph;
                s_gnss_snapshot.course_deg = info.course_deg;
                s_gnss_snapshot.hdop = info.hdop;
                s_gnss_snapshot.sats_in_use = info.sats_in_use;
                s_gnss_snapshot.sats_in_view = info.sats_in_view;
                s_gnss_snapshot.ts_us = esp_timer_get_time();
                s_gnss_snapshot.utc_time = info.utc_time;
                taskEXIT_CRITICAL(&s_gnss_lock);
                
                // Zeit via time_sync-Modul aktualisieren (GPS primär)
                if (info.utc_time > 0) {
                    esp_err_t sync_err = time_sync_update_from_gps(info.utc_time);
                    if (sync_err != ESP_OK) {
                        ESP_LOGW(TAG, "GPS-Zeit-Sync fehlgeschlagen: %s", esp_err_to_name(sync_err));
                    }
                }
            } else {
                ESP_LOGI(TAG, "GNSS: run=%d fix=%d sats=%d/%d hdop=%.1f powered=%d mode=CGPS",
                         info.run_status, info.fix_status, info.sats_in_use, info.sats_in_view, info.hdop, gnss_powered);
            }
        } else {
            ESP_LOGW(TAG, "GNSS-Abfrage (%s) fehlgeschlagen: %s", "CGPSINFO", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(8000));
    }
}

void gnss_task_start(void)
{
    if (s_gnss_task != NULL) {
        return;
    }
    BaseType_t created = xTaskCreatePinnedToCore(gnss_poll_task, "gnss", 4096, NULL, 4, &s_gnss_task, 0);
    if (created != pdPASS) {
        ESP_LOGW(TAG, "GNSS-Task konnte nicht erzeugt werden");
        s_gnss_task = NULL;
    }
}

bool gnss_get_snapshot(gnss_snapshot_t *out)
{
    if (!out) {
        return false;
    }
    taskENTER_CRITICAL(&s_gnss_lock);
    *out = s_gnss_snapshot;
    taskEXIT_CRITICAL(&s_gnss_lock);
    return out->valid;
}
