#pragma once
/*
 * WoMoHome Display – Lokaler Buzzer (Piezo auf GPIO6)
 *
 * Passiver Piezo-Summer, angesteuert per LEDC PWM.
 * GPIO6 war als ADC herausgeführt, ohne Funktion.
 * LEDC Timer 0, Channel 0, Low-Speed, 10-Bit.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_BUZZER_MAX_PREVIEW_NOTES 32

typedef struct {
	uint16_t freq_hz;
	uint16_t dur_ms;
	uint16_t pause_ms;
} display_buzzer_note_t;

/**
 * @brief Buzzer initialisieren (LEDC + Task).
 *        Einmalig beim Boot aufrufen.
 */
esp_err_t display_buzzer_init(void);

/**
 * @brief Kurzer Click-Ton (120 ms, 1000 Hz) – Touch-Rückmeldung.
 */
void display_buzzer_click(void);

/**
 * @brief Warnton: 3× kurze Pieptöne (A4, 80 ms).
 */
void display_buzzer_warn(void);

/**
 * @brief Alarmton: 3× langer Ton (A5, 500 ms).
 */
void display_buzzer_alarm(void);

/**
 * @brief Startup-Melodie: G5–A5–C6–E6 aufsteigendes Arpeggio.
 */
void display_buzzer_startup(void);

/**
 * @brief Erfolg-Ton: G5–C6–E6 hell aufsteigend.
 */
void display_buzzer_success(void);

/**
 * @brief Fehler-Ton: A5→A4 Oktavfall.
 */
void display_buzzer_error(void);

/**
 * @brief Benutzerdefinierte Tonsequenz abspielen (z. B. aus HTTP Sound-Studio).
 */
esp_err_t display_buzzer_play_preview(const display_buzzer_note_t *notes, size_t note_count);

/**
 * @brief Lautstärke setzen (0–100 %). 100 = maximale Duty (50% Rechteckwelle).
 *        Wirkung ab dem nächsten Ton.
 */
void display_buzzer_set_volume(uint8_t pct);

/**
 * @brief Laufende Ausgabe sofort stoppen.
 */
void display_buzzer_stop(void);

/**
 * @brief Systemtöne (Warn/Alarm) aktivieren/deaktivieren.
 */
void display_buzzer_set_enabled(bool enable);
bool display_buzzer_is_enabled(void);

/**
 * @brief Touch-Click-Ton separat aktivieren/deaktivieren.
 */
void display_buzzer_set_click_enabled(bool enable);
bool display_buzzer_is_click_enabled(void);

#ifdef __cplusplus
}
#endif
