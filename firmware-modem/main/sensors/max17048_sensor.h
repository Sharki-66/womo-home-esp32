#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Momentaufnahme der letzten/aktuellen Messung
typedef struct {
	float voltage;       // Zellspannung in Volt
	float percent;       // State of Charge in Prozent
	int64_t timestamp_us; // Zeitstempel der Messung (esp_timer_get_time)
	bool valid;          // true, wenn Messung ok
} max17048_snapshot_t;

// Initialisiert I2C-Bus (Port/Pins aus modem_config.h), legt MAX17048-Handle an
// und startet ein periodisches Logging (Spannung/SoC) im Hintergrund.
esp_err_t max17048_app_start(void);

// Liest den Fuel-Gauge und liefert eine frische Momentaufnahme.
// Bei Fehlern wird, falls vorhanden, die letzte gültige Messung zurückgegeben.
esp_err_t max17048_app_get_snapshot(max17048_snapshot_t *out);

#ifdef __cplusplus
}
#endif
