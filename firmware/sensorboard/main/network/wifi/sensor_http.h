/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * HTTP-Server für Parkhilfe (Künstlicher Horizont).
 *
 * Serviert horizon.html aus SPIFFS und stellt /api/imu als JSON-Endpunkt bereit.
 * Wird beim Boot gestartet und läuft dauerhaft.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * SPIFFS mounten und HTTP-Server auf SENSOR_HTTP_PORT starten.
 * Registrierte Routen:
 *   GET /           → horizon.html
 *   GET /api/imu    → JSON mit IMU-Daten
 *   GET /...        → statische Dateien aus SPIFFS
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
