/*
 * Copyright (c) 2019 Ruslan V. Uss <unclerus@gmail.com>
 * Zephyr-Port: 2026 Hajo Harms
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * hx711.h – HX711 24-bit ADC (Wägezellen), GPIO-Bitbang über Zephyr-GPIO-API
 *
 * Port von esp-idf-lib/hx711 (firmware/gasbee/components/hx711).
 * Liefert den vollen 24-bit-Rohwert (vorzeichenbehaftet).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>

typedef enum {
	HX711_GAIN_A_128 = 0, // Kanal A, Gain 128
	HX711_GAIN_B_32,      // Kanal B, Gain 32
	HX711_GAIN_A_64       // Kanal A, Gain 64
} hx711_gain_t;

typedef struct {
	struct gpio_dt_spec dout;
	struct gpio_dt_spec pd_sck;
	hx711_gain_t gain;
} hx711_t;

/** GPIOs konfigurieren, Power-Up, Gain setzen. -EAGAIN wenn HX711 nicht bereit. */
int hx711_init(hx711_t *dev);

/** PD_SCK high (>60 µs) = Power-Down, low = Power-Up. */
int hx711_power_down(hx711_t *dev, bool down);

/** Gain für die nächste Wandlung setzen (liest dafür einen Wert). */
int hx711_set_gain(hx711_t *dev, hx711_gain_t gain);

/** Wartet bis DOUT low (Daten bereit). -EAGAIN bei Timeout. */
int hx711_wait(hx711_t *dev, uint32_t timeout_ms);

/** Einen Rohwert lesen (vorher hx711_wait aufrufen). */
int hx711_read_data(hx711_t *dev, int32_t *data);
