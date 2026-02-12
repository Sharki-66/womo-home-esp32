#include "time/lte_time.h"

#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "iot_usbh_modem.h"
#include "modem_at_parser.h"
#include "time/time_sync.h"
#include "modem_config.h"

static const char *TAG = "lte_time";
static TaskHandle_t s_lte_time_task = NULL;
static lte_snapshot_t s_lte_snapshot = {0};
static portMUX_TYPE s_lte_lock = portMUX_INITIALIZER_UNLOCKED;

// LTE-Zeit alle 10 Minuten abfragen (GPS läuft alle 8s, hat Vorrang)
#define LTE_TIME_POLL_INTERVAL_MS 30000

static uint8_t csq_to_percent(int rssi)
{
    if (rssi < 0 || rssi > 31) {
        return 0;
    }
    // rssi 0..31 → 0..100 linear
    int pct = (rssi * 100) / 31;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

static void normalize_operator_name(const char *raw, char *out, size_t len)
{
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (!raw) {
        return;
    }

    const char *start_quote = strchr(raw, '"');
    if (start_quote) {
        start_quote++; // hinter das erste Anführungszeichen
        const char *end_quote = strchr(start_quote, '"');
        size_t n = end_quote ? (size_t)(end_quote - start_quote) : strlen(start_quote);
        if (n >= len) n = len - 1;
        memcpy(out, start_quote, n);
        out[n] = '\0';
    } else {
        // Fallback: +COPS: Präfix und Leerzeichen/Kommas entfernen
        while (*raw && isspace((unsigned char)*raw)) {
            raw++;
        }
        if (strncmp(raw, "+COPS:", 6) == 0) {
            raw += 6;
            while (*raw && (*raw == ' ' || *raw == ',')) {
                raw++;
            }
        }

        strlcpy(out, raw, len);
        // Trailing Delimiter entfernen
        size_t L = strlen(out);
        while (L > 0 && (out[L - 1] == ' ' || out[L - 1] == ',')) {
            out[--L] = '\0';
        }
    }

    size_t L = strlen(out);

    // Numerische MCC/MNC Codes auf bekannte Provider mappen
    if (L >= 5 && L <= 6) {
        bool digits_only = true;
        for (size_t i = 0; i < L; ++i) {
            if (!isdigit((unsigned char)out[i])) {
                digits_only = false;
                break;
            }
        }
        if (digits_only) {
            const struct { const char *code; const char *name; } map[] = {
                {"26201", "Telekom"},
                {"26202", "Vodafone"},
                {"26203", "o2"},
                {"26207", "o2"},
            };
            for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); ++i) {
                if (strcmp(out, map[i].code) == 0) {
                    strlcpy(out, map[i].name, len);
                    break;
                }
            }
        }
    }
}

/**
 * @brief Parse AT+CCLK response to Unix timestamp
 * 
 * Format: "+CCLK: \"yy/MM/dd,HH:mm:ss±zz\""
 * Example: "+CCLK: \"26/01/29,14:23:45+04\""
 * 
 * @param response AT command response line
 * @param out_time Output Unix timestamp (UTC)
 * @return ESP_OK on success
 */
static esp_err_t parse_cclk_response(const char *response, time_t *out_time)
{
    if (response == NULL || out_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Suche nach "+CCLK: \""
    const char *start = strstr(response, "+CCLK: \"");
    if (start == NULL) {
        return ESP_FAIL;
    }
    start += 8; // Skip "+CCLK: \""

    // Parse: yy/MM/dd,HH:mm:ss±zz
    struct tm tm_time = {0};
    int year, month, day, hour, min, sec, tz_quarters;
    char tz_sign;

    int parsed = sscanf(start, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d",
                        &year, &month, &day, &hour, &min, &sec, &tz_sign, &tz_quarters);
    
    if (parsed < 6) {
        ESP_LOGW(TAG, "CCLK Parse-Fehler: '%s'", response);
        return ESP_FAIL;
    }

    // Jahr 2000+ konvertieren
    tm_time.tm_year = (year >= 70 ? year : year + 100); // 1970-2069
    tm_time.tm_mon = month - 1;
    tm_time.tm_mday = day;
    tm_time.tm_hour = hour;
    tm_time.tm_min = min;
    tm_time.tm_sec = sec;

    // Lokale Zeit zu UTC konvertieren (Zeitzone korrigieren)
    time_t local_time = mktime(&tm_time);
    
    // Zeitzone: ±zz ist in Vierteln einer Stunde (z.B. +04 = +1h)
    int tz_offset_sec = 0;
    if (parsed >= 8) {
        tz_offset_sec = (tz_quarters * 15 * 60);
        if (tz_sign == '-') {
            tz_offset_sec = -tz_offset_sec;
        }
    }

    // UTC = Lokalzeit - Zeitzone
    *out_time = local_time - tz_offset_sec;

    ESP_LOGD(TAG, "CCLK parsed: %04d-%02d-%02d %02d:%02d:%02d (TZ=%c%02d) → UTC=%ld",
             tm_time.tm_year + 1900, month, day, hour, min, sec,
             tz_sign, tz_quarters, (long)*out_time);

    return ESP_OK;
}

typedef struct {
    time_t utc_time;
    bool valid;
} cclk_result_t;

/**
 * @brief Handle response from AT+CCLK?
 */
static bool handle_cclk_response(at_handle_t at_handle, const char *line)
{
    cclk_result_t *result = (cclk_result_t *)modem_at_get_handle_line_ctx(at_handle);
    
    if (strstr(line, "ERROR")) {
        result->valid = false;
        return true;
    }

    if (strstr(line, "+CCLK:")) {
        // Parse CCLK response
        if (parse_cclk_response(line, &result->utc_time) == ESP_OK) {
            result->valid = true;
        } else {
            result->valid = false;
        }
    }

    if (strstr(line, "OK")) {
        return true;
    }
    
    return false;
}

/**
 * @brief Frage LTE-Netzwerkzeit ab (AT+CCLK?)
 * 
 * @param at_handle AT Parser Handle
 * @param out_time Output Unix Timestamp (UTC)
 * @return ESP_OK bei Erfolg
 */
static esp_err_t query_lte_time(at_handle_t at_handle, time_t *out_time)
{
    if (at_handle == NULL || out_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cclk_result_t result = {
        .utc_time = 0,
        .valid = false
    };

    esp_err_t err = modem_at_send_command(at_handle, "AT+CCLK?", 1000, handle_cclk_response, &result);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AT+CCLK? Kommando fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    if (!result.valid) {
        ESP_LOGW(TAG, "AT+CCLK? Response ungültig");
        return ESP_FAIL;
    }

    *out_time = result.utc_time;
    return ESP_OK;
}

static void lte_time_poll_task(void *arg)
{
    ESP_LOGI(TAG, "LTE-Zeit-Task gestartet (Poll-Intervall: %d min)", LTE_TIME_POLL_INTERVAL_MS / 60000);

    // Warte 30s nach Start, damit Modem Zeit hat sich zu registrieren
    vTaskDelay(pdMS_TO_TICKS(30000));

    while (1) {
        at_handle_t at = usbh_modem_get_atparser();
        if (at == NULL) {
            ESP_LOGW(TAG, "AT-Parser nicht verfügbar, warte...");
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        time_t lte_time = 0;
        esp_err_t err = query_lte_time(at, &lte_time);
        
        if (err == ESP_OK) {
            // Zeit via time_sync aktualisieren (prüft automatisch GPS-Priorität)
            esp_err_t sync_err = time_sync_update_from_lte(lte_time);
            if (sync_err == ESP_OK) {
                struct tm tm_time = {0};
                gmtime_r(&lte_time, &tm_time);
                ESP_LOGI(TAG, "LTE-Zeit aktualisiert: %04d-%02d-%02d %02d:%02d:%02d UTC",
                         tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                         tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
            } else {
                ESP_LOGD(TAG, "LTE-Zeit nicht verwendet (GPS hat Vorrang oder Zeit ungültig)");
            }
        } else {
            ESP_LOGW(TAG, "LTE-Zeit-Abfrage fehlgeschlagen: %s", esp_err_to_name(err));
        }

        // LTE Status (Signal + Operator)
        esp_modem_at_csq_t csq = {0};
        esp_err_t csq_err = at_cmd_get_signal_quality(at, &csq);
        char operator_raw[64] = {0};
        char operator_name[sizeof(s_lte_snapshot.operator_name)] = {0};
        at_cmd_get_operator_name(at, operator_raw, sizeof(operator_raw));
        normalize_operator_name(operator_raw, operator_name, sizeof(operator_name));

        bool csq_valid = (csq_err == ESP_OK && csq.rssi != 99);
        bool op_present = (operator_name[0] != '\0');
        int csq_dbm = csq_valid ? (-113 + 2 * csq.rssi) : 0;
        uint8_t csq_pct = csq_valid ? csq_to_percent(csq.rssi) : 0;

        taskENTER_CRITICAL(&s_lte_lock);
        lte_snapshot_t prev = s_lte_snapshot;

        s_lte_snapshot.ts_us = esp_timer_get_time();
        s_lte_snapshot.valid = (csq_err == ESP_OK || op_present || prev.valid);
        // Registriert, sobald ein Operator oder irgendeine CSQ-Antwort kam (auch rssi=99), oder vorher schon registriert war
        s_lte_snapshot.registered = (op_present || csq_err == ESP_OK || csq_valid || prev.registered);

        if (csq_valid) {
            s_lte_snapshot.rsrp_dbm = (float)csq_dbm;
            s_lte_snapshot.signal_percent = csq_pct;
        } else if (prev.valid) {
            s_lte_snapshot.rsrp_dbm = prev.rsrp_dbm;
            s_lte_snapshot.signal_percent = prev.signal_percent;
        } else {
            s_lte_snapshot.rsrp_dbm = -140.0f;
            s_lte_snapshot.signal_percent = 0;
        }

        if (op_present) {
            strlcpy(s_lte_snapshot.operator_name, operator_name, sizeof(s_lte_snapshot.operator_name));
        } else if (prev.operator_name[0] != '\0') {
            strlcpy(s_lte_snapshot.operator_name, prev.operator_name, sizeof(s_lte_snapshot.operator_name));
        } else {
            s_lte_snapshot.operator_name[0] = '\0';
        }

        taskEXIT_CRITICAL(&s_lte_lock);

        ESP_LOGI(TAG, "LTE snap: csq_err=%s rssi=%d csq_valid=%d pct=%u op_raw='%s' op='%s' reg=%d",
             esp_err_to_name(csq_err), csq.rssi, csq_valid, csq_pct, operator_raw, s_lte_snapshot.operator_name, s_lte_snapshot.registered);

        // RTC-Batteriestatus alle 10 Minuten prüfen
        bool battery_low = false;
        esp_err_t bat_err = time_sync_check_rtc_battery(&battery_low);
        if (bat_err == ESP_OK && battery_low) {
            ESP_LOGW(TAG, "⚠️  RTC-Batterie schwach!");
        }

        vTaskDelay(pdMS_TO_TICKS(LTE_TIME_POLL_INTERVAL_MS));
    }
}

esp_err_t lte_time_task_start(void)
{
    if (s_lte_time_task != NULL) {
        ESP_LOGW(TAG, "LTE-Zeit-Task bereits gestartet");
        return ESP_OK;
    }

    BaseType_t created = xTaskCreatePinnedToCore(
        lte_time_poll_task,
        "lte_time",
        4096,
        NULL,
        4,  // Priority 4 (gleich wie GNSS)
        &s_lte_time_task,
        0   // Core 0
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "LTE-Zeit-Task konnte nicht erzeugt werden");
        s_lte_time_task = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool lte_status_get_snapshot(lte_snapshot_t *out)
{
    if (!out) {
        return false;
    }
    taskENTER_CRITICAL(&s_lte_lock);
    *out = s_lte_snapshot;
    taskEXIT_CRITICAL(&s_lte_lock);
    return out->valid;
}
