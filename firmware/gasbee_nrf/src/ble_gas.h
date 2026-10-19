/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * ble_gas.h – BLE GATT Server für GasBee (Zephyr Bluetooth)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Bluetooth starten, GATT-Service registrieren, Advertising-Set anlegen. */
int ble_gas_init(void);

/** Werte aktualisieren und an abonnierte Clients senden. */
void ble_gas_notify(float weight_kg, float net_gas_kg, uint8_t gas_pct);

/** Akkuspannung aktualisieren und senden (0x2B04, uint16 mV). */
void ble_gas_notify_battery(uint16_t bat_mv);

/**
 * Advertising-Format v1 setzen und einen Burst starten
 * (GASBEE_ADV_BURST_COUNT Pakete), im Konfigurationsmodus Dauersenden.
 * Der Sequenzzähler zählt intern hoch.
 * Bei bestehender Verbindung wird kein Burst gestartet.
 *
 * @param weight_10g Bruttogewicht in 10 g oder GASBEE_ADV_WEIGHT_INVALID
 * @param bat_mv     Akkuspannung in mV oder GASBEE_ADV_BAT_INVALID
 * @param flags      GASBEE_ADV_FLAG_*
 */
void ble_gas_adv_publish(int16_t weight_10g, uint16_t bat_mv, uint8_t flags);

/**
 * Konfigurationsmodus (USB/VBUS vorhanden) ein/aus.
 * Ein:  dauerhaft verbindbar senden (ab der nächsten Messung), Kalibrierung erlaubt.
 * Aus:  Advertising stoppen, Verbindungen trennen, danach wieder Burst-Modus.
 */
void ble_gas_set_config_mode(bool on);

typedef void (*ble_gas_tare_cb_t)(void);

/** Callback für den Tare-Befehl (Write 0x01 auf 0x2B10). */
void ble_gas_set_tare_callback(ble_gas_tare_cb_t cb);

/**
 * Kalibrier-Befehle über GATT. Die Callbacks laufen im BT-Kontext: nur
 * übernehmen und die Mess-Schleife wecken, nicht messen oder Flash schreiben.
 */
typedef struct {
	void (*zero)(void);                         // 0x2B11: Nullpunkt (Waage leer)
	void (*reference)(uint32_t grams);          // 0x2B12: bekanntes Gewicht aufgelegt
	void (*set)(int32_t offset, float scale);   // 0x2B13 Write: scale = 0 → löschen
	void (*get)(int32_t *offset, float *scale); // 0x2B13 Read
} ble_gas_cal_ops_t;

void ble_gas_set_cal_ops(const ble_gas_cal_ops_t *ops);
