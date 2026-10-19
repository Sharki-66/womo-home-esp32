/*
 * SPDX-FileCopyrightText: 2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * battery.c – Akkuspannung und Ladestatus
 *
 * Teiler und ADC-Kanal stehen in app.overlay (Knoten vbatt). Der Treiber
 * voltage-divider rechnet das Teilerverhältnis heraus und liefert VBAT.
 * Ladestatus: BQ25101 ~CHG an P0.17 (zephyr,user chg-gpios, aktiv = lädt).
 * USB: VBUS-Erkennung des nRF52840 (POWER.USBREGSTATUS), nur lesend – der
 *      USB-Stack des Boards bleibt unberührt.
 * Aufgerufen wird alles aus der Mess-Schleife in main.c.
 */

#include "battery.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

static const struct device *const s_vbatt = DEVICE_DT_GET(DT_NODELABEL(vbatt));
static const struct gpio_dt_spec s_chg = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), chg_gpios);

static bool s_chg_ready;

int battery_init(void)
{
	if (gpio_is_ready_dt(&s_chg) && gpio_pin_configure_dt(&s_chg, GPIO_INPUT) == 0) {
		s_chg_ready = true;
	} else {
		LOG_ERR("Ladestatus-Pin nicht verfügbar");
	}

	if (!device_is_ready(s_vbatt)) {
		LOG_ERR("voltage-divider nicht bereit");
		return -ENODEV;
	}
	return 0;
}

int battery_read_mv(uint16_t *mv_out)
{
	struct sensor_value val;

	int err = sensor_sample_fetch(s_vbatt);
	if (err) {
		return err;
	}
	err = sensor_channel_get(s_vbatt, SENSOR_CHAN_VOLTAGE, &val);
	if (err) {
		return err;
	}

	int32_t mv = val.val1 * 1000 + val.val2 / 1000;
	if (mv < 0) {
		mv = 0;
	}
	// 0xFFFF ist im Advertising "ungültig"
	*mv_out = (uint16_t)MIN(mv, UINT16_MAX - 1);
	return 0;
}

bool battery_usb_present(void)
{
	return nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
}

bool battery_is_charging(void)
{
	return s_chg_ready && gpio_pin_get_dt(&s_chg) == 1;
}
