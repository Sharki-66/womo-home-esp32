#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert ADC1 mit 12 Bit und 12 dB Attenuation.
esp_err_t analog_init(void);

// Liest einen ADC1-Kanal (0-9) als Rohwert.
esp_err_t analog_read_raw(int channel, int *raw_out);

// Liest und konvertiert einen ADC1-Kanal in Millivolt.
// Spannungsteiler 82kΩ/15kΩ: Faktor 97/15 wird intern angewendet.
esp_err_t analog_read_mv(int channel, int *mv_out);

#ifdef __cplusplus
}
#endif
