#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * buzzer – passiver Piezo-Transducer über LEDC-PWM
 *
 * Konfiguration (GPIO, Timer, Kanal) in sensor_config.h.
 * Alle Töne laufen NON-BLOCKING in einem dedizierten FreeRTOS-Task.
 * Ein neuer Aufruf unterbricht laufende Sequenzen sofort.
 */

/* ── Notenwerte (Frequenz in Hz) ── */
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_D5   587
#define NOTE_E5   659
#define NOTE_F5   698
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_REST   0   /* Pause */

/* ── Einzelner Ton in einer Melodie ── */
typedef struct {
    uint16_t freq_hz;   /**< Frequenz in Hz; 0 = Pause        */
    uint16_t dur_ms;    /**< Dauer in ms                       */
} buzzer_note_t;

/* ── Initialisierung ── */

/**
 * @brief LEDC-Timer + Channel konfigurieren, Task starten.
 *        Einmalig aus app_main() aufrufen.
 */
esp_err_t buzzer_init(void);

/* ── Einfache Töne ── */

/**
 * @brief Kurzer Bestätigungs-Beep (200 ms, 1 kHz).
 */
void buzzer_beep(void);

/**
 * @brief Doppel-Beep (z.B. für Warnungen) – 2× 100 ms.
 */
void buzzer_double_beep(void);

/**
 * @brief Langer Alarm-Ton (3× 500 ms, 880 Hz).
 */
void buzzer_alarm(void);

/**
 * @brief Einzelnen Ton mit freier Frequenz und Dauer abspielen.
 *
 * @param freq_hz  Frequenz in Hz (20–20000), 0 = Stille
 * @param dur_ms   Dauer in ms
 */
void buzzer_tone(uint16_t freq_hz, uint16_t dur_ms);

/* ── Melodien ── */

/**
 * @brief Beliebige Note-Sequenz non-blocking abspielen.
 *
 * @param notes     Array von buzzer_note_t (letzte Note MUSS freq=0, dur=0 sein)
 * @param repeat    Anzahl Wiederholungen (0 = einmalig)
 */
void buzzer_play(const buzzer_note_t *notes, uint8_t repeat);

/** Vordefinierte Melodien */
void buzzer_melody_startup(void);   /**< Kurze Aufwärts-Tonleiter beim Start      */
void buzzer_melody_level_ok(void);  /**< Fahrzeug waagerecht (Nivellierhilfe OK)  */
void buzzer_melody_level_warn(void);/**< Fahrzeug noch nicht waagerecht           */

/**
 * @brief Laufende Wiedergabe sofort stoppen.
 */
void buzzer_stop(void);

#ifdef __cplusplus
}
#endif
