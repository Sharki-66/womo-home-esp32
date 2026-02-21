/**
 * app_main.c – GasBee Hauptprogramm
 *
 * ESP32-C3 Mini: HX711 Einzelkanal → Gas-Berechnung → BLE GATT Notify
 *
 * Ablauf:
 *  1. NVS + BLE init
 *  2. HX711 init (Kanal A, Gain 128)
 *  3. Alle 5 s: Gewicht messen → Gas-% berechnen → BLE notify
 *  4. Tare-Befehl über BLE: aktuellen Rohwert als Offset speichern
 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>

#include <hx711.h>
#include "gasbee_config.h"
#include "ble_gas.h"

static const char *TAG = "gasbee";

// ── Kalibrierung (aus NVS ladbar) ────────────────────────────────────────────
static int32_t s_offset = GASBEE_HX711_OFFSET;
static float   s_scale  = GASBEE_HX711_SCALE;
static float   s_tare   = 0.0f;   // Zusätzlicher Tare-Offset (Behälter o.ä.)

static hx711_t s_hx = {
    .dout = GASBEE_HX711_DOUT_GPIO,
    .pd_sck = GASBEE_HX711_SCK_GPIO,
    .gain = HX711_GAIN_A_128,
};

// ── NVS: Kalibrierung laden/speichern ────────────────────────────────────────
static void cal_load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(GASBEE_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    int32_t off = s_offset;
    float   sc  = s_scale;
    float   tar = s_tare;
    size_t  sz;

    nvs_get_i32(h, GASBEE_NVS_KEY_OFFSET, &off);
    sz = sizeof(sc);  nvs_get_blob(h, GASBEE_NVS_KEY_SCALE, &sc, &sz);
    sz = sizeof(tar); nvs_get_blob(h, GASBEE_NVS_KEY_TARE,  &tar, &sz);

    nvs_close(h);
    s_offset = off;
    s_scale  = sc;
    s_tare   = tar;
    ESP_LOGI(TAG, "Kalibrierung geladen: offset=%ld scale=%.5f tare=%.3f kg",
             (long)s_offset, s_scale, s_tare);
}

static void cal_save_tare(void)
{
    nvs_handle_t h;
    if (nvs_open(GASBEE_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, GASBEE_NVS_KEY_TARE, &s_tare, sizeof(s_tare));
    nvs_commit(h);
    nvs_close(h);
}

// ── Tare-Befehl (aus BLE) ────────────────────────────────────────────────────
static void on_ble_tare(void)
{
    // Tara = 0 setzen (nächste Messung wird als Referenz genommen)
    s_tare = 0.0f;
    cal_save_tare();
    ESP_LOGI(TAG, "Tare gesetzt: s_tare=0 (nächste Messung = Referenz)");
}

// ── HX711 Messung ─────────────────────────────────────────────────────────────
static bool measure_weight(float *weight_kg_out)
{
    int32_t raw = 0;
    int32_t sum = 0;
    int     valid = 0;
    int32_t min_raw = INT32_MAX;
    int32_t max_raw = INT32_MIN;

    for (int i = 0; i < GASBEE_HX711_AVG_SAMPLES; i++) {
        esp_err_t err = hx711_wait(&s_hx, GASBEE_HX711_READY_TIMEOUT);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HX711 not ready (sample %d)", i);
            continue;
        }
        err = hx711_read_data(&s_hx, &raw);
        if (err == ESP_OK) {
            sum += raw;
            valid++;
            if (raw < min_raw) min_raw = raw;
            if (raw > max_raw) max_raw = raw;
        }
    }

    if (valid == 0) {
        ESP_LOGE(TAG, "Keine gültigen HX711-Messwerte");
        return false;
    }

    int32_t avg_raw = sum / valid;

    // Diagnose: raw=0 → DOUT floating (Verdrahtung prüfen!)
    if (avg_raw == 0) {
        ESP_LOGE(TAG, "⚠ HX711 raw=0! DOUT floating? Verdrahtung prüfen:");
        ESP_LOGE(TAG, "  DOUT → GPIO%d | SCK → GPIO%d | VCC → 5V? | Load-Cell E+/E-/A+/A-?",
                 GASBEE_HX711_DOUT_GPIO, GASBEE_HX711_SCK_GPIO);
    }
    // Diagnose: alle Samples identisch → kein echtes HX711-Signal
    if (min_raw == max_raw && valid > 1) {
        ESP_LOGW(TAG, "⚠ HX711 alle %d Samples = %ld (kein Rauschen, DOUT prüfen)", valid, (long)avg_raw);
    }

    ESP_LOGI(TAG, "HX711 raw=%ld  min=%ld  max=%ld  offset=%ld  delta=%ld",
             (long)avg_raw, (long)min_raw, (long)max_raw,
             (long)s_offset, (long)(avg_raw - s_offset));

    *weight_kg_out = ((float)avg_raw - (float)s_offset) * s_scale - s_tare;
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

// ── Mess-Task ─────────────────────────────────────────────────────────────────
static void measure_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(GASBEE_HX711_STARTUP_MS));

    // Pull-Up auf DOUT aktivieren: bei offenem Pin liest der HX711 HIGH (nicht bereit)
    // → hx711_wait() timeouted dann sauber statt raw=0 zurückzugeben.
    gpio_set_pull_mode(GASBEE_HX711_DOUT_GPIO, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "DOUT GPIO%d Pull-Up aktiviert", GASBEE_HX711_DOUT_GPIO);

    // Retry-Schleife: HX711 kann nach Power-On länger brauchen oder
    // ist beim ersten Start noch nicht angeschlossen.
    {
        esp_err_t init_err;
        int attempt = 0;
        do {
            init_err = hx711_init(&s_hx);
            if (init_err != ESP_OK) {
                attempt++;
                ESP_LOGW(TAG, "HX711 nicht bereit (Versuch %d, err=%s) – retry in 2 s...",
                         attempt, esp_err_to_name(init_err));
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        } while (init_err != ESP_OK);
    }
    ESP_LOGI(TAG, "HX711 bereit (DOUT=GPIO%d, SCK=GPIO%d)",
             GASBEE_HX711_DOUT_GPIO, GASBEE_HX711_SCK_GPIO);

    while (1) {
        float weight_kg = 0.0f;

        if (measure_weight(&weight_kg)) {
            float   net_kg = 0.0f;
            uint8_t pct    = 0;
            calc_gas(weight_kg, &net_kg, &pct);

            ESP_LOGI(TAG, "Gewicht=%.2f kg | Gas-Netto=%.2f kg | Füllstand=%u%%",
                     weight_kg, net_kg, pct);

            ble_gas_notify(weight_kg, net_kg, pct);
        }

        vTaskDelay(pdMS_TO_TICKS(GASBEE_HX711_POLL_INTERVAL_MS));
    }
}

// ── app_main ──────────────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "GasBee startet (ESP32-C3)");

    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS voll/veraltet, löschen...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    cal_load_from_nvs();

    // BLE
    ESP_ERROR_CHECK(ble_gas_init());
    ble_gas_set_tare_callback(on_ble_tare);

    // Mess-Task (eigener Stack für HX711-Timing)
    xTaskCreate(measure_task, "measure", 4096, NULL, 5, NULL);
}
