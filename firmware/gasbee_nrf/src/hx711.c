/*
 * Copyright (c) 2019 Ruslan V. Uss <unclerus@gmail.com>
 * Zephyr-Port: 2026 Hajo Harms
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of itscontributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * hx711.c – HX711-Treiber, Zephyr-Port von esp-idf-lib/hx711
 *
 * Timing wie im ESP-Original: 10 µs high / 10 µs low (~50 kHz, langes Kabel).
 * Datenblatt: PD_SCK high max. 50 µs, sonst nach >60 µs Power-Down.
 * Deshalb läuft der Lesezyklus unter irq_lock().
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include "hx711.h"

#define HX711_SCK_HALF_PERIOD_US 10

static uint32_t read_raw(const hx711_t *dev, hx711_gain_t gain)
{
	unsigned int key = irq_lock();

	// Daten lesen (MSB zuerst)
	uint32_t data = 0;
	for (int i = 0; i < 24; i++) {
		gpio_pin_set_dt(&dev->pd_sck, 1);
		k_busy_wait(HX711_SCK_HALF_PERIOD_US);
		data |= (uint32_t)(gpio_pin_get_dt(&dev->dout) & 1) << (23 - i);
		gpio_pin_set_dt(&dev->pd_sck, 0);
		k_busy_wait(HX711_SCK_HALF_PERIOD_US);
	}

	// Zusatzpulse: Kanal + Gain für die nächste Wandlung
	for (int i = 0; i <= (int)gain; i++) {
		gpio_pin_set_dt(&dev->pd_sck, 1);
		k_busy_wait(HX711_SCK_HALF_PERIOD_US);
		gpio_pin_set_dt(&dev->pd_sck, 0);
		k_busy_wait(HX711_SCK_HALF_PERIOD_US);
	}

	irq_unlock(key);

	return data;
}

int hx711_init(hx711_t *dev)
{
	if (!dev || !gpio_is_ready_dt(&dev->dout) || !gpio_is_ready_dt(&dev->pd_sck)) {
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&dev->dout, GPIO_INPUT);
	if (ret) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&dev->pd_sck, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}

	ret = hx711_power_down(dev, false);
	if (ret) {
		return ret;
	}

	return hx711_set_gain(dev, dev->gain);
}

int hx711_power_down(hx711_t *dev, bool down)
{
	int ret = gpio_pin_set_dt(&dev->pd_sck, down);
	k_msleep(1);
	return ret;
}

int hx711_set_gain(hx711_t *dev, hx711_gain_t gain)
{
	if (gain > HX711_GAIN_A_64) {
		return -EINVAL;
	}

	int ret = hx711_wait(dev, 200);
	if (ret) {
		return ret;
	}

	read_raw(dev, gain);
	dev->gain = gain;

	return 0;
}

int hx711_wait(hx711_t *dev, uint32_t timeout_ms)
{
	k_timepoint_t end = sys_timepoint_calc(K_MSEC(timeout_ms));

	while (!sys_timepoint_expired(end)) {
		if (gpio_pin_get_dt(&dev->dout) == 0) {
			return 0;
		}
		k_msleep(1);
	}

	return -EAGAIN;
}

int hx711_read_data(hx711_t *dev, int32_t *data)
{
	if (!data) {
		return -EINVAL;
	}

	uint32_t raw = read_raw(dev, dev->gain);
	if (raw & 0x800000) {
		raw |= 0xff000000;
	}
	*data = (int32_t)raw;

	return 0;
}
