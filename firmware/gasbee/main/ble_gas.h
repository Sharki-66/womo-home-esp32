#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BLE GATT Server für GasBee initialisieren und Advertising starten.
 *        Muss nach nvs_flash_init() und esp_nimble_hci_init() aufgerufen werden.
 */
esp_err_t ble_gas_init(void);

/**
 * @brief Messwerte aktualisieren und an alle verbundenen Clients notifizieren.
 *
 * @param weight_kg    Rohgewicht der Flasche (inkl. Tara) in kg
 * @param net_gas_kg   Netto-Gasgewicht (weight_kg - Tara) in kg
 * @param gas_pct      Füllstand 0–100 %
 */
void ble_gas_notify(float weight_kg, float net_gas_kg, uint8_t gas_pct);

/**
 * @brief Callback-Typ: wird aufgerufen wenn Display/App einen Tare-Befehl schickt.
 */
typedef void (*ble_gas_tare_cb_t)(void);

/**
 * @brief Tare-Callback registrieren.
 */
void ble_gas_set_tare_callback(ble_gas_tare_cb_t cb);

#ifdef __cplusplus
}
#endif
