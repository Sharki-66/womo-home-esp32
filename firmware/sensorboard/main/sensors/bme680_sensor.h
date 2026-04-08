/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	BME_CHIP_NONE = 0,
	BME_CHIP_BME680,
} bme_chip_type_t;

typedef enum {
	BME680_TREND_FALLING_FAST = -2,  // < -2.5 hPa/3h
	BME680_TREND_FALLING = -1,       // -2.5 bis -0.5 hPa/3h
	BME680_TREND_STEADY = 0,         // -0.5 bis +0.5 hPa/3h
	BME680_TREND_RISING = 1,         // +0.5 bis +2.5 hPa/3h
	BME680_TREND_RISING_FAST = 2     // > +2.5 hPa/3h
} bme680_pressure_trend_t;

typedef struct {
	bool valid;
	bme_chip_type_t chip;      // BME_CHIP_BME280 oder BME_CHIP_BME680
	float temperature_c;
	float humidity_pct;
	float pressure_hpa;
	float gas_kohm;
	bool gas_valid;
	bool heater_stable;
	int64_t timestamp_us;

	bool iaq_valid;
	float iaq;
	int iaq_accuracy;
	float eco2_ppm;
	float bvoc_ppm;
	float run_in;
	float stab;

	// Luftdruck-Trend (nur outdoor); 1h ab 4 Samples, 3h ab 12 Samples verfügbar
	bool press_trend_1h_valid;
	float press_trend_1h_hpa_h;
	bme680_pressure_trend_t press_trend_1h_state;

	bool press_trend_3h_valid;
	float press_trend_3h_hpa_h;
	bme680_pressure_trend_t press_trend_3h_state;
} bme680_reading_t;

typedef struct {
	bme680_reading_t indoor;
	bme680_reading_t outdoor;
	uint8_t indoor_addr;   // Physische I2C-Adresse (0x76 oder 0x77)
	uint8_t outdoor_addr;
} bme680_snapshot_t;

esp_err_t bme680_app_start(void);

esp_err_t bme680_app_get_snapshot(bme680_snapshot_t *out);

#ifdef __cplusplus
}
#endif
