/*
 * SPDX-FileCopyrightText: 2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * status_led.c – RGB-Status-LED des XIAO nRF52840
 *
 * LEDs aus dem Boardfile (xiao_ble_common.dtsi), active low:
 *   led0 = Rot (P0.26), led1 = Grün (P0.30), led2 = Blau (P0.06)
 *
 * Blinken über k_timer. Die Callbacks laufen im ISR-Kontext,
 * gpio_pin_set_dt() ist dort erlaubt.
 */

#include "status_led.h"
#include "gasbee_config.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1)) &&
	     DT_NODE_EXISTS(DT_ALIAS(led2)), "Board braucht die Aliase led0..led2");

static const struct gpio_dt_spec s_red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec s_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec s_blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static bool s_ready;

// ── Blau: Advertising-Blinken ────────────────────────────────────────────────
static atomic_t s_blue_blink;   // 1 = Advertising (blinkt), 0 = fest an/aus

static void blue_off_expiry(struct k_timer *t)
{
	if (atomic_get(&s_blue_blink)) {
		gpio_pin_set_dt(&s_blue, 0);
	}
}
static K_TIMER_DEFINE(s_blue_off_timer, blue_off_expiry, NULL);

static void blue_blink_expiry(struct k_timer *t)
{
	gpio_pin_set_dt(&s_blue, 1);
	k_timer_start(&s_blue_off_timer, K_MSEC(GASBEE_LED_FLASH_MS), K_NO_WAIT);
}
static K_TIMER_DEFINE(s_blue_blink_timer, blue_blink_expiry, NULL);

// ── Grün: Blitze ─────────────────────────────────────────────────────────────
static atomic_t s_green_toggles;   // verbleibende Flanken (2 pro Blitz)

static void green_expiry(struct k_timer *t)
{
	if (atomic_dec(&s_green_toggles) <= 1) {
		gpio_pin_set_dt(&s_green, 0);
		k_timer_stop(t);
		return;
	}
	gpio_pin_toggle_dt(&s_green);
}
static K_TIMER_DEFINE(s_green_timer, green_expiry, NULL);

static void green_flash(int count)
{
	if (!s_ready) {
		return;
	}
	k_timer_stop(&s_green_timer);
	gpio_pin_set_dt(&s_green, 1);
	atomic_set(&s_green_toggles, 2 * count - 1);
	k_timer_start(&s_green_timer, K_MSEC(GASBEE_LED_FLASH_MS),
		      K_MSEC(GASBEE_LED_FLASH_MS));
}

// ── Öffentliche API ───────────────────────────────────────────────────────────

int status_led_init(void)
{
	const struct gpio_dt_spec *leds[] = { &s_red, &s_green, &s_blue };

	for (int i = 0; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(leds[i])) {
			LOG_ERR("LED-GPIO nicht bereit");
			return -ENODEV;
		}
		int err = gpio_pin_configure_dt(leds[i], GPIO_OUTPUT_INACTIVE);
		if (err) {
			LOG_ERR("LED-GPIO konfigurieren err=%d", err);
			return err;
		}
	}
	s_ready = true;
	LOG_INF("Status-LED aktiv (Testmodus)");
	return 0;
}

void status_led_ble_adv(void)
{
	if (!s_ready) {
		return;
	}
	atomic_set(&s_blue_blink, 1);
	k_timer_start(&s_blue_blink_timer, K_NO_WAIT, K_MSEC(GASBEE_LED_ADV_BLINK_MS));
}

void status_led_ble_connected(void)
{
	if (!s_ready) {
		return;
	}
	atomic_set(&s_blue_blink, 0);
	k_timer_stop(&s_blue_blink_timer);
	k_timer_stop(&s_blue_off_timer);
	gpio_pin_set_dt(&s_blue, 1);
}

void status_led_ble_off(void)
{
	if (!s_ready) {
		return;
	}
	atomic_set(&s_blue_blink, 0);
	k_timer_stop(&s_blue_blink_timer);
	k_timer_stop(&s_blue_off_timer);
	gpio_pin_set_dt(&s_blue, 0);
}

void status_led_hx711_ok(bool ok)
{
	if (!s_ready) {
		return;
	}
	gpio_pin_set_dt(&s_red, ok ? 0 : 1);
}

void status_led_flash_measure(void)
{
	green_flash(1);
}

void status_led_flash_tare(void)
{
	green_flash(3);
}
