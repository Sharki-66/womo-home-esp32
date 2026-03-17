/*
 * WoMo Router UCI Client
 *
 * Steuert den RUTX11-Router über JSON-RPC / ubus.
 * Alle Aufrufe sind synchron und thread-safe (interner Mutex).
 *
 * Typischer Ablauf:
 *   womo_router_uci_init()          – einmalig beim Start
 *   womo_router_uci_ensure_session() – automatisch bei jedem API-Call
 *   womo_router_get_wifi_status()    – High-Level Abfragen
 */

#ifndef WOMO_ROUTER_UCI_H
#define WOMO_ROUTER_UCI_H

/* Standard-IP des RUTX11 – via Kconfig überschreibbar */
#ifndef CONFIG_WOMO_ROUTER_IP
#define CONFIG_WOMO_ROUTER_IP "192.168.10.1"
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Ergebnis-Strukturen ──────────────────────────────────── */

/** WiFi-Client-Status des Routers (wifi0 / STA-Interface) */
typedef struct {
    bool  enabled;                /**< Radio aktiv (UCI disabled=0) */
    bool  connected;              /**< STA assoziiert?            */
    char  ssid[33];               /**< Aktuell verbundenes SSID   */
    int8_t rssi;                  /**< Signalstärke in dBm        */
    uint8_t signal_percent;       /**< 0-100 %                    */
    char  mode[16];               /**< z.B. "Master", "Client"    */
    char  encryption[32];         /**< z.B. "WPA2 PSK (CCMP)"     */
    uint32_t bitrate_mbps;        /**< TX-Bitrate in Mbit/s       */
    char  bssid[18];              /**< AP-BSSID (XX:XX:XX:…)      */
    uint8_t channel;              /**< Aktueller Kanal            */
} womo_router_wifi_status_t;

/** LTE/Mobilfunk-Status des Routers */
typedef struct {
    bool  registered;             /**< Im Netz registriert?       */
    char  operator_name[32];      /**< z.B. "Vodafone"           */
    int   rssi_dbm;               /**< RSSI in dBm               */
    uint8_t signal_percent;       /**< 0-100 %                    */
    char  conn_type[16];          /**< "4G", "3G", …             */
    char  sim_state[16];          /**< "inserted", "not inserted" */
} womo_router_lte_status_t;

/** GPS-Position vom Router */
typedef struct {
    bool   valid;                 /**< Fix vorhanden?             */
    double latitude;
    double longitude;
    double altitude_m;
    double speed_kmh;
    double heading;               /**< Kurs in Grad (gpsctl -g)  */
    double accuracy_m;            /**< Genauigkeit in m (-u)      */
    int    fix_status;            /**< Fix-Status (-s): 0=kein, 2=2D, 3=3D */
    int    satellites;
    char   utc_time[24];          /**< "YYYY-MM-DD HH:MM:SS" (-e) */
} womo_router_gps_t;

/** Einzelnes Scan-Ergebnis beim WiFi-Scan des Routers */
typedef struct {
    char   ssid[33];
    char   bssid[18];
    int8_t rssi;
    uint8_t channel;
    char   encryption[32];
} womo_router_scan_result_t;

/* ── Initialisierung & Session ────────────────────────────── */

/**
 * @brief Modul initialisieren (Mutex anlegen, kein Login).
 *        Einmal aus app_main() aufrufen, NACHDEM WiFi verbunden ist.
 */
esp_err_t womo_router_uci_init(void);

/**
 * @brief Session sicherstellen (Login bei Bedarf / Refresh).
 *        Wird intern von allen API-Funktionen aufgerufen.
 * @return ESP_OK wenn Session gültig
 */
esp_err_t womo_router_uci_ensure_session(void);

/**
 * @brief Aktive Session explizit beenden.
 */
void womo_router_uci_logout(void);

/* ── Low-Level UCI / ubus ────────────────────────────────── */

/**
 * @brief UCI-Konfiguration lesen.
 *
 * @param config  UCI config name  (z.B. "wireless")
 * @param section UCI section name (z.B. "wifi_iface_0") oder NULL für alle
 * @param option  UCI option name  (z.B. "ssid") oder NULL für ganze Section
 * @param out_json  Zeiger auf cJSON*, wird bei Erfolg gesetzt (Caller muss cJSON_Delete!)
 */
esp_err_t womo_router_uci_get(const char *config,
                               const char *section,
                               const char *option,
                               void **out_json);

/**
 * @brief UCI-Wert setzen.
 *
 * @param config  UCI config name
 * @param section UCI section name
 * @param values_json  cJSON-Objekt mit key:value-Paaren
 */
esp_err_t womo_router_uci_set(const char *config,
                               const char *section,
                               const void *values_json);

/**
 * @brief UCI-Änderungen committen und anwenden.
 */
esp_err_t womo_router_uci_commit(const char *config);

/**
 * @brief Shell-Kommando auf dem Router ausführen.
 *
 * @param command    Pfad/Name (z.B. "/usr/sbin/gsmctl")
 * @param params     Array von Argument-Strings
 * @param param_count  Anzahl Argumente
 * @param out_stdout   Buffer für stdout-Ausgabe (kann NULL sein)
 * @param stdout_size  Größe des Buffers
 * @return ESP_OK bei Erfolg (exit-code 0 vom Router)
 */
esp_err_t womo_router_exec(const char *command,
                            const char **params,
                            int param_count,
                            char *out_stdout,
                            size_t stdout_size);

/* ── High-Level Abfragen ─────────────────────────────────── */

/**
 * @brief WiFi-Client-Status des Routers abfragen (iwinfo + UCI).
 */
esp_err_t womo_router_get_wifi_status(womo_router_wifi_status_t *out);

/**
 * @brief LTE-/Mobilfunk-Status abfragen (gsmctl).
 */
esp_err_t womo_router_get_lte_status(womo_router_lte_status_t *out);

/**
 * @brief GPS-Position vom Router abfragen (gpsctl).
 */
esp_err_t womo_router_get_gps(womo_router_gps_t *out);

/**
 * @brief WiFi-Scan am Router starten und Ergebnisse liefern.
 *
 * @param results      Array für Ergebnisse
 * @param max_results  Max. Einträge im Array
 * @param out_count    Tatsächlich gefundene Netze
 */
esp_err_t womo_router_wifi_scan(womo_router_scan_result_t *results,
                                 size_t max_results,
                                 size_t *out_count);

/**
 * @brief SSID + Passwort des Router WiFi-Clients (STA) setzen.
 *        Ruft UCI set + commit + reload_config auf.
 *
 * @param ssid       Ziel-SSID
 * @param password   WPA-Passwort (NULL = offen)
 * @param encryption Verschlüsselungstyp (z.B. "psk2") oder NULL für auto
 */
esp_err_t womo_router_wifi_set_sta(const char *ssid,
                                    const char *password,
                                    const char *encryption);

/**
 * @brief Router WiFi-Client (STA) ein-/ausschalten.
 */
esp_err_t womo_router_wifi_enable_sta(bool enable);

#define WOMO_AP_CLIENT_MAX 8

/** Einzelner AP-Client (assoziiertes Gerät am Router-HotSpot) */
typedef struct {
    char  hostname[33];           /**< Hostname aus DHCP-Lease    */
    char  mac[18];                /**< "AA:BB:CC:DD:EE:FF"        */
    int8_t signal_dbm;            /**< RSSI in dBm                */
} womo_ap_client_info_t;

/** AP-Status des Routers (HotSpot / SoftAP) */
typedef struct {
    bool  enabled;                /**< AP aktiv?                   */
    char  ssid[33];               /**< AP-SSID (z.B. "Malibu-622") */
    uint8_t channel;              /**< Kanal (erstes AP-Interface) */
    uint8_t clients;              /**< Anzahl verbundener Clients  */
    womo_ap_client_info_t client_list[WOMO_AP_CLIENT_MAX];
    bool  band_2_4ghz_active;     /**< 2.4 GHz Band aktiv?         */
    bool  band_5ghz_active;       /**< 5 GHz Band aktiv?           */
} womo_router_ap_status_t;

/**
 * @brief AP-Status des Routers abfragen (iwinfo + UCI).
 */
esp_err_t womo_router_get_ap_status(womo_router_ap_status_t *out);

/**
 * @brief Router LTE/Mobilfunk ein-/ausschalten.
 */
esp_err_t womo_router_lte_enable(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* WOMO_ROUTER_UCI_H */
