#pragma once

#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	bool valid;
	double latitude;
	double longitude;
	double altitude_m;
	double speed_kph;
	double course_deg;
	double hdop;
	uint8_t sats_in_use;
	uint8_t sats_in_view;
	int64_t ts_us;       // Zeitpunkt des Fix (us seit Boot)
	time_t utc_time;     // UTC Sekunden falls vom Modem geliefert
} gnss_snapshot_t;

void gnss_task_start(void);

bool gnss_get_snapshot(gnss_snapshot_t *out);

#ifdef __cplusplus
}
#endif
