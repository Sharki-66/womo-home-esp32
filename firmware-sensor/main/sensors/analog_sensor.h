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

// Liest und konvertiert einen ADC1-Kanal in Millivolt (per Vref 1100 mV Kalibrierung, falls verfügbar).
esp_err_t analog_read_mv(int channel, int *mv_out);

// Speziell für GPIO1 (ADC1_CH0) mit Spannungsteiler 200k/100k → Faktor 3 auf Eingangsspannung
esp_err_t analog_read_bat_gpio1_mv(int *mv_out);

// Startet periodisches Logging (alle 5s) der Batteriespannung an GPIO1
void analog_start_logging(void);

#ifdef __cplusplus
}
#endif
