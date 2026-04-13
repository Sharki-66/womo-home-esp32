/**
 * HTTP-Server für WoMoHome Sensorboard.
 *
 * Serviert womo_dashboard.html aus SPIFFS und stellt JSON-Endpunkte bereit.
 * Wird beim Boot gestartet und läuft dauerhaft.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * SPIFFS mounten und HTTP-Server auf SENSOR_HTTP_PORT starten.
 * Registrierte Routen:
 *   GET /             → womo_dashboard.html (800×480 Gesamt-Dashboard)
 *   GET /horizon.html → Parkhilfe (Künstlicher Horizont, BNO055)
 *   GET /api/imu      → JSON mit IMU-Daten (BNO055)
 *   GET /api/status   → JSON mit allen Sensordaten (IMU, BME680, HX711, Bat, Tank)
 *   GET /...          → statische Dateien aus SPIFFS
 */
esp_err_t sensor_http_start(void);

/**
 * HTTP-Server stoppen (SPIFFS bleibt gemountet).
 */
esp_err_t sensor_http_stop(void);

/**
 * Gibt true zurück wenn der HTTP-Server läuft.
 */
bool sensor_http_is_running(void);
