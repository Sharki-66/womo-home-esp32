/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * WoMoHome Display Firmware – Zentrale Konfiguration
 * ESP32-S3-Touch-LCD-7 (Waveshare)
 *
 * ====================================================================================
 * Log-Level Konfiguration (ESP_LOG_NONE/ERROR/WARN/INFO/DEBUG/VERBOSE)
 * Hier zentral je Modul einstellen, ohne Code zu ändern.
 * ====================================================================================
 */

// ── Kern ────────────────────────────────────────────────────────────────────────────
#define LOG_LEVEL_MAIN           ESP_LOG_INFO   // main.c (womo_main)
#define LOG_LEVEL_UI             ESP_LOG_WARN   // ui/main.c (WaveShare_7_UI)

// ── Hardware / Treiber ───────────────────────────────────────────────────────────────
#define LOG_LEVEL_LVGL_PORT      ESP_LOG_WARN   // hardware/lvgl_port.c
#define LOG_LEVEL_DISPLAY_HW     ESP_LOG_WARN   // hardware/waveshare_rgb_lcd_port.c

// ── RS485 ────────────────────────────────────────────────────────────────────────────
#define LOG_LEVEL_RS485          ESP_LOG_WARN   // rs485/womo_rs485_display.c

// ── Netzwerk ─────────────────────────────────────────────────────────────────────────
#define LOG_LEVEL_WIFI           ESP_LOG_WARN   // network/womo_wifi.c + ESP-IDF Netz (esp_netif etc.)
#define LOG_LEVEL_WEATHER_HTTP   ESP_LOG_WARN   // network/womo_weather_http.c
#define LOG_LEVEL_METEOALARM     ESP_LOG_WARN   // network/womo_meteoalarm.c
#define LOG_LEVEL_GEOCODE        ESP_LOG_WARN   // network/womo_geocode.c
#define LOG_LEVEL_ROUTER_UCI     ESP_LOG_WARN   // network/womo_router_uci.c
#define LOG_LEVEL_HTTP_MUTEX     ESP_LOG_WARN   // network/womo_http_mutex.c
#define LOG_LEVEL_BUZZER_HTTP    ESP_LOG_INFO   // network/womo_buzzer_http.c
#define LOG_LEVEL_DISPLAY_HTTP   ESP_LOG_INFO   // network/womo_display_http.c

// ── Speicher ─────────────────────────────────────────────────────────────────────────
#define LOG_LEVEL_SD             ESP_LOG_WARN   // storage/womo_sd.c

// ── Zeit ─────────────────────────────────────────────────────────────────────────────
#define LOG_LEVEL_TIME           ESP_LOG_WARN   // time/womo_time.c
#define LOG_LEVEL_SUN_CALC       ESP_LOG_WARN   // time/womo_sun_calc.c

// ── GUI / Theme ──────────────────────────────────────────────────────────────────────
#define LOG_LEVEL_THEME          ESP_LOG_INFO   // gui/womo_theme.c
#define LOG_LEVEL_WEATHER_WIDGET ESP_LOG_WARN   // gui/womo_weather.c
#define LOG_LEVEL_BATTERY        ESP_LOG_WARN   // gui/womo_battery.c
#define LOG_LEVEL_TANK           ESP_LOG_WARN   // gui/womo_tank.c
#define LOG_LEVEL_GAS            ESP_LOG_WARN   // gui/womo_gas_bottle.c
#define LOG_LEVEL_THRESHOLDS     ESP_LOG_WARN   // gui/womo_thresholds.c
#define LOG_LEVEL_FONTS          ESP_LOG_WARN   // gui/womo_fonts_german.c
#define LOG_LEVEL_FORECAST_MODAL ESP_LOG_WARN   // gui/womo_forecast_modal.c
#define LOG_LEVEL_CONN_MODAL     ESP_LOG_WARN   // gui/womo_connectivity_modal.c
#define LOG_LEVEL_SETTINGS_MODAL ESP_LOG_WARN   // gui/womo_settings_modal.c
#define LOG_LEVEL_ROUTER_LEDS    ESP_LOG_WARN   // gui/womo_router_leds_modal.c

// ====================================================================================
// Buzzer – Piezo on Display-Board
// GPIO6: als ADC herausgeführt, keine andere Funktion → Piezo anschließen
// ====================================================================================
#define DISPLAY_BUZZER_GPIO          6
#define DISPLAY_BUZZER_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define DISPLAY_BUZZER_LEDC_TIMER    LEDC_TIMER_0
#define DISPLAY_BUZZER_LEDC_CHANNEL  LEDC_CHANNEL_0
#define DISPLAY_BUZZER_LEDC_BITS     LEDC_TIMER_10_BIT
#define DISPLAY_BUZZER_DUTY          (512)   /* 50% duty bei 10-Bit */

// Optionales Live-Sound-Studio via HTTP API + SPIFFS-Seite
// 1 = aktiv, 0 = vollständig auskompiliert
#define WOMO_ENABLE_BUZZER_STUDIO_HTTP 1

// ====================================================================================
// Screenshot
// Long-Press anywhere auf dem Display speichert PNG auf SD-Card
// Pfad:    /sdcard/screenshots/screen_HHMMSS.png
// Stack:   PSRAM (MALLOC_CAP_SPIRAM), 32 KiB  – lodepng + FAT-I/O
// Methode: Liest Frame-Buffer direkt (RGB565, existiert bereits im PSRAM).
//          Kein lv_snapshot, kein zusätzlicher Pixel-Puffer → ~400 KB Peak (PNG-Output).
// 1 = aktiv, 0 = vollständig auskompiliert
// ====================================================================================
#define WOMO_ENABLE_SCREENSHOT         0
#define WOMO_SCREENSHOT_DIR            "/sdcard/screenshots"
#define WOMO_SCREENSHOT_STACK_SIZE     (32768)
#define WOMO_SCREENSHOT_TASK_PRIORITY  (3)
