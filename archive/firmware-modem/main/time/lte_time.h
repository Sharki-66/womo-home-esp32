#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool valid;
	bool registered;
	uint8_t signal_percent;
	float rsrp_dbm;
	char operator_name[32];
	int64_t ts_us;
} lte_snapshot_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Startet LTE-Zeit-Synchronisations-Task
 * 
 * Task fragt periodisch AT+CCLK ab und aktualisiert System-Zeit via time_sync.
 * Wird automatisch deaktiviert wenn GPS-Zeit verfügbar ist (time_sync prüft das).
 * 
 * @return ESP_OK bei Erfolg
 */
esp_err_t lte_time_task_start(void);

bool lte_status_get_snapshot(lte_snapshot_t *out);

#ifdef __cplusplus
}
#endif
