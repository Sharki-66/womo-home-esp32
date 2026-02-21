#pragma once

/**
 * gasbee_ble_client.h
 *
 * NimBLE Central – verbindet mit GasBee (ESP32-C3) per BLE,
 * abonniert Notify-Characteristics und stellt Snapshot bereit.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float   weight_kg;    // Rohgewicht Flasche + Tara (kg)
    float   net_gas_kg;   // Netto-Gasgewicht (kg)
    uint8_t gas_pct;      // Füllstand (0–100 %)
    bool    valid;        // true sobald mind. eine Notify empfangen
    int64_t timestamp_us; // esp_timer_get_time() beim letzten Update
} gasbee_snapshot_t;

/**
 * @brief NimBLE-Host + Scan starten.
 *        Sucht dauerhaft nach "GasBee" und verbindet sich automatisch.
 *        Ruft intern nimble_port_init() auf – darf nur einmal aufgerufen werden.
 */
esp_err_t gasbee_ble_client_start(void);

/**
 * @brief Aktuellen Snapshot thread-safe auslesen.
 *        snapshot.valid = false solange keine Verbindung.
 */
void gasbee_ble_client_get_snapshot(gasbee_snapshot_t *out);

#ifdef __cplusplus
}
#endif
