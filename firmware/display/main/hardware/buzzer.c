/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "buzzer.h"
#include "display_config.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "disp_buzzer";

/* ── Interne Typen ── */
typedef struct {
    uint16_t freq;
    uint16_t dur_ms;
} buzzer_note_t;

typedef enum {
    BUZZER_CMD_NONE = 0,
    BUZZER_CMD_LEGACY_SEQ,
    BUZZER_CMD_PREVIEW_SEQ,
    BUZZER_CMD_STOP,
} buzzer_cmd_type_t;

typedef struct {
    buzzer_cmd_type_t kind;
    uint16_t           duty;          /* 0 = s_duty verwenden (Preview) */
    const buzzer_note_t *legacy_notes;
    size_t preview_count;
    display_buzzer_note_t preview_notes[DISPLAY_BUZZER_MAX_PREVIEW_NOTES];
} buzzer_cmd_t;

static QueueHandle_t s_queue          = NULL;
static bool          s_initialized     = false;
static bool          s_enabled         = true;   /* Systemtöne (warn/alarm) */
static bool          s_click_enabled   = true;   /* Touch-Click-Ton separat */
static uint16_t      s_duty            = DISPLAY_BUZZER_DUTY; /* Laufzeit-Lautstärke */

/* ── Vordefinierte Melodien ── */
#define NOTE_REST 0
/* Click: einzelner A5 (880 Hz), 10 ms – sauber, kein Metallklang */
static const buzzer_note_t s_seq_click[]   = { {880, 10}, {NOTE_REST, 0} };
static const buzzer_note_t s_seq_warn[]    = {
    {880, 80}, {NOTE_REST, 60}, {880, 80}, {NOTE_REST, 60}, {880, 80}, {NOTE_REST, 0}
};
static const buzzer_note_t s_seq_alarm[]   = {
    {1760, 500}, {NOTE_REST, 100},
    {1760, 500}, {NOTE_REST, 100},
    {1760, 500}, {NOTE_REST, 0}
};
/* Startup: G5–A5–C6–E6 aufsteigendes Arpeggio – sattes "System bereit" */
static const buzzer_note_t s_seq_startup[] = {
    {784, 80}, {880, 80}, {1047, 80}, {1319, 200}, {NOTE_REST, 0}
};
/* Erfolg: G5–C6–E6 hell aufsteigend – kurze positive Bestätigung */
static const buzzer_note_t s_seq_success[] = {
    {784, 80}, {1047, 100}, {1319, 180}, {NOTE_REST, 0}
};
/* Fehler: A5→A4 Oktavfall – unmissverständlich negativ */
static const buzzer_note_t s_seq_error[]   = {
    {880, 100}, {440, 150}, {NOTE_REST, 0}
};

/* ── Low-level ── */
static void tone_on(uint16_t freq_hz, uint16_t duty)
{
    if (freq_hz == 0) {
        ledc_stop(DISPLAY_BUZZER_LEDC_MODE, DISPLAY_BUZZER_LEDC_CHANNEL, 0);
        return;
    }
    ledc_set_freq(DISPLAY_BUZZER_LEDC_MODE, DISPLAY_BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(DISPLAY_BUZZER_LEDC_MODE, DISPLAY_BUZZER_LEDC_CHANNEL, duty);
    ledc_update_duty(DISPLAY_BUZZER_LEDC_MODE, DISPLAY_BUZZER_LEDC_CHANNEL);
}

static void tone_off(void)
{
    ledc_stop(DISPLAY_BUZZER_LEDC_MODE, DISPLAY_BUZZER_LEDC_CHANNEL, 0);
}

/* ── Task ── */
static void buzzer_task(void *arg)
{
    buzzer_cmd_t cmd;
    for (;;) {
        if (!xQueueReceive(s_queue, &cmd, portMAX_DELAY)) continue;

        /* Click hat eigenen Guard (s_click_enabled) – hier nur
           Warn/Alarm mit s_enabled filtern. Click wurde bereits
           in display_buzzer_click() geprüft und kommt gar nicht
           in die Queue wenn s_click_enabled == false. */

        if (cmd.kind == BUZZER_CMD_STOP) {
            tone_off();
            continue;
        }

        uint16_t eff_duty = cmd.duty ? cmd.duty : s_duty;

        if (cmd.kind == BUZZER_CMD_LEGACY_SEQ && cmd.legacy_notes) {
            const buzzer_note_t *n = cmd.legacy_notes;
            while (n->freq != NOTE_REST || n->dur_ms != 0) {
                if (uxQueueMessagesWaiting(s_queue)) goto next_cmd;
                tone_on(n->freq, eff_duty);
                vTaskDelay(pdMS_TO_TICKS(n->dur_ms));
                tone_off();
                n++;
            }
        } else if (cmd.kind == BUZZER_CMD_PREVIEW_SEQ) {
            for (size_t i = 0; i < cmd.preview_count; ++i) {
                if (uxQueueMessagesWaiting(s_queue)) goto next_cmd;

                const display_buzzer_note_t *n = &cmd.preview_notes[i];

                if (n->freq_hz > 0) {
                    tone_on(n->freq_hz, eff_duty);
                    vTaskDelay(pdMS_TO_TICKS(n->dur_ms));
                    tone_off();
                } else {
                    tone_off();
                    vTaskDelay(pdMS_TO_TICKS(n->dur_ms));
                }

                if (n->pause_ms > 0) {
                    vTaskDelay(pdMS_TO_TICKS(n->pause_ms));
                }
            }
        }
        next_cmd:;
    }
}

#define DUTY_PCT(pct) ((uint16_t)((pct) * DISPLAY_BUZZER_DUTY / 100U))

static void play_vol(const buzzer_note_t *notes, uint16_t duty)
{
    if (!s_initialized || !s_enabled) return;
    buzzer_cmd_t cmd = {
        .kind = BUZZER_CMD_LEGACY_SEQ,
        .legacy_notes = notes,
        .duty = duty,
    };
    xQueueOverwrite(s_queue, &cmd);
}

/* ── Public API ── */

esp_err_t display_buzzer_init(void)
{
    if (s_initialized) return ESP_OK;

    ledc_timer_config_t timer = {
        .speed_mode      = DISPLAY_BUZZER_LEDC_MODE,
        .timer_num       = DISPLAY_BUZZER_LEDC_TIMER,
        .duty_resolution = DISPLAY_BUZZER_LEDC_BITS,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Timer init fehler: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t ch = {
        .gpio_num   = DISPLAY_BUZZER_GPIO,
        .speed_mode = DISPLAY_BUZZER_LEDC_MODE,
        .channel    = DISPLAY_BUZZER_LEDC_CHANNEL,
        .timer_sel  = DISPLAY_BUZZER_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Channel init fehler: %s", esp_err_to_name(err));
        return err;
    }

    s_queue = xQueueCreate(1, sizeof(buzzer_cmd_t));
    xTaskCreate(buzzer_task, "disp_buzz", 2048, NULL, 3, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "Init OK – GPIO%d LEDC Timer%d CH%d",
             DISPLAY_BUZZER_GPIO, DISPLAY_BUZZER_LEDC_TIMER, DISPLAY_BUZZER_LEDC_CHANNEL);
    return ESP_OK;
}

void display_buzzer_click(void)
{
    if (!s_initialized || !s_click_enabled) return;
    buzzer_cmd_t cmd = {
        .kind = BUZZER_CMD_LEGACY_SEQ,
        .legacy_notes = s_seq_click,
        .duty = DUTY_PCT(15),
    };
    xQueueOverwrite(s_queue, &cmd);
}
void display_buzzer_warn(void)    { play_vol(s_seq_warn,    DUTY_PCT(50)); }
void display_buzzer_alarm(void)   { play_vol(s_seq_alarm,   DUTY_PCT(50)); }
void display_buzzer_startup(void) { play_vol(s_seq_startup, DUTY_PCT(50)); }
void display_buzzer_success(void) { play_vol(s_seq_success, DUTY_PCT(50)); }
void display_buzzer_error(void)   { play_vol(s_seq_error,   DUTY_PCT(50)); }

void display_buzzer_set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    /* 50% Duty = maximale Lautstärke beim Piezo (symmetrische Rechteckwelle) */
    s_duty = (uint16_t)((pct * DISPLAY_BUZZER_DUTY) / 100U);
    if (s_duty < 1 && pct > 0) s_duty = 1;
}

esp_err_t display_buzzer_play_preview(const display_buzzer_note_t *notes, size_t note_count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!notes || note_count == 0 || note_count > DISPLAY_BUZZER_MAX_PREVIEW_NOTES) {
        return ESP_ERR_INVALID_ARG;
    }

    buzzer_cmd_t cmd = {
        .kind = BUZZER_CMD_PREVIEW_SEQ,
        .preview_count = note_count,
    };
    for (size_t i = 0; i < note_count; ++i) {
        cmd.preview_notes[i] = notes[i];
    }

    if (xQueueOverwrite(s_queue, &cmd) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void display_buzzer_stop(void)
{
    if (!s_initialized) return;

    buzzer_cmd_t cmd = {
        .kind = BUZZER_CMD_STOP,
    };
    xQueueOverwrite(s_queue, &cmd);
}

void display_buzzer_set_enabled(bool enable)       { s_enabled = enable; }
bool display_buzzer_is_enabled(void)               { return s_enabled; }
void display_buzzer_set_click_enabled(bool enable)  { s_click_enabled = enable; }
bool display_buzzer_is_click_enabled(void)           { return s_click_enabled; }
