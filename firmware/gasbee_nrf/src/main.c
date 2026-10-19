/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * main.c – GasBee nRF Hauptprogramm
 *
 * Seeed XIAO nRF52840: HX711 Einzelkanal + Akku → BLE Advertising (Format v1)
 * GATT für Kalibrierung (nur im Konfigurationsmodus) und andere Clients.
 *
 * Ablauf:
 *  1. Settings (NVS) laden: offset / scale / tare
 *  2. BLE init, Advertising-Set anlegen
 *  3. Akkubetrieb: alle CONFIG_GASBEE_MEASURE_INTERVAL_S
 *     HX711 aufwecken → messen → Power-Down, Akku + Ladestatus lesen,
 *     Advertising-Burst (10 Pakete à 1 s), GATT-Notify falls verbunden.
 *     Fehlt der HX711, wird trotzdem gesendet (Gewicht ungültig, Flag Bit 0 = 0).
 *     Konfigurationsmodus (USB/VBUS): Messung alle 5 s, Dauersenden, Kalibrierung
 *     per GATT 0x2B11–0x2B13 freigegeben. USB ab → zurück in den Akkubetrieb.
 *  4. Tare-Befehl über GATT: wirkt nur auf die GATT-Werte, das Advertising
 *     überträgt immer das Bruttogewicht.
 */

#include <math.h>
#include <stdlib.h>
#include <limits.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

#include "hx711.h"
#include "gasbee_config.h"
#include "ble_gas.h"
#include "status_led.h"
#include "battery.h"

LOG_MODULE_REGISTER(gasbee, LOG_LEVEL_INF);

// ── Kalibrierung (aus Settings ladbar) ───────────────────────────────────────
static int32_t s_offset = GASBEE_HX711_OFFSET;
static float   s_scale  = GASBEE_HX711_SCALE;
static float   s_tare   = 0.0f;   // Tara: Bruttogewicht der leeren Waage (nur GATT)

// Kalibrierung aus NVS geladen bzw. per GATT gespeichert (beide Werte)
// → Advertising-Flag "kalibriert"
static bool s_offset_from_nvs;
static bool s_scale_from_nvs;

static bool s_hx_ready;

// Konfigurationsmodus = USB-Spannung (VBUS) liegt an
static bool s_config_mode;

static atomic_t s_tare_request;

// Kalibrier-Befehle aus BLE (BT-Kontext → dort nur übernehmen)
static atomic_t s_cal_zero_request;
static atomic_t s_cal_ref_grams;     // 0 = keine Anforderung
static atomic_t s_cal_set_request;
static int32_t  s_cal_set_offset;
static float    s_cal_set_scale;

// Weckt die Mess-Schleife für eine sofortige Messung (Tare, Kalibrierung)
static K_SEM_DEFINE(s_wake, 0, 1);

#define ZEPHYR_USER DT_PATH(zephyr_user)

static hx711_t s_hx = {
	.dout   = GPIO_DT_SPEC_GET(ZEPHYR_USER, hx711_dout_gpios),
	.pd_sck = GPIO_DT_SPEC_GET(ZEPHYR_USER, hx711_sck_gpios),
	.gain   = HX711_GAIN_A_128,
};

// ── Settings: Kalibrierung laden/speichern ───────────────────────────────────
static int cal_settings_set(const char *name, size_t len,
			    settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	void *dst;
	size_t size;

	if (settings_name_steq(name, GASBEE_SETTINGS_KEY_OFFSET, &next) && !next) {
		dst = &s_offset; size = sizeof(s_offset);
	} else if (settings_name_steq(name, GASBEE_SETTINGS_KEY_SCALE, &next) && !next) {
		dst = &s_scale;  size = sizeof(s_scale);
	} else if (settings_name_steq(name, GASBEE_SETTINGS_KEY_TARE, &next) && !next) {
		dst = &s_tare;   size = sizeof(s_tare);
	} else {
		return -ENOENT;
	}

	if (len != size) {
		return -EINVAL;
	}
	int rc = read_cb(cb_arg, dst, size);
	if (rc < 0) {
		return rc;
	}
	if (dst == &s_offset) {
		s_offset_from_nvs = true;
	} else if (dst == &s_scale) {
		s_scale_from_nvs = true;
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(gasbee, GASBEE_SETTINGS_SUBTREE, NULL,
			       cal_settings_set, NULL, NULL);

static void cal_load(void)
{
	int err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings_subsys_init err=%d", err);
		return;
	}
	settings_load_subtree(GASBEE_SETTINGS_SUBTREE);
	LOG_INF("Kalibrierung geladen: offset=%d scale=%.8f tare=%.3f kg",
		s_offset, (double)s_scale, (double)s_tare);
}

static void cal_save_tare(void)
{
	int err = settings_save_one(GASBEE_SETTINGS_SUBTREE "/" GASBEE_SETTINGS_KEY_TARE,
				    &s_tare, sizeof(s_tare));
	if (err) {
		LOG_ERR("Tare speichern fehlgeschlagen err=%d", err);
	}
}

static void cal_save_offset(void)
{
	int err = settings_save_one(GASBEE_SETTINGS_SUBTREE "/" GASBEE_SETTINGS_KEY_OFFSET,
				    &s_offset, sizeof(s_offset));
	if (err) {
		LOG_ERR("Offset speichern fehlgeschlagen err=%d", err);
		return;
	}
	s_offset_from_nvs = true;
}

static void cal_save_scale(void)
{
	int err = settings_save_one(GASBEE_SETTINGS_SUBTREE "/" GASBEE_SETTINGS_KEY_SCALE,
				    &s_scale, sizeof(s_scale));
	if (err) {
		LOG_ERR("Scale speichern fehlgeschlagen err=%d", err);
		return;
	}
	s_scale_from_nvs = true;
}

// Kalibrierung aus NVS löschen → Startwerte aus gasbee_config.h, Flag "kalibriert" aus
static void cal_delete(void)
{
	settings_delete(GASBEE_SETTINGS_SUBTREE "/" GASBEE_SETTINGS_KEY_OFFSET);
	settings_delete(GASBEE_SETTINGS_SUBTREE "/" GASBEE_SETTINGS_KEY_SCALE);
	s_offset = GASBEE_HX711_OFFSET;
	s_scale  = GASBEE_HX711_SCALE;
	s_offset_from_nvs = false;
	s_scale_from_nvs  = false;
	LOG_INF("Kalibrierung gelöscht, Startwerte aktiv");
}

// ── Befehle aus BLE ──────────────────────────────────────────────────────────
// Laufen im BT-Kontext → nur übernehmen und die Mess-Schleife wecken
static void on_ble_tare(void)
{
	atomic_set(&s_tare_request, 1);
	k_sem_give(&s_wake);
	LOG_INF("Tare angefordert: nächste Messung wird als Tara übernommen");
}

static void on_cal_zero(void)
{
	atomic_set(&s_cal_zero_request, 1);
	k_sem_give(&s_wake);
}

static void on_cal_reference(uint32_t grams)
{
	atomic_set(&s_cal_ref_grams, (atomic_val_t)grams);
	k_sem_give(&s_wake);
}

static void on_cal_set(int32_t offset, float scale)
{
	s_cal_set_offset = offset;
	s_cal_set_scale  = scale;
	atomic_set(&s_cal_set_request, 1);
	k_sem_give(&s_wake);
}

static void on_cal_get(int32_t *offset, float *scale)
{
	*offset = s_offset;
	*scale  = s_scale;
}

static const ble_gas_cal_ops_t s_cal_ops = {
	.zero      = on_cal_zero,
	.reference = on_cal_reference,
	.set       = on_cal_set,
	.get       = on_cal_get,
};

// ── HX711 Messung ─────────────────────────────────────────────────────────────
static bool measure_gross(float *gross_kg_out, int32_t *raw_out)
{
	int32_t raw = 0;
	int64_t sum = 0;
	int     valid = 0;
	int32_t min_raw = INT32_MAX;
	int32_t max_raw = INT32_MIN;

	for (int i = 0; i < GASBEE_HX711_AVG_SAMPLES; i++) {
		if (hx711_wait(&s_hx, GASBEE_HX711_READY_TIMEOUT_MS) != 0) {
			LOG_WRN("HX711 nicht bereit (Sample %d)", i);
			continue;
		}
		if (hx711_read_data(&s_hx, &raw) == 0) {
			sum += raw;
			valid++;
			if (raw < min_raw) min_raw = raw;
			if (raw > max_raw) max_raw = raw;
		}
	}

	if (valid == 0) {
		LOG_ERR("Keine gültigen HX711-Messwerte");
		return false;
	}

	int32_t avg_raw = (int32_t)(sum / valid);

	// Diagnose: raw=0 → DOUT floating (Verdrahtung prüfen!)
	if (avg_raw == 0) {
		LOG_ERR("HX711 raw=0! DOUT floating? Verdrahtung prüfen:");
		LOG_ERR("  DOUT → D2 | PD_SCK → D3 | VCC → 3V3 | Load-Cell E+/E-/A+/A-?");
	}
	// Diagnose: alle Samples identisch → kein echtes HX711-Signal
	if (min_raw == max_raw && valid > 1) {
		LOG_WRN("HX711 alle %d Samples = %d (kein Rauschen, DOUT prüfen)", valid, avg_raw);
	}

	LOG_INF("HX711 raw=%d  min=%d  max=%d  offset=%d  delta=%d",
		avg_raw, min_raw, max_raw, s_offset, avg_raw - s_offset);

	*gross_kg_out = ((float)avg_raw - (float)s_offset) * s_scale;
	*raw_out      = avg_raw;
	return true;
}

// ── Gas-Berechnung ───────────────────────────────────────────────────────────
static void calc_gas(float weight_kg, float *net_kg_out, uint8_t *pct_out)
{
	float net = weight_kg - GASBEE_GAS_TARE_KG;
	if (net < 0.0f) net = 0.0f;
	if (net > GASBEE_GAS_FILL_KG) net = GASBEE_GAS_FILL_KG;

	float pct = (net / GASBEE_GAS_FILL_KG) * 100.0f;
	if (pct < 0.0f)   pct = 0.0f;
	if (pct > 100.0f) pct = 100.0f;

	*net_kg_out = net;
	*pct_out    = (uint8_t)roundf(pct);
}

// ── HX711 aufwecken / schlafen legen ─────────────────────────────────────────
// Nach Power-Up startet der HX711 auf Kanal A, Gain 128 (unsere Einstellung).
// Die ersten Wandlungen sind noch nicht eingeschwungen → verwerfen.
static bool hx711_wake(void)
{
	if (!s_hx_ready) {
		int err = hx711_init(&s_hx);   // enthält Power-Up + Gain setzen
		if (err) {
			LOG_WRN("HX711 nicht bereit (err=%d)", err);
			return false;
		}
		s_hx_ready = true;
		LOG_INF("HX711 bereit (DOUT=D2, PD_SCK=D3)");
	} else {
		hx711_power_down(&s_hx, false);
	}

	int32_t raw;
	for (int i = 0; i < GASBEE_HX711_DISCARD_SAMPLES; i++) {
		if (hx711_wait(&s_hx, GASBEE_HX711_WAKE_TIMEOUT_MS) != 0) {
			LOG_WRN("HX711 nach Power-Up nicht bereit");
			return false;
		}
		hx711_read_data(&s_hx, &raw);
	}
	return true;
}

static void hx711_sleep(void)
{
	if (s_hx_ready) {
		hx711_power_down(&s_hx, true);   // PD_SCK high > 60 µs
	}
}

// ── Kalibrier-Befehle ausführen (Mess-Schleife) ──────────────────────────────
// Direkt gesetzte Werte (0x2B13) brauchen keine Messung.
static bool cal_apply_set_request(void)
{
	if (!atomic_cas(&s_cal_set_request, 1, 0)) {
		return false;
	}
	if (s_cal_set_scale == 0.0f) {
		cal_delete();
		return true;
	}
	s_offset = s_cal_set_offset;
	s_scale  = s_cal_set_scale;
	cal_save_offset();
	cal_save_scale();
	LOG_INF("Kalibrierung gespeichert: offset=%d scale=%.10f", s_offset, (double)s_scale);
	return true;
}

// Nullpunkt (0x2B11) und Referenzgewicht (0x2B12) brauchen den aktuellen Rohwert.
static bool cal_handle_measured(int32_t raw)
{
	bool done = false;

	if (atomic_cas(&s_cal_zero_request, 1, 0)) {
		s_offset = raw;
		cal_save_offset();
		LOG_INF("Kalibrierung: Nullpunkt gespeichert, offset=%d", s_offset);
		done = true;
	}

	uint32_t grams = (uint32_t)atomic_set(&s_cal_ref_grams, 0);
	if (grams) {
		int32_t delta = raw - s_offset;
		if (abs(delta) < GASBEE_CAL_MIN_DELTA_RAW) {
			LOG_ERR("Kalibrierung: kein Gewicht erkennbar (delta=%d), Scale unverändert", delta);
		} else {
			// Negatives Scale ist erlaubt (Wägezelle andersherum angeschlossen)
			s_scale = ((float)grams / 1000.0f) / (float)delta;
			cal_save_scale();
			LOG_INF("Kalibrierung: %u g bei delta=%d → scale=%.10f gespeichert",
				grams, delta, (double)s_scale);
			done = true;
		}
	}
	return done;
}

static void cal_drop_measured(void)
{
	bool zero = atomic_cas(&s_cal_zero_request, 1, 0);
	bool ref  = atomic_set(&s_cal_ref_grams, 0) != 0;

	if (zero || ref) {
		LOG_ERR("Kalibrierung verworfen: HX711 liefert keinen Messwert");
	}
}

// ── Ein Messzyklus ────────────────────────────────────────────────────────────
static void measure_cycle(void)
{
	float   gross_kg = 0.0f;
	int32_t raw = 0;
	bool    cal_done = cal_apply_set_request();
	bool    hx_ok = hx711_wake() && measure_gross(&gross_kg, &raw);

	hx711_sleep();

	if (hx_ok) {
		if (cal_handle_measured(raw)) {
			cal_done = true;
		}
		// nach einer Kalibrierung mit den neuen Werten rechnen
		gross_kg = ((float)raw - (float)s_offset) * s_scale;
	} else {
		cal_drop_measured();
	}

	uint16_t bat_mv = GASBEE_ADV_BAT_INVALID;
	int err = battery_read_mv(&bat_mv);
	if (err) {
		LOG_ERR("Akkumessung fehlgeschlagen err=%d", err);
		bat_mv = GASBEE_ADV_BAT_INVALID;
	}
	bool charging = battery_is_charging();

	uint8_t flags = 0;
	int16_t weight_10g = GASBEE_ADV_WEIGHT_INVALID;
	bool tared = false;

	if (hx_ok) {
		flags |= GASBEE_ADV_FLAG_HX711_OK;

		// Advertising: Bruttogewicht ohne Tara, 10 g, 0x8000 bleibt reserviert
		float w = roundf(gross_kg * 100.0f);
		w = CLAMP(w, (float)(INT16_MIN + 1), (float)INT16_MAX);
		weight_10g = (int16_t)w;

		// GATT (Kompatibilität mit dem bisherigen Sensorboard): wie bisher nach Tara
		if (atomic_cas(&s_tare_request, 1, 0)) {
			s_tare = gross_kg;
			cal_save_tare();
			tared = true;
			LOG_INF("Tare übernommen: %.3f kg", (double)s_tare);
		}
		float   weight_kg = gross_kg - s_tare;
		float   net_kg    = 0.0f;
		uint8_t pct       = 0;
		calc_gas(weight_kg, &net_kg, &pct);
		ble_gas_notify(weight_kg, net_kg, pct);
	}
	if (charging) {
		flags |= GASBEE_ADV_FLAG_CHARGING;
	}
	if (s_config_mode) {
		flags |= GASBEE_ADV_FLAG_CONFIG_MODE;
	}
	if (s_offset_from_nvs && s_scale_from_nvs) {
		flags |= GASBEE_ADV_FLAG_CALIBRATED;
	}

	LOG_INF("Brutto=%.2f kg | Akku=%u mV%s | HX711 %s",
		(double)gross_kg, bat_mv, charging ? " (lädt)" : "", hx_ok ? "ok" : "FEHLER");

	if (bat_mv != GASBEE_ADV_BAT_INVALID) {
		ble_gas_notify_battery(bat_mv);
	}
	ble_gas_adv_publish(weight_10g, bat_mv, flags);

	status_led_hx711_ok(hx_ok);
	if (tared || cal_done) {
		status_led_flash_tare();
	} else if (hx_ok) {
		status_led_flash_measure();
	}
}

// ── Externer QSPI-Flash (P25Q16H) ───────────────────────────────────────────
// Wird nicht genutzt (Settings liegen im internen Flash). Suspend schickt ihn
// in Deep Power-Down und schaltet die QSPI-Peripherie ab.
static void ext_flash_sleep(void)
{
	const struct device *flash = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(p25q16h));

	if (!flash || !device_is_ready(flash)) {
		return;
	}
	int err = pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);
	if (err && err != -EALREADY) {
		LOG_WRN("QSPI-Flash Suspend err=%d", err);
	} else {
		LOG_INF("QSPI-Flash in Deep Power-Down");
	}
}

// ── main ──────────────────────────────────────────────────────────────────────
int main(void)
{
	LOG_INF("GasBee startet (XIAO nRF52840), Messintervall %d s",
		CONFIG_GASBEE_MEASURE_INTERVAL_S);

	ext_flash_sleep();
	status_led_init();
	cal_load();
	battery_init();

	if (ble_gas_init() != 0) {
		LOG_ERR("BLE nicht verfügbar – nur lokale Messung");
	}
	ble_gas_set_tare_callback(on_ble_tare);
	ble_gas_set_cal_ops(&s_cal_ops);

	k_msleep(GASBEE_HX711_STARTUP_MS);

	// Feste Taktung: Intervall zählt ab Zyklusbeginn, nicht ab Zyklusende.
	// Alle GASBEE_USB_POLL_MS wird VBUS abgefragt: USB an → Konfigurationsmodus
	// (Dauersenden, 5-s-Takt, Kalibrierung frei), USB ab → Akkubetrieb.
	// Ein Befehl über BLE (Tare, Kalibrierung) löst sofort eine Zusatzmessung
	// aus, der Takt bleibt dabei erhalten.
	int64_t next_ms = k_uptime_get();

	while (1) {
		int64_t now = k_uptime_get();

		bool usb = battery_usb_present();
		if (usb != s_config_mode) {
			s_config_mode = usb;
			LOG_INF("USB %s → %s", usb ? "an" : "ab",
				usb ? "Konfigurationsmodus" : "Akkubetrieb");
			ble_gas_set_config_mode(usb);
			next_ms = now;   // sofort messen, dann im neuen Takt
		}

		if (now >= next_ms) {
			int64_t interval_ms = s_config_mode
				? (int64_t)GASBEE_CONFIG_MEASURE_MS
				: (int64_t)CONFIG_GASBEE_MEASURE_INTERVAL_S * MSEC_PER_SEC;

			measure_cycle();
			next_ms += interval_ms;
			if (next_ms <= now) {
				next_ms = now + interval_ms;   // keine verpassten Zyklen nachholen
			}
		}

		// Bis zur nächsten Messung warten, spätestens bis zur nächsten VBUS-Abfrage
		int64_t wake_ms = MIN(next_ms, k_uptime_get() + GASBEE_USB_POLL_MS);

		if (k_sem_take(&s_wake, K_TIMEOUT_ABS_MS(wake_ms)) == 0) {
			LOG_INF("Befehl über BLE → sofort messen");
			measure_cycle();
		}
	}
	return 0;
}
