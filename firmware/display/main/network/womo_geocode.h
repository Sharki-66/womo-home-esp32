#ifndef WOMO_GEOCODE_H
#define WOMO_GEOCODE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    char short_name[64];
    char display_name[128];
} womo_geocode_result_t;

typedef void (*womo_geocode_callback_t)(const womo_geocode_result_t *result, void *user_data);

esp_err_t womo_geocode_reverse_request(double latitude,
                                       double longitude,
                                       const char *accept_language,
                                       womo_geocode_callback_t callback,
                                       void *user_data);

bool womo_geocode_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // WOMO_GEOCODE_H
