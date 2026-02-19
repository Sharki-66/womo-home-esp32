/**
 * @file womo_http_mutex.h
 * @brief Globaler Mutex für HTTPS-Requests (TLS-Session-Serialisierung).
 *
 * Auf dem ESP32-S3 reicht der Heap nicht für parallele TLS-Handshakes.
 * Jeder HTTP(S)-Client (Weather, Geocode, Meteoalarm) muss diesen Mutex
 * halten, bevor er esp_http_client_perform() aufruft.
 *
 * Hinweis: Router-UCI (HTTP, kein TLS) braucht den Mutex NICHT.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Einmalig beim Start aufrufen (vor allen HTTP-Tasks). */
esp_err_t womo_http_mutex_init(void);

/** Mutex nehmen – blockiert bis frei. Gibt ESP_OK bei Erfolg zurück. */
esp_err_t womo_http_mutex_acquire(void);

/** Mutex freigeben. */
void womo_http_mutex_release(void);

#ifdef __cplusplus
}
#endif
