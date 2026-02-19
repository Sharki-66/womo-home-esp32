#pragma once

#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcf8523_app_start(void);
esp_err_t pcf8523_app_set_time(time_t utc_time);
esp_err_t pcf8523_app_get_time(time_t *utc_time);

#ifdef __cplusplus
}
#endif
