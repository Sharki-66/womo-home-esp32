#include "buzzer.h"
#include "sensor_config.h"

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "buzzer";

/* ── Interne Typen ── */
typedef struct {
    const buzzer_note_t *notes;  /* NULL → simple tone */
    uint16_t             freq;   /* für simple tone */
    uint16_t             dur;    /* für simple tone */
    uint8_t              repeat;
} buzzer_cmd_t;

static QueueHandle_t s_queue;
static TaskHandle_t  s_task;
static bool          s_initialized = false;

/* ── Vordefinierte Melodien ── */

static const buzzer_note_t s_melody_startup[] = {
    { NOTE_C4, 120 }, { NOTE_E4, 120 }, { NOTE_G4, 120 }, { NOTE_C5, 200 },
    { NOTE_REST, 0 }
};

static const buzzer_note_t s_melody_level_ok[] = {
    { NOTE_G4, 120 }, { NOTE_C5, 250 },
    { NOTE_REST, 0 }
};

static const buzzer_note_t s_melody_level_warn[] = {
    { NOTE_A4, 80 }, { NOTE_REST, 60 }, { NOTE_A4, 80 }, { NOTE_REST, 60 }, { NOTE_A4, 80 },
    { NOTE_REST, 0 }
};

static const buzzer_note_t s_melody_beep[] = {
    { 1000, 200 },
    { NOTE_REST, 0 }
};

static const buzzer_note_t s_melody_double_beep[] = {
    { 1000, 100 }, { NOTE_REST, 80 }, { 1000, 100 },
    { NOTE_REST, 0 }
};

static const buzzer_note_t s_melody_alarm[] = {
    { NOTE_A5, 500 }, { NOTE_REST, 100 },
    { NOTE_A5, 500 }, { NOTE_REST, 100 },
    { NOTE_A5, 500 },
    { NOTE_REST, 0 }
};

/* ── Low-level Tonausgabe ── */

static void tone_on(uint16_t freq_hz)
{
    if (freq_hz == 0) {
        ledc_stop(SENSOR_BUZZER_LEDC_MODE, SENSOR_BUZZER_LEDC_CHANNEL, 0);
        return;
    }
    ledc_set_freq(SENSOR_BUZZER_LEDC_MODE, SENSOR_BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(SENSOR_BUZZER_LEDC_MODE, SENSOR_BUZZER_LEDC_CHANNEL, SENSOR_BUZZER_DUTY);
    ledc_update_duty(SENSOR_BUZZER_LEDC_MODE, SENSOR_BUZZER_LEDC_CHANNEL);
}

static void tone_off(void)
{
    ledc_stop(SENSOR_BUZZER_LEDC_MODE, SENSOR_BUZZER_LEDC_CHANNEL, 0);
}

/* ── Task ── */

static void buzzer_task(void *arg)
{
    buzzer_cmd_t cmd;
    for (;;) {
        if (!xQueueReceive(s_queue, &cmd, portMAX_DELAY)) continue;

        uint8_t runs = (cmd.repeat == 0) ? 1 : cmd.repeat;

        if (cmd.notes) {
            /* Melodie-Modus */
            for (uint8_t r = 0; r < runs; r++) {
                const buzzer_note_t *n = cmd.notes;
                while (n->freq_hz != 0 || n->dur_ms != 0) {
                    /* Prüfen ob neue Nachricht wartet → sofort abbrechen */
                    if (uxQueueMessagesWaiting(s_queue)) goto next_cmd;
                    tone_on(n->freq_hz);
                    vTaskDelay(pdMS_TO_TICKS(n->dur_ms));
                    tone_off();
                    n++;
                }
            }
        } else {
            /* Einzel-Ton */
            tone_on(cmd.freq);
            vTaskDelay(pdMS_TO_TICKS(cmd.dur));
            tone_off();
        }
        next_cmd:;
    }
}

/* ══════════════════════════════════════════════
   Public API
══════════════════════════════════════════════ */

esp_err_t buzzer_init(void)
{
    if (s_initialized) return ESP_OK;

    ledc_timer_config_t timer = {
        .speed_mode       = SENSOR_BUZZER_LEDC_MODE,
        .timer_num        = SENSOR_BUZZER_LEDC_TIMER,
        .duty_resolution  = SENSOR_BUZZER_LEDC_BITS,
        .freq_hz          = 1000,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC Timer init fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t ch = {
        .gpio_num   = SENSOR_BUZZER_GPIO,
        .speed_mode = SENSOR_BUZZER_LEDC_MODE,
        .channel    = SENSOR_BUZZER_LEDC_CHANNEL,
        .timer_sel  = SENSOR_BUZZER_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC Channel init fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    s_queue = xQueueCreate(1, sizeof(buzzer_cmd_t));
    xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 3, &s_task);

    s_initialized = true;
    ESP_LOGI(TAG, "Init OK – GPIO%d, Timer%d, CH%d",
             SENSOR_BUZZER_GPIO, SENSOR_BUZZER_LEDC_TIMER, SENSOR_BUZZER_LEDC_CHANNEL);
    return ESP_OK;
}

void buzzer_play(const buzzer_note_t *notes, uint8_t repeat)
{
    if (!s_initialized) return;
    buzzer_cmd_t cmd = { .notes = notes, .repeat = repeat };
    xQueueOverwrite(s_queue, &cmd);   /* ältere Sequenz sofort ersetzen */
}

void buzzer_tone(uint16_t freq_hz, uint16_t dur_ms)
{
    if (!s_initialized) return;
    buzzer_cmd_t cmd = { .notes = NULL, .freq = freq_hz, .dur = dur_ms };
    xQueueOverwrite(s_queue, &cmd);
}

void buzzer_stop(void)
{
    if (!s_initialized) return;
    /* Leere Melodie (nur Terminator) sofort einreihen */
    static const buzzer_note_t stop_seq[] = {{ NOTE_REST, 0 }};
    buzzer_play(stop_seq, 1);
}

void buzzer_beep(void)        { buzzer_play(s_melody_beep,        1); }
void buzzer_double_beep(void) { buzzer_play(s_melody_double_beep, 1); }
void buzzer_alarm(void)       { buzzer_play(s_melody_alarm,       1); }

void buzzer_melody_startup(void)    { buzzer_play(s_melody_startup,    1); }
void buzzer_melody_level_ok(void)   { buzzer_play(s_melody_level_ok,   1); }
void buzzer_melody_level_warn(void) { buzzer_play(s_melody_level_warn, 1); }
