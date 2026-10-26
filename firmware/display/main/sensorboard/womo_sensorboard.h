/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sensor data structure matching Sensorboard JSON output
typedef struct {
    // Timestamp
    uint64_t timestamp_ms;
    
    // BNO055 IMU
    struct {
        bool valid;
        float heading_deg;
        float roll_deg;
        float pitch_deg;
        char direction[4];  // "N", "NNO", "NO", etc.
        uint8_t cal_sys;
        uint8_t cal_gyro;
        uint8_t cal_accel;
        uint8_t cal_mag;
    } bno;
    
    // HX711 Load Cells
    struct {
        bool valid;
        bool nc;            // true: keine Waage angeschlossen
        float weight_a_kg;
        float weight_b_kg;
        float weight_sum_kg;
        bool is_wired;      // src "hx": Kabel-HX711 aktiv (Vorrang vor GasBee)
        bool is_gasbee;     // src "ble": Gewichte kommen von GasBee-Waagen (BLE)
        struct {
            bool present;      // GasBee dieser Flasche aktuell (Statusfelder empfangen)
            bool batt_valid;
            uint16_t batt_mv;
            uint8_t batt_pct;  // LiPo-Kennlinie, auf dem Sensorboard berechnet
            bool charging;     // Akku lädt (USB)
            bool calibrated;   // false: GasBee nutzt Firmware-Default-Kalibrierung
            bool config_mode;  // GasBee an USB (Konfigurationsmodus, Kalibrierung möglich)
        } bee[2];           // 0 = Flasche A, 1 = Flasche B (nur bei is_gasbee)
    } hx711;

    // Gas summary (Sensorboard)
    struct {
        bool valid;
        bool nc_a;           // true: keine Flasche auf Kanal A (Gewicht < Schwellwert)
        bool nc_b;           // true: keine Flasche auf Kanal B (Gewicht < Schwellwert)
        int8_t active_idx;   // 0=A/Vorne, 1=B/Hinten
        float net_kg;        // Netto der aktiven Flasche
        float rate_kgph_1h;  // Verbrauch kg/h (1h-Glättung)
        float rate_kgph_2h;  // Verbrauch kg/h (2h-Glättung)
        float rest_hours;    // Restlaufzeit in Stunden
        float net_a_kg;      // Netto Flasche A
        float net_b_kg;      // Netto Flasche B
        float cap_kg;        // Nennfüllmenge je Flasche (z. B. 11.0)
        float pct;           // Füllstand aktive Flasche (0-100%, swap-bereinigt)
        float pct_a;         // Füllstand Flasche A
        float pct_b;         // Füllstand Flasche B
    } gas;
    
    // BME680 Environmental (outdoor/default)
    struct {
        bool valid;
        bool is_ruuvi;     // true: Ruuvi-BLE-Fallback statt verkabeltem BME280/680
        bool batt_valid;   // true: Ruuvi-Batteriespannung vorhanden
        uint16_t batt_mv;  // Ruuvi-Batteriespannung (mV, nur wenn batt_valid)
        float temperature_c;
        float humidity_percent;
        float pressure_hpa;
        float gas_kohm;
        uint16_t iaq;
        uint8_t iaq_accuracy;
        float eco2_ppm;
        float bvoc_ppm;
        char press_trend_state[12];
        float press_trend_slope_hpa_h;
        uint16_t press_trend_samples;
        uint16_t press_trend_window_min;
    } bme680;

    // BME680 Environmental (indoor, addr 0x76)
    struct {
        bool valid;
        bool iaq_valid;    // true nur wenn BME680 IAQ-Daten liefert (nicht bei Ruuvi-Fallback)
        bool is_ruuvi;     // true: Ruuvi-BLE-Fallback statt verkabeltem BME280/680
        bool batt_valid;   // true: Ruuvi-Batteriespannung vorhanden
        uint16_t batt_mv;  // Ruuvi-Batteriespannung (mV, nur wenn batt_valid)
        float temperature_c;
        float humidity_percent;
        float pressure_hpa;
        float gas_kohm;
        uint16_t iaq;
        uint8_t iaq_accuracy;
        float eco2_ppm;
        float bvoc_ppm;
        char press_trend_state[12];
        float press_trend_slope_hpa_h;
        uint16_t press_trend_samples;
        uint16_t press_trend_window_min;
    } bme680_indoor;
    
    // Battery voltages
    struct {
        bool valid;
        bool nc1;
        bool nc2;
        float battery1_v;
        float battery2_v;
    } battery;
    
    // Tank levels
    struct {
        bool valid;
        uint8_t tank1_percent;
        uint8_t tank2_percent;
        bool nc1;
        bool nc2;
        float tank1_liters;
        float tank2_liters;
        float tank1_rate1h;  // L/h (1h-Fenster, negativ=Verbrauch)
        float tank1_rate2h;  // L/h (2h-Fenster)
        float tank1_rest_h;  // Restlaufzeit Stunden
        float tank2_rate1h;  // L/h (1h-Fenster, positiv=Füllrate)
        float tank2_rate2h;  // L/h (2h-Fenster)
        float tank2_rest_h;  // Zeit bis voll Stunden
    } tank;
    
    // GPS/GNSS
    struct {
        bool valid;
        double latitude;
        double longitude;
        double altitude_m;
        float speed_kmh;
        float heading_deg;
        uint8_t satellites;
        float confidence_m;
        uint32_t ttf_ms;      // Time to fix (ms)
        int64_t ts;           // GNSS/UTC epoch seconds from modem
        int64_t last_fix_us;
    } gps;

    // LTE modem status
    struct {
        bool valid;
        bool registered;
        char operator_name[32];
        float rsrp_dbm;
        uint8_t signal_percent;
    } lte;

    // Power / control status (from modem ctrl block)
    struct {
        bool valid;
        bool pwr_12v_on;       // 12V Bordnetz aktiv
        bool radio_on;         // Multimedia/Radio aktiv (nur wenn 12V an)
        bool ac_present;       // 230V Landstrom angeschlossen
        bool sb_eco;           // Sensorboard im Eco-Modus (ctrl "eco")
        bool rtc_bat_low;      // BLF: RTC-Backup-Batterie schwach (<~1.2V)
        bool rtc_bat_switched; // BSF: Seit letztem Reset mind. 1× auf RTC-Batterie gelaufen
        bool buzzer_on;        // Globale Systemtöne aktiv
        bool touch_click_on;   // Touch-Klick-Ton aktiv
    } power;

    // WLAN-Verbindung des Sensorboards (aus ctrl: ssid, rssi, wifi_trial)
    struct {
        bool valid;            // ctrl mit Feld "ssid" empfangen
        bool connected;        // Sensorboard hat eine WLAN-Verbindung
        bool trial;            // Sensorboard testet gerade neue Zugangsdaten
        char ssid[33];
        int8_t rssi;
        int64_t rx_us;         // esp_timer-Zeit des letzten ctrl mit diesen Feldern
    } sb_wifi;

    // INA226 Strom-/Leistungsmessung Hauptleitung
    struct {
        bool valid;
        bool nc;           // INA226 nicht eingebaut
        float v_bus_v;     // Busspannung (V)
        float i_a;         // Strom (A, positiv = Verbrauch)
        float p_w;         // Leistung (W)
    } elec;

    // Per-Topic Empfangszeitstempel (esp_timer_get_time(), 0 = noch nie empfangen)
    int64_t bat_topic_rx_us;
    int64_t tank_topic_rx_us;
    int64_t gas_topic_rx_us;
    int64_t bme_topic_rx_us;
    int64_t elec_topic_rx_us;
} womo_sensor_data_t;

// Ruuvi-Scan-Ergebnis (Auswahlliste für Konfiguration, ESP-NOW-Frame "ruuvi_scan")
#define WOMO_RUUVI_SCAN_MAX_TAGS 8

typedef struct {
    char     mac[18];       // "AA:BB:CC:DD:EE:FF"
    int8_t   rssi;
    float    temp_c;
    bool     has_pressure;
    uint32_t age_s;
} womo_ruuvi_scan_entry_t;

// GasBee-Scan-Ergebnis (Auswahlliste für Konfiguration, Frame "gasbee_scan")
#define WOMO_GASBEE_SCAN_MAX_TAGS 8

typedef struct {
    char     mac[18];       // "AA:BB:CC:DD:EE:FF"
    int8_t   rssi;
    bool     weight_valid;
    float    weight_kg;     // Bruttogewicht (nur wenn weight_valid)
    bool     batt_valid;
    uint16_t batt_mv;
    uint8_t  batt_pct;
    bool     charging;
    bool     calibrated;
    bool     config_mode;   // GasBee an USB (Konfigurationsmodus)
    uint8_t  seq;           // Messzyklus der GasBee (bee_seq), steigt bei neuer Messung
    uint32_t age_s;
} womo_gasbee_scan_entry_t;

// Callback für empfangene Sensordaten
typedef void (*womo_sensorboard_data_cb_t)(const womo_sensor_data_t *data, void *user_data);

// Callback für eine per ruuvi_scan_request angeforderte Ruuvi-Scan-Liste
typedef void (*womo_ruuvi_scan_cb_t)(const womo_ruuvi_scan_entry_t *entries, size_t count, void *user_data);

// Callback für eine per gasbee_scan_request angeforderte GasBee-Scan-Liste
typedef void (*womo_gasbee_scan_cb_t)(const womo_gasbee_scan_entry_t *entries, size_t count, void *user_data);

typedef enum {
    WOMO_SENSORBOARD_EVENT_INVALID_JSON = 0,
} womo_sensorboard_event_t;

typedef void (*womo_sensorboard_event_cb_t)(womo_sensorboard_event_t event, void *user_data);

// Sensorboard-Kommunikation initialisieren (ESP-NOW)
esp_err_t womo_sensorboard_init(void);

// Sensordaten-Callback setzen
void womo_sensorboard_set_data_callback(womo_sensorboard_data_cb_t callback, void *user_data);

// Optionaler Event-Callback (Handshake, Heartbeat, Parse-Fehler)
void womo_sensorboard_set_event_callback(womo_sensorboard_event_cb_t callback, void *user_data);

// Callback für Ruuvi-Scan-Ergebnisse (Antwort auf womo_sensorboard_send_ruuvi_scan_request)
void womo_sensorboard_set_ruuvi_scan_callback(womo_ruuvi_scan_cb_t callback, void *user_data);

// Callback für GasBee-Scan-Ergebnisse (Antwort auf womo_sensorboard_send_gasbee_scan_request)
void womo_sensorboard_set_gasbee_scan_callback(womo_gasbee_scan_cb_t callback, void *user_data);

// Ergebnis eines GasBee-Kalibrierschritts (Frame "gasbee_cal_result").
// err: 0 = ok, > 0 ATT-Fehlercode der GasBee, < 0 Fehler im Sensorboard; msg = Klartext
typedef void (*womo_gasbee_cal_result_cb_t)(bool zero_step, bool ok, int err, const char *msg, void *user_data);
void womo_sensorboard_set_gasbee_cal_result_callback(womo_gasbee_cal_result_cb_t callback, void *user_data);

// Kommandos an Sensorboard senden
esp_err_t womo_sensorboard_send_tare_a(void);
esp_err_t womo_sensorboard_send_tare_b(void);
esp_err_t womo_sensorboard_send_wifi_control(bool enable, const char *ssid, const char *password);
esp_err_t womo_sensorboard_send_lte_control(bool enable);
esp_err_t womo_sensorboard_send_wifi_credentials(const char *ssid, const char *pass);
// Blockierend: sendet wifi_config bis zu attempts Mal, bis ein cmd_ack kommt
// (je WOMO_SENSOR_COMMAND_TIMEOUT_MS). ESP_OK = ok-ACK, ESP_FAIL = Fehler-ACK,
// ESP_ERR_TIMEOUT = kein ACK. Nicht aus dem LVGL-Task aufrufen.
esp_err_t womo_sensorboard_send_wifi_credentials_wait(const char *ssid, const char *pass, int attempts);
esp_err_t womo_sensorboard_send_ruuvi_config(const char *indoor_mac, const char *outdoor_mac);
esp_err_t womo_sensorboard_send_ruuvi_scan_request(void);
esp_err_t womo_sensorboard_send_gasbee_config(const char *bottle_a_mac, const char *bottle_b_mac);
esp_err_t womo_sensorboard_send_gasbee_scan_request(void);
// Einrichten: nur GasBees im Konfigurationsmodus (an USB) melden lassen
esp_err_t womo_sensorboard_send_gasbee_setup_scan(void);
// Kalibrier-Sitzung je Flasche (0 = A, 1 = B): Sensorboard meldet die Flasche solange als nc
esp_err_t womo_sensorboard_send_gasbee_cal_session(int slot, bool start);
// Kalibrierschritt per GATT: zero = Nullpunkt, sonst Referenzgewicht grams
esp_err_t womo_sensorboard_send_gasbee_cal(const char *mac, bool zero, uint32_t grams);
esp_err_t womo_sensorboard_send_gas_bottle_replace(uint8_t slot, const char *channel);
esp_err_t womo_sensorboard_send_pwr_12v(bool enable);
/* Eco-Wunsch (Hintergrundlicht aus) – geht mit jedem hb ans Sensorboard, bei Änderung sofort.
 * Das Sensorboard spart nur ohne Landstrom, Ergebnis in power.sb_eco. */
void womo_sensorboard_set_eco(bool eco);
esp_err_t womo_sensorboard_send_radio(bool enable);
esp_err_t womo_sensorboard_send_imu_zero(void);
esp_err_t womo_sensorboard_send_buzzer(bool enable);
esp_err_t womo_sensorboard_send_touch_click(bool enable);
esp_err_t womo_sensorboard_send_buzzer_alert(bool is_alarm); /* One-Shot: true=Alarm, false=Warnung */

// Letzte Sensordaten lesen (nicht-blockierend)
bool womo_sensorboard_get_latest_data(womo_sensor_data_t *data);

#ifdef __cplusplus
}
#endif
