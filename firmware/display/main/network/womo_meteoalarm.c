/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_meteoalarm.c
 * @brief Meteoalarm Unwetterwarnungen (Stufen 2+3) – GPS-basiert
 *
 * API-Endpunkt:
 *   GET https://feeds.meteoalarm.org/api/v1/warnings/for-coordinates/{lat}/{lon}
 *
 * Beispiel-Response (vereinfacht):
 * {
 *   "warnings": [
 *     {
 *       "id": "...",
 *       "info": [{
 *         "language": "de",
 *         "event": "Wind",
 *         "headline": "Sturmwarnung ...",
 *         "severity": "Severe",
 *         "expires": "2025-07-10T18:00:00+02:00"
 *       }]
 *     }
 *   ]
 * }
 *
 * Fallback: Falls kein Eintrag auf "de" matcht, wird der erste
 * verfügbare "info"-Eintrag genutzt.
 */

#include "womo_meteoalarm.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "harica_root_pem.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "womo_http_mutex.h"
#include "cJSON.h"

static const char *TAG = "meteoalarm";

/* ── Konfiguration ───────────────────────────────────────────── */
#define MA_FETCH_INTERVAL_S   (30 * 60)   // 30 Minuten Normalintervall
#define MA_RETRY_INTERVAL_S   (5 * 60)    // 5 Minuten bei Fehler
#define MA_START_DELAY_S      (3 * 60)    // Erststart deutlich nach Boot/TLS-Peaks
#define MA_LOCATION_DELTA_DEG (0.045)     // ~5 km – löst Re-Fetch aus
#define MA_HTTP_TIMEOUT_MS    10000
#define MA_BUF_SIZE           8192        // JSON-Buffer
#define MA_URL_LEN            128
#define MA_TASK_STACK         6144
#define MA_TASK_PRIO          3
#define MA_MIN_INTERNAL_FREE      16384U
#define MA_MIN_INTERNAL_LARGEST   13312U   // RSA-3072 (GEANT cert) braucht ~13 KB contiguous internal RAM

/* ── Interner Kontext ────────────────────────────────────────── */
typedef struct {
    char     buf[MA_BUF_SIZE + 1];
    int      len;
    esp_err_t last_err;
} ma_response_t;

static struct {
    TaskHandle_t         task_handle;
    womo_meteoalarm_cb_t callback;
    void                *user_data;
    volatile bool        stop_requested;
    /* GPS (geschützt durch spinlock) */
    portMUX_TYPE         gps_lock;
    double               gps_lat;
    double               gps_lon;
    bool                 gps_valid;
    /* Letzte Fetch-Position */
    double               fetch_lat;
    double               fetch_lon;
    bool                 fetch_done_once;
    /* Frühester erlaubter Fetch-Zeitpunkt (Ticks seit Boot) */
    TickType_t           earliest_fetch_tick;
} s_ctx = {
    .gps_lock = portMUX_INITIALIZER_UNLOCKED,
};

/* ── Hilfsfunktionen ─────────────────────────────────────────── */

static void ma_log_heap(const char *stage)
{
    /* Nur internen SRAM – PSRAM-Traversierung (MALLOC_CAP_8BIT) blockiert QSPI-Bus. */
    size_t free_internal    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    ESP_LOGW(TAG,
             "Heap %s: internal_free=%u internal_largest=%u",
             stage ? stage : "?",
             (unsigned)free_internal,
             (unsigned)largest_internal);
}

static bool ma_has_enough_internal_heap(void)
{
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    return free_internal >= MA_MIN_INTERNAL_FREE &&
           largest_internal >= MA_MIN_INTERNAL_LARGEST;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    ma_response_t *resp = (ma_response_t *)evt->user_data;
    if (!resp) return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0) {
            int remaining = MA_BUF_SIZE - resp->len;
            int to_copy   = (evt->data_len < remaining) ? evt->data_len : remaining;
            if (to_copy > 0) {
                memcpy(resp->buf + resp->len, evt->data, to_copy);
                resp->len += to_copy;
            }
            if (evt->data_len > remaining) {
                ESP_LOGW(TAG, "Meteoalarm response truncated (%d bytes lost)",
                         evt->data_len - remaining);
                resp->last_err = ESP_ERR_NO_MEM;
            }
        }
        break;
    case HTTP_EVENT_ERROR:
        resp->last_err = ESP_FAIL;
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Parst RFC 3339 / ISO 8601 Timestamp "2025-07-10T18:00:00+02:00" → time_t  */
static time_t parse_iso8601(const char *str)
{
    if (!str || !*str) return 0;

    struct tm tm = {0};
    int tz_h = 0, tz_m = 0;
    char tz_sign = '+';

    /* Versuch mit Offset */
    int matched = sscanf(str, "%4d-%2d-%2dT%2d:%2d:%2d%c%2d:%2d",
                         &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                         &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
                         &tz_sign, &tz_h, &tz_m);
    if (matched < 6) return 0;

    tm.tm_year -= 1900;
    tm.tm_mon  -= 1;
    tm.tm_isdst = -1;

    /* Naive UTC-Berechnung via mktime (libc) – mktime erwartet Lokalzeit.
     * Wir rechnen manuell in UTC um: */
    time_t t = mktime(&tm);   // ergibt Lokalzeit → ggf. Offset berücksichtigen
    if (t == (time_t)-1) return 0;

    /* TZ-Offset herausrechnen (sign ist '+' oder '-') */
    int tz_offset_s = (tz_h * 3600 + tz_m * 60);
    if (tz_sign == '+') {
        t -= tz_offset_s;  // Lokalzeit → UTC
    } else {
        t += tz_offset_s;
    }

    /* Außerdem lokale TZ-Differenz von mktime rückgängig machen */
    /* (mktime fügt die Systemzeitzone hinzu; wir addieren sie zurück) */
    time_t now_utc  = time(NULL);
    struct tm gm_tm = {0};
    gmtime_r(&now_utc, &gm_tm);
    gm_tm.tm_isdst = -1;
    time_t utc_check = mktime(&gm_tm);
    int sys_tz_offset = (int)(now_utc - utc_check); // Sekunden, die mktime zur Zeit hinzufügt
    t -= sys_tz_offset;

    return t;
}

/* Expires-Timestamp lesbar formatieren */
static void format_expires(time_t ts, char *out, size_t len)
{
    if (ts == 0) {
        snprintf(out, len, "--");
        return;
    }
    struct tm tm;
    localtime_r(&ts, &tm);
    strftime(out, len, "%H:%M %d.%m.", &tm);
}

/* Severity-String → Enum */
static womo_warn_severity_t parse_severity(const char *str)
{
    if (!str) return WOMO_WARN_SEV_UNKNOWN;
    if (strcasecmp(str, "Extreme")  == 0) return WOMO_WARN_SEV_EXTREME;
    if (strcasecmp(str, "Severe")   == 0) return WOMO_WARN_SEV_SEVERE;
    if (strcasecmp(str, "Moderate") == 0) return WOMO_WARN_SEV_MODERATE;
    if (strcasecmp(str, "Minor")    == 0) return WOMO_WARN_SEV_MINOR;
    return WOMO_WARN_SEV_UNKNOWN;
}

/* ── Fetch + Parse ───────────────────────────────────────────── */

static esp_err_t ma_fetch(double lat, double lon, womo_meteoalarm_result_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    ma_response_t *resp = calloc(1, sizeof(ma_response_t));
    if (!resp) return ESP_ERR_NO_MEM;

    char url[MA_URL_LEN];
    snprintf(url, sizeof(url),
             "https://feeds.meteoalarm.org/api/v1/warnings/for-coordinates/%.5f/%.5f",
             lat, lon);
    ESP_LOGI(TAG, "Fetch: %s", url);
    ma_log_heap("before_http_init");

    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = HTTP_METHOD_GET,
        .timeout_ms    = MA_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data     = resp,
        .cert_pem      = harica_root_pem,  // GEANT TLS RSA 1 direkt; HARICA fehlt im ESP-IDF-Bundle
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ma_log_heap("http_init_failed");
        free(resp);
        return ESP_ERR_NO_MEM;
    }

    resp->len      = 0;
    resp->last_err = ESP_OK;

    /* TLS-Mutex: nur eine HTTPS-Session gleichzeitig (Heap-Limit).
     * cleanup() MUSS im Mutex-Scope liegen, damit TLS-RAM frei ist
     * bevor der nächste Client den Mutex bekommt. */
    if (womo_http_mutex_acquire() != ESP_OK) {
        esp_http_client_cleanup(client);
        free(resp);
        return ESP_ERR_TIMEOUT;
    }
    ma_log_heap("before_http_perform");
    esp_err_t err = esp_http_client_perform(client);
    int status    = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    womo_http_mutex_release();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        ma_log_heap("http_perform_failed");
        free(resp);
        return err;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP status %d", status);
        free(resp);
        return ESP_FAIL;
    }
    if (resp->len == 0) {
        ESP_LOGW(TAG, "Empty response");
        free(resp);
        return ESP_FAIL;
    }

    resp->buf[resp->len] = '\0';

    /* ── JSON-Parsing ─────────────────────────────────── */
    cJSON *root = cJSON_Parse(resp->buf);
    free(resp);

    if (!root) {
        ESP_LOGE(TAG, "cJSON parse error");
        return ESP_FAIL;
    }

    out->valid = true;

    /* Meteoalarm-Region des Standorts: top-level 'area'-Objekt (falls vorhanden) */
    cJSON *root_area = cJSON_GetObjectItem(root, "area");
    if (cJSON_IsObject(root_area)) {
        cJSON *ad = cJSON_GetObjectItem(root_area, "areaDesc");
        if (cJSON_IsString(ad)) {
            strlcpy(out->region, ad->valuestring, sizeof(out->region));
        }
    } else if (cJSON_IsArray(root_area)) {
        cJSON *a0 = cJSON_GetArrayItem(root_area, 0);
        cJSON *ad = a0 ? cJSON_GetObjectItem(a0, "areaDesc") : NULL;
        if (cJSON_IsString(ad)) {
            strlcpy(out->region, ad->valuestring, sizeof(out->region));
        }
    }

    cJSON *warnings_arr = cJSON_GetObjectItem(root, "warnings");
    if (!cJSON_IsArray(warnings_arr)) {
        /* API liefert leeres Objekt oder anderes Format → keine Warnungen */
        ESP_LOGI(TAG, "Keine 'warnings'-Array in Antwort (keine Warnungen aktiv?)");
        cJSON_Delete(root);
        return ESP_OK;
    }

    cJSON *warn_obj = NULL;
    cJSON_ArrayForEach(warn_obj, warnings_arr) {

        /* info-Array suchen */
        cJSON *info_arr = cJSON_GetObjectItem(warn_obj, "info");
        if (!cJSON_IsArray(info_arr)) continue;

        /* Deutschen Eintrag bevorzugen, sonst ersten nehmen */
        cJSON *info_obj = NULL;
        cJSON *info_de  = NULL;
        cJSON *info_any = NULL;
        cJSON *info_it  = NULL;
        cJSON_ArrayForEach(info_it, info_arr) {
            cJSON *lang = cJSON_GetObjectItem(info_it, "language");
            if (cJSON_IsString(lang)) {
                if (strncasecmp(lang->valuestring, "de", 2) == 0) {
                    info_de = info_it;
                }
            }
            if (!info_any) info_any = info_it;
        }
        info_obj = info_de ? info_de : info_any;
        if (!info_obj) continue;

        /* Severity lesen */
        const char *sev_str = NULL;
        cJSON *sev_node = cJSON_GetObjectItem(info_obj, "severity");
        if (cJSON_IsString(sev_node)) sev_str = sev_node->valuestring;
        womo_warn_severity_t sev = parse_severity(sev_str);

        /* Stufe 1 (Minor) und Unbekannt ignorieren */
        if (sev < WOMO_WARN_SEV_MODERATE) continue;

        if (out->count >= WOMO_METEOALARM_MAX_WARNINGS) break;

        womo_meteoalarm_warning_t *w = &out->warnings[out->count];
        w->severity = sev;

        /* Event */
        cJSON *event_node = cJSON_GetObjectItem(info_obj, "event");
        if (cJSON_IsString(event_node)) {
            strlcpy(w->event, event_node->valuestring, sizeof(w->event));
        }

        /* Headline */
        cJSON *hl_node = cJSON_GetObjectItem(info_obj, "headline");
        if (cJSON_IsString(hl_node)) {
            strlcpy(w->headline, hl_node->valuestring, sizeof(w->headline));
        } else if (w->event[0]) {
            strlcpy(w->headline, w->event, sizeof(w->headline));
        }

        /* Region aus area-Array (top-level im warn_obj, nicht in info) */
        cJSON *area_arr = cJSON_GetObjectItem(warn_obj, "area");
        if (cJSON_IsArray(area_arr)) {
            cJSON *area0 = cJSON_GetArrayItem(area_arr, 0);
            cJSON *area_desc = area0 ? cJSON_GetObjectItem(area0, "areaDesc") : NULL;
            if (cJSON_IsString(area_desc)) {
                strlcpy(w->region, area_desc->valuestring, sizeof(w->region));
            }
        }
        /* Fallback: areaDesc direkt im info-Objekt (manche CAP-Varianten) */
        if (w->region[0] == '\0') {
            cJSON *ad = cJSON_GetObjectItem(info_obj, "areaDesc");
            if (cJSON_IsString(ad)) {
                strlcpy(w->region, ad->valuestring, sizeof(w->region));
            }
        }

        /* Expires */
        cJSON *exp_node = cJSON_GetObjectItem(info_obj, "expires");
        if (cJSON_IsString(exp_node)) {
            w->expires_ts = parse_iso8601(exp_node->valuestring);
            format_expires(w->expires_ts, w->expires, sizeof(w->expires));
        }

        if (sev > out->max_severity) out->max_severity = sev;
        out->count++;
    }

    /* Fallback 1: Region aus erster geparster Warnung */
    if (out->region[0] == '\0' && out->count > 0 && out->warnings[0].region[0]) {
        strlcpy(out->region, out->warnings[0].region, sizeof(out->region));
    }

    /* Fallback 2: Region aus BELIEBIGER Warnung im Array (auch Minor/Ignored).
     * Wichtig: warnings_arr ist noch Teil des root-Baums → MUSS vor
     * cJSON_Delete(root) laufen! */
    if (out->region[0] == '\0' && cJSON_IsArray(warnings_arr)) {
        cJSON *w0 = NULL;
        cJSON_ArrayForEach(w0, warnings_arr) {
            cJSON *a_arr = cJSON_GetObjectItem(w0, "area");
            cJSON *a0 = cJSON_IsArray(a_arr) ? cJSON_GetArrayItem(a_arr, 0) : NULL;
            if (!a0) a0 = cJSON_IsObject(a_arr) ? a_arr : NULL;
            cJSON *ad = a0 ? cJSON_GetObjectItem(a0, "areaDesc") : NULL;
            if (cJSON_IsString(ad) && ad->valuestring[0]) {
                strlcpy(out->region, ad->valuestring, sizeof(out->region));
                ESP_LOGI(TAG, "Region aus Fallback-Warnung: '%s'", out->region);
                break;
            }
            /* Auch in info[*].areaDesc suchen */
            cJSON *i_arr = cJSON_GetObjectItem(w0, "info");
            cJSON *i0 = cJSON_IsArray(i_arr) ? cJSON_GetArrayItem(i_arr, 0) : NULL;
            cJSON *i_ad = i0 ? cJSON_GetObjectItem(i0, "areaDesc") : NULL;
            if (cJSON_IsString(i_ad) && i_ad->valuestring[0]) {
                strlcpy(out->region, i_ad->valuestring, sizeof(out->region));
                ESP_LOGI(TAG, "Region aus info.areaDesc Fallback: '%s'", out->region);
                break;
            }
        }
    }

    cJSON_Delete(root);  /* Erst hier freigeben, nachdem alle Fallbacks durch sind */

    ESP_LOGI(TAG, "Warnungen: %u (max. Stufe %u) Region: '%s'",
             out->count, out->max_severity, out->region);
    return ESP_OK;
}

/* ── Hintergrundtask ─────────────────────────────────────────── */

static void ma_task(void *arg)
{
    (void)arg;

    /* Beim ersten Start 30 s warten, damit NTP/TLS-Stack sicher bereit ist.
     * Wichtig: Wir drängen GPS-Notifys ab, indem wir den frühesten
     * erlaubten Startzeitpunkt absolut prüfen – unabhängig davon wie oft
     * der Task durch xTaskNotifyGive geweckt wird. */
    while (!s_ctx.stop_requested) {
        TickType_t now = xTaskGetTickCount();
        if (now >= s_ctx.earliest_fetch_tick) break;
        TickType_t remaining = s_ctx.earliest_fetch_tick - now;
        ulTaskNotifyTake(pdTRUE, remaining);  // GPS-Notify konsumieren ohne zu fetchen
    }

    while (!s_ctx.stop_requested) {

        /* Systemzeit plausibel prüfen (Jahr >= 2024) */
        time_t now = time(NULL);
        struct tm tm_now = {0};
        gmtime_r(&now, &tm_now);
        if (tm_now.tm_year < (2024 - 1900)) {
            ESP_LOGW(TAG, "Systemzeit noch nicht sync – warte 10 s (Jahr=%d)",
                     tm_now.tm_year + 1900);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
            continue;
        }

        /* Position aus shared state kopieren */
        double lat = 0.0, lon = 0.0;
        bool gps_ok = false;
        taskENTER_CRITICAL(&s_ctx.gps_lock);
        lat    = s_ctx.gps_lat;
        lon    = s_ctx.gps_lon;
        gps_ok = s_ctx.gps_valid;
        taskEXIT_CRITICAL(&s_ctx.gps_lock);

        if (!gps_ok) {
            ESP_LOGD(TAG, "Keine GPS-Position – warte 30 s");
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
            continue;
        }

        if (!ma_has_enough_internal_heap()) {
            ESP_LOGW(TAG,
                     "Zu wenig interner Heap fuer Meteoalarm-TLS, verschiebe Fetch");
            ma_log_heap("heap_guard_skip");
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(120000));
            continue;
        }

        womo_meteoalarm_result_t result = {0};
        esp_err_t err = ma_fetch(lat, lon, &result);

        /* Fetch-Position aktualisieren */
        if (err == ESP_OK) {
            taskENTER_CRITICAL(&s_ctx.gps_lock);
            s_ctx.fetch_lat        = lat;
            s_ctx.fetch_lon        = lon;
            s_ctx.fetch_done_once  = true;
            taskEXIT_CRITICAL(&s_ctx.gps_lock);
        }

        if (s_ctx.callback) {
            s_ctx.callback(&result, s_ctx.user_data);
        }

        if (s_ctx.stop_requested) break;

        uint32_t wait_ms;
        if (err == ESP_OK) {
            wait_ms = (uint32_t)(MA_FETCH_INTERVAL_S * 1000U);   // 30 min
        } else if (err == ESP_ERR_HTTP_CONNECT) {
            /* TLS-Fehler (Zertifikat, Netz) → kurz warten und nochmal */
            ESP_LOGW(TAG, "TLS-Fehler – Retry in 2 min");
            wait_ms = 2 * 60 * 1000U;
        } else {
            wait_ms = (uint32_t)(MA_RETRY_INTERVAL_S * 1000U);   // 5 min
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }

    s_ctx.task_handle = NULL;
    vTaskDelete(NULL);
}

/* ── Öffentliche API ─────────────────────────────────────────── */

esp_err_t womo_meteoalarm_start(womo_meteoalarm_cb_t callback, void *user_data)
{
    if (s_ctx.task_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.callback           = callback;
    s_ctx.user_data          = user_data;
    s_ctx.stop_requested     = false;
    /* Meteoalarm deutlich spaeter starten als Wetter/Geocode/Boot-Nachlauf,
     * damit der interne Heap sich beruhigen kann bevor ein weiterer TLS-Client
     * anlaeuft. */
    s_ctx.earliest_fetch_tick = xTaskGetTickCount() + pdMS_TO_TICKS(MA_START_DELAY_S * 1000U);

    BaseType_t created = xTaskCreateWithCaps(
        ma_task,
        "meteoalarm",
        MA_TASK_STACK,
        NULL,
        MA_TASK_PRIO,
        &s_ctx.task_handle,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (created != pdPASS) {
        s_ctx.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Meteoalarm-Task gestartet (Intervall %d min)", MA_FETCH_INTERVAL_S / 60);
    return ESP_OK;
}

esp_err_t womo_meteoalarm_stop(void)
{
    if (!s_ctx.task_handle) return ESP_ERR_INVALID_STATE;

    s_ctx.stop_requested = true;
    xTaskNotifyGive(s_ctx.task_handle);

    while (s_ctx.task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_ctx.callback  = NULL;
    s_ctx.user_data = NULL;
    ESP_LOGI(TAG, "Meteoalarm-Task gestoppt");
    return ESP_OK;
}

bool womo_meteoalarm_is_running(void)
{
    return s_ctx.task_handle != NULL;
}

void womo_meteoalarm_set_location(double lat, double lon)
{
    taskENTER_CRITICAL(&s_ctx.gps_lock);
    bool was_valid   = s_ctx.gps_valid;
    double old_lat   = s_ctx.gps_lat;
    double old_lon   = s_ctx.gps_lon;
    s_ctx.gps_lat    = lat;
    s_ctx.gps_lon    = lon;
    s_ctx.gps_valid  = true;
    taskEXIT_CRITICAL(&s_ctx.gps_lock);

    if (!s_ctx.task_handle) return;

    /* Bei erstem Fix oder signifikanter Bewegung sofort neu fetchen */
    bool first_fix  = !was_valid;
    double dlat     = fabs(lat - old_lat);
    double dlon     = fabs(lon - old_lon);
    bool moved      = (dlat > MA_LOCATION_DELTA_DEG || dlon > MA_LOCATION_DELTA_DEG);

    if (first_fix || (moved && s_ctx.fetch_done_once)) {
        ESP_LOGI(TAG, "GPS%s → sofortiger Meteoalarm-Fetch (%.5f, %.5f)",
                 first_fix ? "-Fix" : "-Bewegung", lat, lon);
        xTaskNotifyGive(s_ctx.task_handle);
    }
}

void womo_meteoalarm_trigger_now(void)
{
    if (s_ctx.task_handle) {
        xTaskNotifyGive(s_ctx.task_handle);
    }
}

const char *womo_meteoalarm_severity_label(womo_warn_severity_t sev)
{
    switch (sev) {
    case WOMO_WARN_SEV_MINOR:    return "Stufe 1";
    case WOMO_WARN_SEV_MODERATE: return "Stufe 2";
    case WOMO_WARN_SEV_SEVERE:   return "Stufe 2";
    case WOMO_WARN_SEV_EXTREME:  return "Stufe 3";
    default:                     return "Unbekannt";
    }
}
