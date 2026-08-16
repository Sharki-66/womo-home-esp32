/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
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
        bool rtc_bat_low;      // BLF: RTC-Backup-Batterie schwach (<~1.2V)
        bool rtc_bat_switched; // BSF: Seit letztem Reset mind. 1× auf RTC-Batterie gelaufen
        bool buzzer_on;        // Globale Systemtöne aktiv
        bool touch_click_on;   // Touch-Klick-Ton aktiv
    } power;

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

// Callback für empfangene Sensordaten
typedef void (*womo_sensorboard_data_cb_t)(const womo_sensor_data_t *data, void *user_data);

typedef enum {
    WOMO_SENSORBOARD_EVENT_HELLO = 0,
    WOMO_SENSORBOARD_EVENT_HEARTBEAT,
    WOMO_SENSORBOARD_EVENT_INVALID_JSON,
} womo_sensorboard_event_t;

typedef void (*womo_sensorboard_event_cb_t)(womo_sensorboard_event_t event, void *user_data);

// Sensorboard-Kommunikation initialisieren (ESP-NOW)
esp_err_t womo_sensorboard_init(void);

// Sensordaten-Callback setzen
void womo_sensorboard_set_data_callback(womo_sensorboard_data_cb_t callback, void *user_data);

// Optionaler Event-Callback (Handshake, Heartbeat, Parse-Fehler)
void womo_sensorboard_set_event_callback(womo_sensorboard_event_cb_t callback, void *user_data);

// Kommandos an Sensorboard senden
esp_err_t womo_sensorboard_send_level_start(void);
esp_err_t womo_sensorboard_send_level_stop(void);
esp_err_t womo_sensorboard_send_tare_a(void);
esp_err_t womo_sensorboard_send_tare_b(void);
esp_err_t womo_sensorboard_send_wifi_control(bool enable, const char *ssid, const char *password);
esp_err_t womo_sensorboard_send_lte_control(bool enable);
esp_err_t womo_sensorboard_send_wifi_credentials(const char *ssid, const char *pass);
esp_err_t womo_sensorboard_send_gas_bottle_replace(uint8_t slot, const char *channel);
esp_err_t womo_sensorboard_send_pwr_12v(bool enable);
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
