/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * WoMo Router UCI Client – Implementierung
 *
 * Kommuniziert mit dem RUTX11-Router über JSON-RPC 2.0 / ubus
 * (POST http://<ROUTER_IP>/ubus).
 *
 * Session-Management:
 *   - Login liefert ubus_rpc_session Token (300 s Gültigkeit).
 *   - ensure_session() erneuert Token 30 s vor Ablauf.
 *   - Bei 403/Fehler wird automatisch re-login versucht.
 *
 * Thread-Safety: Interner Mutex schützt Session-State und HTTP-Client.
 */

#include "womo_router_uci.h"

#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define TAG "router_uci"

/* ── Konfiguration ────────────────────────────────────────── */

// CONFIG_WOMO_ROUTER_IP ist jetzt in womo_router_uci.h definiert
#ifndef CONFIG_WOMO_ROUTER_USERNAME
#define CONFIG_WOMO_ROUTER_USERNAME "admin"
#endif
#ifndef CONFIG_WOMO_ROUTER_PASSWORD
#define CONFIG_WOMO_ROUTER_PASSWORD ""
#endif

#define ROUTER_URL_MAX       80
#define ROUTER_HTTP_INITIAL  4096      /* Startgröße HTTP-Buffer            */
#define ROUTER_HTTP_MAX      65536     /* Maximal 64 KB (Scan-Ergebnisse)   */
#define ROUTER_LOGIN_MAX     2048      /* Login: nur Session-ID nötig       */
#define SESSION_MARGIN_S     30        /* Refresh 30 s vor Ablauf           */
#define SESSION_TIMEOUT_S    300       /* Server-Default 300 s              */
#define HTTP_TIMEOUT_MS      15000
#define UBUS_NULL_SESSION    "00000000000000000000000000000000"

/* ── Interner State ───────────────────────────────────────── */

typedef struct {
    char   *buffer;           /* Dynamisch allokiert (PSRAM)      */
    size_t  capacity;         /* Aktuelle Buffer-Größe             */
    size_t  length;           /* Geschriebene Bytes                */
    bool    truncate_ok;      /* true → bei Buffer-Limit still verwerfen */
    bool    truncated;        /* true → Daten wurden abgeschnitten */
    esp_err_t last_error;
} router_http_response_t;

static struct {
    bool          initialised;
    SemaphoreHandle_t mutex;
    char          session[65];        /* 32 Hex-Bytes + NUL                */
    int64_t       session_login_us;   /* esp_timer_get_time() beim Login   */
    int           session_timeout_s;  /* vom Server gemeldet               */
    int           rpc_id;             /* Monoton steigend für JSON-RPC id  */
    char          url[ROUTER_URL_MAX];
} s_ctx;

/** Gibt Response inkl. dynamischem Buffer frei */
static void resp_free(router_http_response_t *r)
{
    if (r) {
        free(r->buffer);  /* PSRAM/Heap */
        free(r);           /* Struct selbst */
    }
}

/* ── HTTP Event Handler ──────────────────────────────────── */

static esp_err_t router_http_event(esp_http_client_event_t *evt)
{
    router_http_response_t *resp = (router_http_response_t *)evt->user_data;
    if (!resp) return ESP_FAIL;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data && evt->data_len > 0) {
            /* Bereits abgeschnitten? Rest still verwerfen */
            if (resp->truncated) return ESP_OK;

            size_t needed = resp->length + evt->data_len + 1; /* +1 für NUL */
            size_t limit  = resp->truncate_ok ? ROUTER_LOGIN_MAX
                                               : ROUTER_HTTP_MAX;
            if (needed > limit) {
                if (resp->truncate_ok) {
                    /* Login-Antwort: Session-ID steht am Anfang, Rest weg */
                    resp->truncated = true;
                    return ESP_OK;
                }
                if (resp->last_error == ESP_OK) {
                    ESP_LOGE(TAG, "HTTP-Antwort > %zu Bytes – abgebrochen",
                             limit);
                }
                resp->last_error = ESP_ERR_NO_MEM;
                return ESP_FAIL;
            }
            /* Buffer bei Bedarf verdoppeln */
            if (needed > resp->capacity) {
                size_t new_cap = resp->capacity ? resp->capacity : ROUTER_HTTP_INITIAL;
                while (new_cap < needed) new_cap *= 2;
                if (new_cap > ROUTER_HTTP_MAX) new_cap = ROUTER_HTTP_MAX;
                char *tmp = heap_caps_realloc(resp->buffer, new_cap,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!tmp) tmp = realloc(resp->buffer, new_cap);
                if (!tmp) {
                    ESP_LOGE(TAG, "HTTP realloc fehlgeschlagen (%zu)", new_cap);
                    resp->last_error = ESP_ERR_NO_MEM;
                    return ESP_FAIL;
                }
                resp->buffer = tmp;
                resp->capacity = new_cap;
            }
            memcpy(resp->buffer + resp->length, evt->data, evt->data_len);
            resp->length += evt->data_len;
        }
        break;
    case HTTP_EVENT_ERROR:
        resp->last_error = ESP_FAIL;
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ── HTTP POST Helper ────────────────────────────────────── */

/**
 * Sendet POST-Body an /ubus, füllt response.
 * Caller muss response->buffer NUL-terminieren.
 */
static esp_err_t router_http_post(const char *body,
                                   router_http_response_t *response)
{
    /* Nur Metadaten zurücksetzen, buffer bleibt NULL bis Event-Handler */
    response->buffer     = NULL;
    response->capacity   = 0;
    response->length     = 0;
    response->truncated  = false;
    response->last_error = ESP_OK;
    /* truncate_ok bleibt – wird vom Caller gesetzt */

    esp_http_client_config_t cfg = {
        .url            = s_ctx.url,
        .method         = HTTP_METHOD_POST,
        .timeout_ms     = HTTP_TIMEOUT_MS,
        .event_handler  = router_http_event,
        .user_data      = response,
        .disable_auto_redirect = true,
        .buffer_size    = 1024,        /* Receive-Buffer für Chunked-Decode */
        .buffer_size_tx = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP-Client init fehlgeschlagen");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST fehlgeschlagen: %s", esp_err_to_name(err));
    } else {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "HTTP Status %d", status);
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    /* NUL-terminieren */
    if (response->buffer && response->length < response->capacity) {
        response->buffer[response->length] = '\0';
    } else if (response->buffer) {
        response->buffer[response->capacity - 1] = '\0';
    }

    esp_http_client_cleanup(client);

    /* Bei truncate_ok ist Abbruch durch Event-Handler kein Fehler */
    if (response->truncate_ok && response->truncated && response->buffer) {
        return ESP_OK;
    }
    return (err == ESP_OK && response->last_error == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/* ── JSON-RPC Request bauen ──────────────────────────────── */

/**
 * Baut ein JSON-RPC 2.0 "call"-Request-Objekt.
 *
 * @param session  Session-Token (oder UBUS_NULL_SESSION)
 * @param object   ubus-Objekt (z.B. "uci", "session", "file", "iwinfo")
 * @param method   Methode (z.B. "get", "login", "exec", "info")
 * @param args     cJSON-Objekt mit Argumenten (wird geklont)
 * @return         Heap-allokierter JSON-String (Caller muss free()!)
 */
static char *rpc_build_request(const char *session,
                                const char *object,
                                const char *method,
                                const cJSON *args)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", ++s_ctx.rpc_id);
    cJSON_AddStringToObject(root, "method", "call");

    cJSON *params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateString(session));
    cJSON_AddItemToArray(params, cJSON_CreateString(object));
    cJSON_AddItemToArray(params, cJSON_CreateString(method));
    cJSON_AddItemToArray(params, args ? cJSON_Duplicate(args, true)
                                      : cJSON_CreateObject());
    cJSON_AddItemToObject(root, "params", params);

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/* ── ubus-Antwort parsen ─────────────────────────────────── */

/**
 * Parst {"jsonrpc":"2.0","id":N,"result":[<code>,{...}]}
 *
 * @param json_str  Rohe HTTP-Antwort
 * @param out_code  ubus-Return-Code (0 = OK)
 * @param out_data  Zeiger auf geklontes Daten-Objekt (Caller: cJSON_Delete!)
 *                  Kann NULL sein wenn nur code gewünscht.
 * @return ESP_OK bei gültigem JSON-RPC result
 */
static esp_err_t rpc_parse_response(const char *json_str,
                                     int *out_code,
                                     cJSON **out_data)
{
    if (out_data) *out_data = NULL;
    if (out_code) *out_code = -1;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON-Parse fehlgeschlagen");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Fehler-Objekt? */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        ESP_LOGE(TAG, "JSON-RPC Fehler: %s",
                 msg ? msg->valuestring : "unbekannt");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result) || cJSON_GetArraySize(result) < 1) {
        ESP_LOGE(TAG, "Kein result-Array");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    int code = cJSON_GetArrayItem(result, 0)->valueint;
    if (out_code) *out_code = code;

    if (code != 0) {
        const char *ubus_errors[] = {
            "OK", "INVALID_COMMAND", "INVALID_ARGUMENT", "METHOD_NOT_FOUND",
            "NOT_FOUND", "NO_DATA", "PERMISSION_DENIED", "TIMEOUT"
        };
        const char *err_str = (code >= 0 && code < 8) ? ubus_errors[code] : "UNKNOWN";
        ESP_LOGW(TAG, "ubus Returncode %d (%s)", code, err_str);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    if (out_data && cJSON_GetArraySize(result) >= 2) {
        *out_data = cJSON_Duplicate(cJSON_GetArrayItem(result, 1), true);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── Session Management ──────────────────────────────────── */

static bool session_is_valid(void)
{
    if (s_ctx.session[0] == '\0') return false;

    int64_t now_us = esp_timer_get_time();
    int64_t age_s  = (now_us - s_ctx.session_login_us) / 1000000LL;
    int limit_s    = s_ctx.session_timeout_s - SESSION_MARGIN_S;
    if (limit_s < 10) limit_s = 10;

    return (age_s < limit_s);
}

static esp_err_t do_login(void)
{
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "username", CONFIG_WOMO_ROUTER_USERNAME);
    cJSON_AddStringToObject(args, "password", CONFIG_WOMO_ROUTER_PASSWORD);

    char *body = rpc_build_request(UBUS_NULL_SESSION, "session", "login", args);
    cJSON_Delete(args);

    if (!body) {
        ESP_LOGE(TAG, "Login-Request konnte nicht gebaut werden");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Login an %s …", s_ctx.url);

    /*
     * Die Login-Antwort enthält die volle ACL-Liste (>20 KB).
     * Session-ID + Timeout stehen aber in den ersten ~200 Bytes,
     * z.B.: {"jsonrpc":"2.0","id":1,"result":[0,{"ubus_rpc_session":"abc...","timeout":300,...
     * Wir brauchen nur diesen Anfang → truncate_ok = true.
     */
    router_http_response_t *resp = heap_caps_calloc(1, sizeof(*resp),
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resp) resp = calloc(1, sizeof(*resp));
    if (!resp) { free(body); return ESP_ERR_NO_MEM; }
    resp->truncate_ok = true;   /* Rest der ACL-Daten verwerfen */

    esp_err_t err = router_http_post(body, resp);
    free(body);

    if (err != ESP_OK && !resp->buffer) {
        resp_free(resp);
        return err;
    }

    /* Session-ID per String-Suche extrahieren (robuster als JSON-Parser
     * bei abgeschnittener Antwort) */
    esp_err_t ret = ESP_FAIL;
    if (resp->buffer) {
        const char *sid_key = "\"ubus_rpc_session\":\"";
        char *p = strstr(resp->buffer, sid_key);
        if (p) {
            p += strlen(sid_key);
            char *end = strchr(p, '"');
            if (end && (end - p) < (int)sizeof(s_ctx.session)) {
                size_t len = end - p;
                memcpy(s_ctx.session, p, len);
                s_ctx.session[len] = '\0';

                /* Timeout extrahieren */
                const char *to_key = "\"timeout\":";
                char *tp = strstr(resp->buffer, to_key);
                s_ctx.session_timeout_s = tp ? atoi(tp + strlen(to_key))
                                             : SESSION_TIMEOUT_S;

                s_ctx.session_login_us = esp_timer_get_time();

                ESP_LOGI(TAG, "Login OK, Session %.8s…, Timeout %d s",
                         s_ctx.session, s_ctx.session_timeout_s);
                ret = ESP_OK;
            }
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Session-ID nicht in Antwort gefunden");
        }
    } else {
        ESP_LOGE(TAG, "Keine Antwort vom Router");
    }

    resp_free(resp);
    return ret;
}

/* ── Generischer ubus-Call (intern) ──────────────────────── */

/**
 * Führt einen ubus JSON-RPC Call aus.
 * Session wird vorher sichergestellt.
 * Bei Fehler 6 (UBUS_STATUS_PERMISSION_DENIED) wird einmalig re-login versucht.
 */
static esp_err_t ubus_call(const char *object,
                             const char *method,
                             const cJSON *args,
                             int *out_code,
                             cJSON **out_data)
{
    esp_err_t err = womo_router_uci_ensure_session();
    if (err != ESP_OK) return err;

    char *body = rpc_build_request(s_ctx.session, object, method, args);
    if (!body) return ESP_ERR_NO_MEM;

    router_http_response_t *resp = heap_caps_calloc(1, sizeof(*resp),
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resp) resp = calloc(1, sizeof(*resp));
    if (!resp) { free(body); return ESP_ERR_NO_MEM; }

    err = router_http_post(body, resp);
    free(body);

    if (err != ESP_OK) {
        resp_free(resp);
        return err;
    }

    int code = -1;
    cJSON *data = NULL;
    err = rpc_parse_response(resp->buffer, &code, &data);
    resp_free(resp);

    /* UBUS_STATUS_PERMISSION_DENIED (6) → re-login */
    if (err == ESP_ERR_INVALID_STATE && code == 6) {
        ESP_LOGW(TAG, "Permission denied – versuche re-login");
        s_ctx.session[0] = '\0';
        if (data) { cJSON_Delete(data); data = NULL; }

        err = womo_router_uci_ensure_session();
        if (err != ESP_OK) return err;

        body = rpc_build_request(s_ctx.session, object, method, args);
        if (!body) return ESP_ERR_NO_MEM;

        resp = heap_caps_calloc(1, sizeof(*resp),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!resp) resp = calloc(1, sizeof(*resp));
        if (!resp) { free(body); return ESP_ERR_NO_MEM; }

        err = router_http_post(body, resp);
        free(body);

        if (err == ESP_OK) {
            err = rpc_parse_response(resp->buffer, &code, &data);
        }
        resp_free(resp);
    }

    if (out_code) *out_code = code;
    if (out_data) {
        *out_data = data;
    } else if (data) {
        cJSON_Delete(data);
    }

    return err;
}

/* ── Öffentliche API: Init / Session ─────────────────────── */

esp_err_t womo_router_uci_init(void)
{
    if (s_ctx.initialised) return ESP_OK;

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) {
        ESP_LOGE(TAG, "Mutex konnte nicht erstellt werden");
        return ESP_ERR_NO_MEM;
    }

    snprintf(s_ctx.url, sizeof(s_ctx.url),
             "http://%s/ubus", CONFIG_WOMO_ROUTER_IP);

    s_ctx.session[0] = '\0';
    s_ctx.rpc_id = 0;
    s_ctx.initialised = true;

    ESP_LOGI(TAG, "Router UCI Client initialisiert → %s", s_ctx.url);
    return ESP_OK;
}

esp_err_t womo_router_uci_ensure_session(void)
{
    if (!s_ctx.initialised) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    esp_err_t err = ESP_OK;
    if (!session_is_valid()) {
        err = do_login();
    }

    xSemaphoreGive(s_ctx.mutex);
    return err;
}

void womo_router_uci_logout(void)
{
    if (!s_ctx.initialised) return;

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.session[0] = '\0';
    xSemaphoreGive(s_ctx.mutex);

    ESP_LOGI(TAG, "Session verworfen");
}

/* ── Öffentliche API: Low-Level UCI ──────────────────────── */

esp_err_t womo_router_uci_get(const char *config,
                               const char *section,
                               const char *option,
                               void **out_json)
{
    if (!config || !out_json) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "config", config);
    if (section) cJSON_AddStringToObject(args, "section", section);
    if (option)  cJSON_AddStringToObject(args, "option", option);

    cJSON *data = NULL;
    esp_err_t err = ubus_call("uci", "get", args, NULL, &data);
    cJSON_Delete(args);

    if (err == ESP_OK && data) {
        *out_json = data;
    }
    return err;
}

esp_err_t womo_router_uci_set(const char *config,
                               const char *section,
                               const void *values_json)
{
    if (!config || !section || !values_json) return ESP_ERR_INVALID_ARG;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "config", config);
    cJSON_AddStringToObject(args, "section", section);
    cJSON_AddItemToObject(args, "values",
                          cJSON_Duplicate((const cJSON *)values_json, true));

    esp_err_t err = ubus_call("uci", "set", args, NULL, NULL);
    cJSON_Delete(args);

    return err;
}

esp_err_t womo_router_uci_commit(const char *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "config", config);

    esp_err_t err = ubus_call("uci", "commit", args, NULL, NULL);
    cJSON_Delete(args);

    return err;
}

/* ── Öffentliche API: Shell Exec ─────────────────────────── */

esp_err_t womo_router_exec(const char *command,
                            const char **params,
                            int param_count,
                            char *out_stdout,
                            size_t stdout_size)
{
    if (!command) return ESP_ERR_INVALID_ARG;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "command", command);

    if (params && param_count > 0) {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < param_count; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(params[i]));
        }
        cJSON_AddItemToObject(args, "params", arr);
    }

    cJSON *data = NULL;
    esp_err_t err = ubus_call("file", "exec", args, NULL, &data);
    cJSON_Delete(args);

    if (err == ESP_OK && data) {
        /* Prüfe exit-code */
        cJSON *rc = cJSON_GetObjectItem(data, "code");
        if (rc && cJSON_IsNumber(rc) && rc->valueint != 0) {
            cJSON *serr = cJSON_GetObjectItem(data, "stderr");
            const char *errstr = (serr && cJSON_IsString(serr)) ? serr->valuestring : "";
            /* Bei /bin/sh -c den eigentlichen Befehl loggen */
            const char *display_cmd = command;
            if (params && param_count >= 2 && strcmp(command, "/bin/sh") == 0) {
                display_cmd = params[1]; /* der -c Befehl */
            }
            ESP_LOGW(TAG, "exec '%s' → exit %d  stderr='%.120s'",
                     display_cmd, rc->valueint, errstr);
        }

        /* stdout kopieren */
        if (out_stdout && stdout_size > 0) {
            cJSON *sout = cJSON_GetObjectItem(data, "stdout");
            if (sout && cJSON_IsString(sout)) {
                strncpy(out_stdout, sout->valuestring, stdout_size - 1);
                out_stdout[stdout_size - 1] = '\0';
            } else {
                out_stdout[0] = '\0';
            }
        }

        cJSON_Delete(data);
    } else if (out_stdout && stdout_size > 0) {
        out_stdout[0] = '\0';
    }

    return err;
}

/* ── Hilfsfunktionen ─────────────────────────────────────── */

/** RSSI → Prozent (Wi-Fi üblich: -30 dBm = 100%, -90 dBm = 0%) */
static uint8_t rssi_to_percent(int rssi)
{
    if (rssi >= -30) return 100;
    if (rssi <= -90) return 0;
    return (uint8_t)((rssi + 90) * 100 / 60);
}

/** Sichere Kopie eines cJSON-Strings in Buffer */
static void json_strcpy(char *dst, size_t dst_size, const cJSON *obj, const char *key)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!obj) return;
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

/** Extrahiert WiFi-Status-Felder aus einem iwinfo-info-JSON-Objekt */
static void wifi_extract_info(womo_router_wifi_status_t *out, const cJSON *data)
{
    json_strcpy(out->ssid, sizeof(out->ssid), data, "ssid");
    json_strcpy(out->bssid, sizeof(out->bssid), data, "bssid");
    json_strcpy(out->mode, sizeof(out->mode), data, "mode");

    /* encryption kann String oder Objekt sein */
    cJSON *enc = cJSON_GetObjectItem(data, "encryption");
    if (enc && cJSON_IsString(enc)) {
        strncpy(out->encryption, enc->valuestring, sizeof(out->encryption) - 1);
    } else if (enc && cJSON_IsObject(enc)) {
        cJSON *en = cJSON_GetObjectItem(enc, "enabled");
        if (en && cJSON_IsTrue(en)) {
            strncpy(out->encryption, "encrypted", sizeof(out->encryption) - 1);
        }
    }

    cJSON *ch = cJSON_GetObjectItem(data, "channel");
    if (ch && cJSON_IsNumber(ch)) out->channel = (uint8_t)ch->valueint;

    cJSON *br = cJSON_GetObjectItem(data, "bitrate");
    if (br && cJSON_IsNumber(br)) out->bitrate_mbps = (uint32_t)(br->valueint / 1000);

    cJSON *sig = cJSON_GetObjectItem(data, "signal");
    if (sig && cJSON_IsNumber(sig)) {
        out->rssi = (int8_t)sig->valueint;
    }

    /* quality/quality_max nutzen (wie Router-UI), sonst rssi_to_percent */
    cJSON *qual = cJSON_GetObjectItem(data, "quality");
    cJSON *qmax = cJSON_GetObjectItem(data, "quality_max");
    if (qual && cJSON_IsNumber(qual) && qmax && cJSON_IsNumber(qmax) && qmax->valueint > 0) {
        out->signal_percent = (uint8_t)(qual->valueint * 100 / qmax->valueint);
    } else if (out->rssi != 0) {
        out->signal_percent = rssi_to_percent(out->rssi);
    }

    out->connected = (out->ssid[0] != '\0' && (out->rssi != 0 || out->signal_percent > 0));
}

/* ── Helper: STA-Interface finden ────────────────────────── */

/**
 * Lädt den kompletten wireless-UCI-Dump und sucht die Sektion
 * mit option "mode" == "sta" / "client".
 * Der gefundene Name wird gecacht (einmalige Suche pro Boot).
 *
 * Router-Antwort auf uci get {"config":"wireless"} (ohne section/option):
 *   {"result":[0,{"values":{"wifi_iface_0":{".type":"wifi-iface","mode":"ap",...},
 *                           "wifi_iface_1":{".type":"wifi-iface","mode":"sta",...}}}]}
 *
 * @return Sektionsname (z.B. "wifi_iface_1") oder NULL wenn nicht gefunden
 */
static const char *find_sta_interface(void)
{
    static char   s_sta_iface[32] = "";
    static bool   s_searched      = false;

    if (s_searched && s_sta_iface[0]) {
        return s_sta_iface;
    }

    /* Kompletten wireless-Dump holen */
    cJSON *dump = NULL;
    esp_err_t err = womo_router_uci_get("wireless", NULL, NULL, (void **)&dump);
    if (err != ESP_OK || !dump) {
        ESP_LOGW(TAG, "find_sta_interface: uci get wireless fehlgeschlagen");
        s_searched = true;
        return NULL;
    }

    /* Antwortstruktur: {"values": {"<name>": {".type":"wifi-iface","mode":"sta",...}}} */
    cJSON *values = cJSON_GetObjectItem(dump, "values");
    if (!cJSON_IsObject(values)) {
        ESP_LOGW(TAG, "find_sta_interface: kein 'values'-Objekt in Antwort");
        cJSON_Delete(dump);
        s_searched = true;
        return NULL;
    }

    cJSON *sec = NULL;
    cJSON_ArrayForEach(sec, values) {
        if (!cJSON_IsObject(sec)) continue;

        /* Nur wifi-iface Sektionen prüfen */
        cJSON *type = cJSON_GetObjectItem(sec, ".type");
        if (!type || !cJSON_IsString(type)) continue;
        if (strcmp(type->valuestring, "wifi-iface") != 0) continue;

        /* mode == "sta" oder "Client" (Teltonika) */
        cJSON *mode = cJSON_GetObjectItem(sec, "mode");
        if (!mode || !cJSON_IsString(mode)) continue;
        if (strcasecmp(mode->valuestring, "sta")    != 0 &&
            strcasecmp(mode->valuestring, "client") != 0) {
            continue;
        }

        /* Sektionsname: cJSON Array-Key */
        const char *name = sec->string;
        if (!name || name[0] == '\0') continue;

        strncpy(s_sta_iface, name, sizeof(s_sta_iface) - 1);
        s_sta_iface[sizeof(s_sta_iface) - 1] = '\0';
        s_searched = true;
        ESP_LOGI(TAG, "STA-Interface gefunden: wireless.%s (mode=%s)",
                 s_sta_iface, mode->valuestring);
        cJSON_Delete(dump);
        return s_sta_iface;
    }

    cJSON_Delete(dump);
    s_searched = true;
    ESP_LOGW(TAG, "Kein STA-Interface (mode=sta/client) in wireless-Config gefunden");
    return NULL;
}

/* ── High-Level: WiFi-Status ─────────────────────────────── */

esp_err_t womo_router_get_wifi_status(womo_router_wifi_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /* ── UCI disabled-Flag vorab prüfen ───────────────────────
     * Wenn STA-Interface per UCI deaktiviert wurde (disabled=1), melden
     * wir sofort „nicht verbunden".  iwinfo liefert sonst noch Restdaten
     * vom gerade herunterfahrenden Interface, und der Switch springt
     * fälschlicherweise wieder auf „an".
     */
    const char *sta_iface = find_sta_interface();
    if (sta_iface) {
        cJSON *uci_dis = NULL;
        esp_err_t de = womo_router_uci_get("wireless", sta_iface,
                                            "disabled", (void **)&uci_dis);
        if (de == ESP_OK && uci_dis) {
            cJSON *val = cJSON_GetObjectItem(uci_dis, "value");
            if (val && cJSON_IsString(val) && strcmp(val->valuestring, "1") == 0) {
                ESP_LOGD(TAG, "WiFi STA UCI disabled=1 → enabled=false, connected=false");
                cJSON_Delete(uci_dis);
                /* out ist bereits memset(0) → enabled=false, connected=false */
                return ESP_OK;
            }
            cJSON_Delete(uci_dis);
        }
    }

    /* Ab hier ist das Interface enabled (disabled != 1 oder unbekannt) */
    out->enabled = true;

    /*
     * STA/Client-Interface automatisch finden:
     * 1. iwinfo devices → Liste aller WLAN-Interfaces
     * 2. Für jedes Interface iwinfo info abfragen
     * 3. Das Interface mit mode=="Client" verwenden
     *
     * Fallback: bekannte RUTX11-Interfacenamen durchprobieren
     * (wifi0 = Radio, mob1s1a1 = LTE auf RUTX11)
     */

    /* Gecachter Interface-Name für STA (spart Wiederholung) */
    static char s_sta_device[16] = "";

    /* Helper: Einzelnes Interface abfragen und STA-Daten extrahieren */
    #define TRY_DEVICE(dev_name) do { \
        cJSON *_a = cJSON_CreateObject(); \
        cJSON_AddStringToObject(_a, "device", (dev_name)); \
        cJSON *_d = NULL; \
        esp_err_t _e = ubus_call("iwinfo", "info", _a, NULL, &_d); \
        cJSON_Delete(_a); \
        if (_e == ESP_OK && _d) { \
            cJSON *_mode = cJSON_GetObjectItem(_d, "mode"); \
            if (_mode && cJSON_IsString(_mode) && \
                strcasecmp(_mode->valuestring, "Client") == 0) { \
                wifi_extract_info(out, _d); \
                strncpy(s_sta_device, (dev_name), sizeof(s_sta_device)-1); \
                s_sta_device[sizeof(s_sta_device)-1] = '\0'; \
                cJSON_Delete(_d); \
                return ESP_OK; \
            } \
            cJSON_Delete(_d); \
        } \
    } while(0)

    /* Wenn wir das STA-Interface bereits kennen, direkt versuchen
     * (ohne TRY_DEVICE-Makro, weil dev_name == s_sta_device wäre) */
    if (s_sta_device[0]) {
        cJSON *_ca = cJSON_CreateObject();
        cJSON_AddStringToObject(_ca, "device", s_sta_device);
        cJSON *_cd = NULL;
        esp_err_t _ce = ubus_call("iwinfo", "info", _ca, NULL, &_cd);
        cJSON_Delete(_ca);
        if (_ce == ESP_OK && _cd) {
            cJSON *_cm = cJSON_GetObjectItem(_cd, "mode");
            if (_cm && cJSON_IsString(_cm) &&
                strcasecmp(_cm->valuestring, "Client") == 0) {
                wifi_extract_info(out, _cd);
                cJSON_Delete(_cd);
                return ESP_OK;
            }
            cJSON_Delete(_cd);
        }
        /* Interface-Name nicht mehr gültig → Cache leeren und neu suchen */
        ESP_LOGD(TAG, "Gecachtes STA-Interface '%s' nicht mehr gültig", s_sta_device);
        s_sta_device[0] = '\0';
    }

    /* Versuch 1: iwinfo devices → alle Interfaces auflisten */
    cJSON *dev_args = cJSON_CreateObject();
    cJSON *dev_data = NULL;
    esp_err_t err = ubus_call("iwinfo", "devices", dev_args, NULL, &dev_data);
    cJSON_Delete(dev_args);

    if (err == ESP_OK && dev_data) {
        cJSON *devices = cJSON_GetObjectItem(dev_data, "devices");
        if (cJSON_IsArray(devices)) {
            cJSON *dev = NULL;
            cJSON_ArrayForEach(dev, devices) {
                if (cJSON_IsString(dev)) {
                    TRY_DEVICE(dev->valuestring);
                }
            }
        }
        cJSON_Delete(dev_data);
    } else {
        if (dev_data) cJSON_Delete(dev_data);
        ESP_LOGW(TAG, "iwinfo devices fehlgeschlagen – versuche Fallback-Interfaces");
    }

    /* Versuch 2: Bekannte RUTX11-Interfacenamen durchprobieren */
    static const char *fallbacks[] = {"wlan0-2", "wifi0", "wlan0-1", "wlan1", NULL};
    for (int i = 0; fallbacks[i]; i++) {
        TRY_DEVICE(fallbacks[i]);
    }

    #undef TRY_DEVICE

    ESP_LOGW(TAG, "Kein WiFi-Client-Interface (STA) auf dem Router gefunden");
    return ESP_FAIL;
}

/* ── High-Level: LTE-Status ──────────────────────────────── */

/** Hilfsfunktion: RSSI dBm → Prozent (LTE-Skala) */
static uint8_t lte_rssi_to_percent(int rssi)
{
    if (rssi >= -51)  return 100;
    if (rssi <= -113) return 0;
    return (uint8_t)((rssi + 113) * 100 / 62);
}

static bool contains_case_insensitive(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;

    for (const char *h = haystack; *h; ++h) {
        const char *hp = h;
        const char *np = needle;
        while (*hp && *np &&
               tolower((unsigned char)*hp) == tolower((unsigned char)*np)) {
            ++hp;
            ++np;
        }
        if (*np == '\0') return true;
    }

    return false;
}

static bool lte_output_is_error_text(const char *text)
{
    if (!text || !text[0]) return false;

    return contains_case_insensitive(text, "error") ||
           contains_case_insensitive(text, "couldn't retrive") ||
           contains_case_insensitive(text, "couldn't retrieve") ||
           contains_case_insensitive(text, "not available") ||
           contains_case_insensitive(text, "no data");
}

/** Hilfsfunktion: Sucht in mehrzeiliger gsmctl -q Ausgabe nach "RSSI: <wert>" */
static int parse_gsmctl_rssi(const char *buf)
{
    /* gsmctl -q liefert z.B.: "RSSI: -54\nRSRP: -87\nSINR: 8\nRSRQ: -12\n" */
    const char *p = strstr(buf, "RSSI:");
    if (p) {
        p += 5;  /* strlen("RSSI:") */
        while (*p == ' ') p++;
        return atoi(p);
    }
    /* Fallback: vielleicht nur eine nackte Zahl */
    int v = atoi(buf);
    return (v < 0 && v > -130) ? v : 0;
}

/**
 * Hilfs-Wrapper: Führt einen Shell-Befehl über /bin/sh -c aus.
 * Damit funktioniert PATH-Lookup und Pipes.
 */
static esp_err_t router_sh(const char *cmdline, char *out, size_t out_size)
{
    const char *p[] = {"-c", cmdline};
    return womo_router_exec("/bin/sh", p, 2, out, out_size);
}

esp_err_t womo_router_get_lte_status(womo_router_lte_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /*
     * gsmctl auf dem RUTX11 – Aufruf über /bin/sh -c damit PATH greift.
     * Neuere Firmware-Versionen brauchen ggf. -n <modem_id>.
     */
    char buf[256];

    /* Operator */
    if (router_sh("gsmctl -o", buf, sizeof(buf)) == ESP_OK && buf[0]) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        if (lte_output_is_error_text(buf)) {
            ESP_LOGW(TAG, "gsmctl -o returned error text: '%s'", buf);
        } else {
            strncpy(out->operator_name, buf, sizeof(out->operator_name) - 1);
            ESP_LOGD(TAG, "gsmctl -o: '%s'", out->operator_name);
        }
    }

    /* Signalstärke – gsmctl -q liefert mehrzeilig: RSSI: -54\nRSRP: -87\n... */
    if (router_sh("gsmctl -q", buf, sizeof(buf)) == ESP_OK && buf[0]) {
        ESP_LOGD(TAG, "gsmctl -q raw: '%.*s'", 120, buf);
        out->rssi_dbm = parse_gsmctl_rssi(buf);
        if (out->rssi_dbm != 0)
            out->signal_percent = lte_rssi_to_percent(out->rssi_dbm);
    }

    /* Netztyp (z.B. "4G (LTE)") */
    if (router_sh("gsmctl -t", buf, sizeof(buf)) == ESP_OK && buf[0]) {
        ESP_LOGD(TAG, "gsmctl -t RAW output: '%s' (len=%d)", buf, strlen(buf));
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        strncpy(out->conn_type, buf, sizeof(out->conn_type) - 1);
        ESP_LOGD(TAG, "gsmctl -t parsed: '%s'", out->conn_type);
    } else {
        ESP_LOGW(TAG, "gsmctl -t: failed or empty");
    }

    /* SIM-State: gsmctl -e auf RUTX11 ist "--bsent <INTERFACE>" (Bytes sent),
     * NICHT SIM-Status.  Feld wird vorerst leer gelassen. */

    out->registered = (out->operator_name[0] != '\0' && out->signal_percent > 0);

    if (!out->registered) {
        out->signal_percent = 0;
        out->rssi_dbm = 0;
    }

    /* ── UCI disabled-Flag prüfen ─────────────────────────────
     * Wenn das Interface per UCI deaktiviert wurde (disabled=1), gilt
     * es als offline, auch wenn das Modem noch Restdaten liefert.
     * Das verhindert, dass der Switch nach dem Abschalten sofort
     * wieder auf "an" springt.
     */
    if (out->registered) {
        cJSON *uci_data = NULL;
        esp_err_t de = womo_router_uci_get("network", "mob1s1a1",
                                            "disabled", (void **)&uci_data);
        if (de == ESP_OK && uci_data) {
            cJSON *val = cJSON_GetObjectItem(uci_data, "value");
            if (val && cJSON_IsString(val) && strcmp(val->valuestring, "1") == 0) {
                ESP_LOGD(TAG, "LTE UCI disabled=1 → registered=false");
                out->registered = false;
            }
            cJSON_Delete(uci_data);
        }
    }

    return ESP_OK;
}

/* ── High-Level: GPS ─────────────────────────────────────── */

esp_err_t womo_router_get_gps(womo_router_gps_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /*
     * RUTX11 gpsctl Optionen:
     *   -i  Latitude        -x  Longitude       -a  Altitude
     *   -v  Speed (km/h)    -p  Satellites       -g  Angle (heading)
     *   -s  Fix status      -u  Accuracy         -e  Datetime (YYYY-MM-DD HH:MM:SS)
     *
     * Einzelne Aufrufe mit echo-Prefix, durch ; verkettet.
     * Einfacher/robuster als printf mit 9 Subshells.
     */
    char buf[320];
    esp_err_t err = router_sh(
        "echo lat=$(gpsctl -i);"
        "echo lon=$(gpsctl -x);"
        "echo alt=$(gpsctl -a);"
        "echo spd=$(gpsctl -v);"
        "echo sat=$(gpsctl -p);"
        "echo hdg=$(gpsctl -g);"
        "echo fix=$(gpsctl -s);"
        "echo acc=$(gpsctl -u);"
        "echo dt=$(gpsctl -e)",
        buf, sizeof(buf));

    static int gps_log_count = 0;
    if (gps_log_count < 5) {
        ESP_LOGI(TAG, "GPS raw[%d]: '%.200s'", gps_log_count, buf);
        gps_log_count++;
    }

    if (err != ESP_OK || !buf[0]) return err != ESP_OK ? err : ESP_FAIL;

    /* Zeilenweise parsen: key=value */
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *key = line;
            const char *val = eq + 1;

            if (strcmp(key, "lat") == 0)       out->latitude   = strtod(val, NULL);
            else if (strcmp(key, "lon") == 0)   out->longitude  = strtod(val, NULL);
            else if (strcmp(key, "alt") == 0)   out->altitude_m = strtod(val, NULL);
            else if (strcmp(key, "spd") == 0)   out->speed_kmh  = strtod(val, NULL);
            else if (strcmp(key, "sat") == 0)   out->satellites  = atoi(val);
            else if (strcmp(key, "hdg") == 0)   out->heading     = strtod(val, NULL);
            else if (strcmp(key, "acc") == 0)   out->accuracy_m  = strtod(val, NULL);
            else if (strcmp(key, "fix") == 0)   out->fix_status  = atoi(val);
            else if (strcmp(key, "dt") == 0)    strncpy(out->utc_time, val, sizeof(out->utc_time) - 1);
        }

        line = nl ? nl + 1 : NULL;
    }

    /* Gültig wenn Fix-Status >= 2 (2D/3D) oder Koordinaten sinnvoll */
    if (out->fix_status >= 2) {
        out->valid = true;
    } else if (fabs(out->latitude) > 0.01 || fabs(out->longitude) > 0.01) {
        out->valid = true;
    }

    return ESP_OK;
}

/* ── High-Level: WiFi-Scan ───────────────────────────────── */

esp_err_t womo_router_wifi_scan(womo_router_scan_result_t *results,
                                 size_t max_results,
                                 size_t *out_count)
{
    if (!results || !out_count || max_results == 0) return ESP_ERR_INVALID_ARG;
    *out_count = 0;

    /*
     * iwinfo scan liefert ein Array von Objekten:
     * [{"ssid":"Test","bssid":"AA:BB:...", "channel":6, "signal":-45, ...}]
     *
     * Scan auf einem aktiven WLAN-Interface.
     * Interfacename wird dynamisch per 'iwinfo devices' ermittelt,
     * da der RUTX11 verschiedene Namen vergeben kann.
     */

    /* Schritt 1: verfügbare WLAN-Interfaces ermitteln */
    const char *scan_device = NULL;
    static const char *scan_fallbacks[] = {"wlan0-2", "wifi0", "wlan0", "wlan1", NULL};

    cJSON *dev_args = cJSON_CreateObject();
    cJSON *dev_data = NULL;
    esp_err_t derr = ubus_call("iwinfo", "devices", dev_args, NULL, &dev_data);
    cJSON_Delete(dev_args);

    char discovered_device[16] = "";
    if (derr == ESP_OK && dev_data) {
        cJSON *devices = cJSON_GetObjectItem(dev_data, "devices");
        if (cJSON_IsArray(devices)) {
            /* Erstes verfügbares Interface nehmen (typischerweise AP = immer up) */
            cJSON *first = cJSON_GetArrayItem(devices, 0);
            if (first && cJSON_IsString(first)) {
                strncpy(discovered_device, first->valuestring, sizeof(discovered_device) - 1);
                scan_device = discovered_device;
            }
            ESP_LOGI(TAG, "iwinfo devices: %d Interfaces, Scan auf '%s'",
                     cJSON_GetArraySize(devices),
                     scan_device ? scan_device : "?");
        }
        cJSON_Delete(dev_data);
    } else {
        if (dev_data) cJSON_Delete(dev_data);
        ESP_LOGW(TAG, "iwinfo devices fehlgeschlagen – versuche Fallback-Interfaces");
    }

    /* Schritt 2: Scan durchführen (entdecktes Device oder Fallbacks) */
    cJSON *data = NULL;
    esp_err_t err = ESP_FAIL;

    if (scan_device) {
        cJSON *args = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "device", scan_device);
        err = ubus_call("iwinfo", "scan", args, NULL, &data);
        cJSON_Delete(args);
    }

    /* Fallbacks durchprobieren wenn nötig */
    for (int i = 0; (err != ESP_OK || !data) && scan_fallbacks[i]; i++) {
        if (data) { cJSON_Delete(data); data = NULL; }
        cJSON *args = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "device", scan_fallbacks[i]);
        err = ubus_call("iwinfo", "scan", args, NULL, &data);
        cJSON_Delete(args);
        if (err == ESP_OK && data) {
            ESP_LOGI(TAG, "WiFi-Scan erfolgreich auf Fallback '%s'", scan_fallbacks[i]);
            break;
        }
    }

    if (err != ESP_OK || !data) {
        ESP_LOGW(TAG, "WiFi-Scan auf keinem Interface erfolgreich");
        if (data) cJSON_Delete(data);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    /* data sollte ein Object mit "results" Array sein */
    cJSON *arr = cJSON_GetObjectItem(data, "results");
    if (!arr) arr = data; /* Fallback: data selbst ist Array */

    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(data);
        return ESP_FAIL;
    }

    size_t count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (count >= max_results) break;

        womo_router_scan_result_t *r = &results[count];
        memset(r, 0, sizeof(*r));

        json_strcpy(r->ssid, sizeof(r->ssid), item, "ssid");
        json_strcpy(r->bssid, sizeof(r->bssid), item, "bssid");
        json_strcpy(r->encryption, sizeof(r->encryption), item, "encryption");

        cJSON *sig = cJSON_GetObjectItem(item, "signal");
        if (sig && cJSON_IsNumber(sig)) r->rssi = (int8_t)sig->valueint;

        cJSON *ch = cJSON_GetObjectItem(item, "channel");
        if (ch && cJSON_IsNumber(ch)) r->channel = (uint8_t)ch->valueint;

        /* Nur Einträge mit SSID zählen */
        if (r->ssid[0] != '\0') count++;
    }

    *out_count = count;
    cJSON_Delete(data);

    /* ── Deduplizierung: pro SSID nur den stärksten AP behalten ──────
     * Mehrere APs (Repeater, Mesh) mit gleicher SSID liefern separate
     * Einträge.  Für den Nutzer ist nur der stärkste relevant.
     */
    for (size_t i = 0; i < *out_count; i++) {
        for (size_t j = i + 1; j < *out_count; ) {
            if (strcmp(results[i].ssid, results[j].ssid) == 0) {
                /* Gleiche SSID → den schwächeren entfernen */
                if (results[j].rssi > results[i].rssi) {
                    /* j ist stärker → nach i kopieren */
                    results[i] = results[j];
                }
                /* j-Eintrag entfernen: letzten an Position j schieben */
                (*out_count)--;
                if (j < *out_count) {
                    results[j] = results[*out_count];
                }
                /* j nicht inkrementieren – neuer Eintrag an j muss auch geprüft werden */
            } else {
                j++;
            }
        }
    }

    return ESP_OK;
}

/* ── High-Level: WiFi STA konfigurieren ──────────────────── */

esp_err_t womo_router_wifi_set_sta(const char *ssid,
                                    const char *password,
                                    const char *encryption)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "WiFi STA konfigurieren: SSID='%s', Passwort: %s, Encryption: %s",
             ssid, password ? "ja" : "nein",
             (encryption && encryption[0]) ? encryption : "psk2");

    /* STA-Interface automatisch finden */
    const char *sta_iface = find_sta_interface();
    if (!sta_iface) {
        ESP_LOGE(TAG, "WiFi set_sta: Kein STA-Interface im Router-UCI gefunden");
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * Wir setzen:
     *   wireless.<sta_iface>.ssid = <ssid>
     *   wireless.<sta_iface>.key  = <password>
     *   wireless.<sta_iface>.encryption = <encryption | "psk2">
     */
    cJSON *values = cJSON_CreateObject();
    cJSON_AddStringToObject(values, "ssid", ssid);
    if (password && password[0]) {
        cJSON_AddStringToObject(values, "key", password);
    }
    cJSON_AddStringToObject(values, "encryption",
                             (encryption && encryption[0]) ? encryption : "psk2");
    /* Interface explizit aktivieren (disabled=0), falls es vorher
     * per enable_sta(false) deaktiviert wurde.  */
    cJSON_AddStringToObject(values, "disabled", "0");

    esp_err_t err = womo_router_uci_set("wireless", sta_iface, values);
    cJSON_Delete(values);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UCI set für STA '%s' fehlgeschlagen: %s", sta_iface, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UCI set OK, committing...");

    /* Commit + Reload */
    err = womo_router_uci_commit("wireless");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UCI commit fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UCI commit OK, reloading WiFi...");

    /* /sbin/wifi reload für zuverlässigen Wireless-Restart */
    const char *p[] = { "reload" };
    err = womo_router_exec("/sbin/wifi", p, 1, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi reload Warnung: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "WiFi STA → SSID='%s' konfiguriert auf '%s'", ssid, sta_iface);
    return ESP_OK;
}

/* ── High-Level: WiFi STA aktivieren/deaktivieren ────────── */

esp_err_t womo_router_wifi_enable_sta(bool enable)
{
    const char *sta_iface = find_sta_interface();
    if (!sta_iface) {
        ESP_LOGE(TAG, "WiFi enable_sta: Kein STA-Interface gefunden");
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *values = cJSON_CreateObject();
    cJSON_AddStringToObject(values, "disabled", enable ? "0" : "1");

    esp_err_t err = womo_router_uci_set("wireless", sta_iface, values);
    cJSON_Delete(values);

    if (err != ESP_OK) return err;

    err = womo_router_uci_commit("wireless");
    if (err != ESP_OK) return err;

    /* /sbin/wifi reload statt /sbin/reload_config:
     * reload_config ist generisch und löst auf dem RUTX11 nicht immer
     * einen sauberen Wireless-Restart aus.  wifi reload wendet die
     * UCI-wireless-Konfig zuverlässig an (Interface hoch/runter).  */
    const char *p[] = { "reload" };
    womo_router_exec("/sbin/wifi", p, 1, NULL, 0);

    ESP_LOGI(TAG, "WiFi STA %s", enable ? "aktiviert" : "deaktiviert");
    return ESP_OK;
}

/* ── High-Level: AP-Status ───────────────────────────────── */

/**
 * Liest /tmp/dhcp.leases vom Router und löst eine MAC-Adresse
 * auf den zugehörigen Hostnamen auf.
 * Format je Zeile: <timestamp> <mac> <ip> <hostname> <client-id>
 */
static void resolve_hostname_from_leases(const char *leases,
                                          const char *mac,
                                          char *hostname, size_t hostname_size)
{
    if (!leases || !mac || !hostname || hostname_size == 0) return;
    hostname[0] = '\0';

    const char *line = leases;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > 0 && len < 256) {
            char buf[256];
            memcpy(buf, line, len);
            buf[len] = '\0';

            /* Felder: ts mac ip hostname [client-id] */
            char *saveptr = NULL;
            /*ts*/  strtok_r(buf, " \t", &saveptr);
            char *m = strtok_r(NULL, " \t", &saveptr);
            /*ip*/  strtok_r(NULL, " \t", &saveptr);
            char *h = strtok_r(NULL, " \t", &saveptr);

            if (m && h && strcasecmp(m, mac) == 0 && strcmp(h, "*") != 0) {
                strncpy(hostname, h, hostname_size - 1);
                hostname[hostname_size - 1] = '\0';
                return;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
}

esp_err_t womo_router_get_ap_status(womo_router_ap_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /*
     * RUTX11 AP-Interface per iwinfo info abfragen.
     * Das AP-Interface hat mode=="Master".
     * RUTX11 hat Dual-Band → es können MEHRERE Master-Interfaces existieren
     * (z.B. wlan0 für 2.4 GHz, wlan1 für 5 GHz).
     * Wir sammeln alle und fragen deren assoclist ab.
     */

    #define AP_DEV_MAX 4
    char ap_devices[AP_DEV_MAX][16];
    uint8_t ap_dev_count = 0;

    /* Zuerst alle Interfaces durchgehen und ALLE Master-Interfaces sammeln */
    cJSON *dev_args = cJSON_CreateObject();
    cJSON *dev_data = NULL;
    esp_err_t err = ubus_call("iwinfo", "devices", dev_args, NULL, &dev_data);
    cJSON_Delete(dev_args);

    if (err == ESP_OK && dev_data) {
        cJSON *devices = cJSON_GetObjectItem(dev_data, "devices");
        if (cJSON_IsArray(devices)) {
            cJSON *dev = NULL;
            cJSON_ArrayForEach(dev, devices) {
                if (!cJSON_IsString(dev)) continue;

                cJSON *info_args = cJSON_CreateObject();
                cJSON_AddStringToObject(info_args, "device", dev->valuestring);
                cJSON *info_data = NULL;
                esp_err_t ie = ubus_call("iwinfo", "info", info_args, NULL, &info_data);
                cJSON_Delete(info_args);

                if (ie == ESP_OK && info_data) {
                    /* KOMPLETTE JSON-Antwort loggen (nur bei DEBUG) */
                    char *json_str = cJSON_Print(info_data);
                    if (json_str) {
                        ESP_LOGD(TAG, "iwinfo info for '%s': %s", dev->valuestring, json_str);
                        free(json_str);
                    }
                    
                    cJSON *mode = cJSON_GetObjectItem(info_data, "mode");
                    if (mode && cJSON_IsString(mode) &&
                        strcasecmp(mode->valuestring, "Master") == 0) {
                        /* AP-Interface gefunden */
                        cJSON *ch = cJSON_GetObjectItem(info_data, "channel");
                        uint8_t channel = 0;
                        if (ch && cJSON_IsNumber(ch)) {
                            channel = (uint8_t)ch->valueint;
                        }
                        
                        /* hwmode als Fallback: "11a" / "11na" / "11ac" = 5GHz, sonst 2.4GHz */
                        cJSON *hwmode = cJSON_GetObjectItem(info_data, "hwmode");
                        const char *hwmode_str = (hwmode && cJSON_IsString(hwmode)) ? hwmode->valuestring : NULL;
                        
                        ESP_LOGI(TAG, "AP Interface '%s': channel=%u, hwmode='%s'", 
                                 dev->valuestring, channel, hwmode_str ? hwmode_str : "?");
                        
                        /* Band erkennen: 1. Versuch über Kanal, 2. Versuch über hwmode */
                        if (channel > 0 && channel <= 14) {
                            out->band_2_4ghz_active = true;
                            ESP_LOGI(TAG, "  -> 2.4 GHz band detected (via channel)");
                        } else if (channel > 14) {
                            out->band_5ghz_active = true;
                            ESP_LOGI(TAG, "  -> 5 GHz band detected (via channel)");
                        } else if (hwmode_str) {
                            /* hwmode als Fallback */
                            if (strstr(hwmode_str, "11a") || strstr(hwmode_str, "11ac") || 
                                strstr(hwmode_str, "11ax") || strstr(hwmode_str, "11na")) {
                                out->band_5ghz_active = true;
                                ESP_LOGI(TAG, "  -> 5 GHz band detected (via hwmode '%s')", hwmode_str);
                            } else {
                                out->band_2_4ghz_active = true;
                                ESP_LOGI(TAG, "  -> 2.4 GHz band detected (via hwmode '%s')", hwmode_str);
                            }
                        } else {
                            ESP_LOGW(TAG, "  -> Cannot determine band (channel=0, no hwmode)");
                        }
                        
                        if (!out->enabled) {
                            /* SSID + Channel vom ersten AP übernehmen */
                            out->enabled = true;
                            json_strcpy(out->ssid, sizeof(out->ssid), info_data, "ssid");
                            out->channel = channel;
                        }
                        if (ap_dev_count < AP_DEV_MAX) {
                            strncpy(ap_devices[ap_dev_count], dev->valuestring,
                                    sizeof(ap_devices[0]) - 1);
                            ap_devices[ap_dev_count][sizeof(ap_devices[0]) - 1] = '\0';
                            ap_dev_count++;
                        }
                    }
                    cJSON_Delete(info_data);
                }
            }
        }
        cJSON_Delete(dev_data);
    } else {
        if (dev_data) cJSON_Delete(dev_data);
    }

    if (!out->enabled) {
        /* Kein Master-Interface gefunden – AP vermutlich deaktiviert.
         * SSID trotzdem aus UCI lesen, damit der Name angezeigt werden kann. */
        cJSON *uci_data = NULL;
        err = womo_router_uci_get("wireless", "wifi_iface_0", "ssid", (void **)&uci_data);
        if (err == ESP_OK && uci_data) {
            cJSON *val = cJSON_GetObjectItem(uci_data, "value");
            if (val && cJSON_IsString(val)) {
                strncpy(out->ssid, val->valuestring, sizeof(out->ssid) - 1);
            }
            cJSON_Delete(uci_data);
        }
        return ESP_OK;
    }

    /* ── Client-Liste per iwinfo assoclist (alle AP-Interfaces) ── */
    uint8_t total_idx = 0;
    for (uint8_t d = 0; d < ap_dev_count; d++) {
        cJSON *assoc_args = cJSON_CreateObject();
        cJSON_AddStringToObject(assoc_args, "device", ap_devices[d]);
        cJSON *assoc_data = NULL;
        esp_err_t ae = ubus_call("iwinfo", "assoclist", assoc_args, NULL, &assoc_data);
        cJSON_Delete(assoc_args);

        if (ae == ESP_OK && assoc_data) {
            cJSON *results = cJSON_GetObjectItem(assoc_data, "results");
            if (cJSON_IsArray(results)) {
                cJSON *entry = NULL;
                cJSON_ArrayForEach(entry, results) {
                    if (total_idx >= WOMO_AP_CLIENT_MAX) break;
                    cJSON *mac_j = cJSON_GetObjectItem(entry, "mac");
                    if (mac_j && cJSON_IsString(mac_j)) {
                        strncpy(out->client_list[total_idx].mac,
                                mac_j->valuestring,
                                sizeof(out->client_list[total_idx].mac) - 1);
                        cJSON *sig = cJSON_GetObjectItem(entry, "signal");
                        if (sig && cJSON_IsNumber(sig)) {
                            out->client_list[total_idx].signal_dbm = (int8_t)sig->valueint;
                        }
                        total_idx++;
                    }
                }
            }
            cJSON_Delete(assoc_data);
        }
        if (total_idx >= WOMO_AP_CLIENT_MAX) break;
    }
    out->clients = total_idx;

    /* ── Hostnamen aus DHCP-Leases auflösen ─────────────── */
    if (out->clients > 0) {
        char leases_buf[1024] = {0};
        const char *cat_params[] = {"-c", "cat /tmp/dhcp.leases"};
        esp_err_t le = womo_router_exec("/bin/sh", cat_params, 2,
                                         leases_buf, sizeof(leases_buf));
        if (le == ESP_OK && leases_buf[0]) {
            for (uint8_t i = 0; i < out->clients; i++) {
                resolve_hostname_from_leases(leases_buf,
                                              out->client_list[i].mac,
                                              out->client_list[i].hostname,
                                              sizeof(out->client_list[i].hostname));
            }
        }
    }

    ESP_LOGI(TAG, "AP Status: '%s' ch%d %u Clients (über %u Interfaces)",
             out->ssid, out->channel, out->clients, ap_dev_count);
    for (uint8_t i = 0; i < out->clients; i++) {
        ESP_LOGI(TAG, "  Client %u: %s (%s) %d dBm",
                 i, out->client_list[i].hostname[0] ? out->client_list[i].hostname : "?",
                 out->client_list[i].mac, out->client_list[i].signal_dbm);
    }
    return ESP_OK;
    #undef AP_DEV_MAX
}

/* ── High-Level: LTE ein-/ausschalten ────────────────────── */

esp_err_t womo_router_lte_enable(bool enable)
{
    cJSON *values = cJSON_CreateObject();
    cJSON_AddStringToObject(values, "disabled", enable ? "0" : "1");

    esp_err_t err = womo_router_uci_set("network", "mob1s1a1", values);
    cJSON_Delete(values);

    if (err != ESP_OK) return err;

    err = womo_router_uci_commit("network");
    if (err != ESP_OK) return err;

    const char *p[] = {};
    womo_router_exec("/sbin/reload_config", p, 0, NULL, 0);

    ESP_LOGI(TAG, "LTE %s", enable ? "aktiviert" : "deaktiviert");
    return ESP_OK;
}
