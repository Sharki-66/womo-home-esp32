#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	bool valid;
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
} bme680_reading_t;

typedef struct {
	bme680_reading_t indoor;   // I2C 0x76
	bme680_reading_t outdoor;  // I2C 0x77
} bme680_snapshot_t;

esp_err_t bme680_app_start(void);

esp_err_t bme680_app_get_snapshot(bme680_snapshot_t *out);

#ifdef __cplusplus
}
#endif
