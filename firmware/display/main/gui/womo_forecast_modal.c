/*
 * womo_forecast_modal.c – 5-Tage Wetter-Vorhersage Modal
 *
 * Layout: 640×420 px Panel (identisch mit anderen Modals).
 * 5 Tages-Spalten à 118 px + 8 px Rand beiderseits.
 * Jede Spalte: Wochentag, Datum, Wetter-Icon (64×64), Max/Min-Temp,
 * Niederschlag, Wind.
 */

#include "womo_forecast_modal.h"
#include "womo_locale.h"
#include "storage/womo_sd.h"
#include "network/womo_meteoalarm.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "womo_fonts_german.h"

static const char *TAG = "forecast_modal";

/* ── Dimensionen ──────────────────────────────────────────── */
#define MODAL_W     640
#define MODAL_H     420
#define HDR_H        48
#define COL_COUNT     5
#define COL_W       118   /* Spaltenbreite */
#define COL_GAP       5   /* Abstand zwischen Spalten */
#define SIDE_PAD      8   /* Rand links + rechts */
#define ICON_SIZE    64

/* ── Meteoalarm-Warnbereich (unterhalb der 5 Spalten) ─────── */
#define WARN_MAX_DISPLAY   3
#define WARN_Y0            252   /* COL_CONTENT_Y0(4) + COL_CONTENT_H(242) + 6 */
#define WARN_DOT_SZ        12
#define WARN_ROW_H         24

#define WEATHER_ICON_DIR "/sdcard/images/weather-icons/black"
#define DATAICON_DIR      "/sdcard/images"

/* ── Interne Zustandsvariablen ───────────────────────────── */
static lv_obj_t *s_overlay   = NULL;
static lv_obj_t *s_panel     = NULL;
static lv_obj_t *s_title_lbl = NULL;
static lv_obj_t *s_loc_lbl   = NULL;  /* Ortsname im Header */
/* Header-Sonnenzeiten: [☀-ico] [↑-ico] [SR-Label] [↓-ico] [SS-Label] */
static lv_obj_t *s_hdr_ico_sun = NULL;
static lv_obj_t *s_hdr_ico_up  = NULL;
static lv_obj_t *s_hdr_lbl_sr  = NULL;  /* "HH:MM" Sonnenaufgang */
static lv_obj_t *s_hdr_ico_dn  = NULL;
static lv_obj_t *s_hdr_lbl_ss  = NULL;  /* "HH:MM" Sonnenuntergang */

/* Gespeicherte Standort-/Sonnenwerte für nachträgliches Öffnen */
static char    s_location[64] = "";
static uint8_t s_sr_h = 0, s_sr_m = 0, s_ss_h = 0, s_ss_m = 0;

/* Pro Spalte: Icon-Objekt und Temp/Prec/Wind-Labels */
static lv_obj_t *s_day_lbl  [COL_COUNT];
static lv_obj_t *s_date_lbl [COL_COUNT];
static lv_obj_t *s_icon_img [COL_COUNT];
static lv_obj_t *s_tmax_lbl [COL_COUNT];
static lv_obj_t *s_tmin_lbl [COL_COUNT];
static lv_obj_t *s_prec_lbl [COL_COUNT];
static lv_obj_t *s_wind_lbl [COL_COUNT];
static lv_obj_t *s_rain_lbl [COL_COUNT];
static lv_obj_t *s_sun_lbl  [COL_COUNT];

/* PNG-Puffer für Icons (wird beim Schließen freigegeben) */
typedef struct {
    uint8_t       *png_buf;
    lv_image_dsc_t img_dsc;
} day_icon_t;
static day_icon_t s_icons[COL_COUNT];

/* Kleine Daten-Icons (einmal geladen, alle Spalten teilen sich das Bild)
 * SD-Dateien: WEATHER_ICON_DIR/ic_rain_prob.png  – Regentropfen (Wahrscheinlichkeit)
 *                              ic_precip.png      – Regen mm
 *                              ic_sun_h.png       – Sonne (Stunden)
 *                              ic_wind.png        – Wind
 * Empfohlene Größe: 16×16 px, RGBA-PNG */
#define DATA_ICON_SIZE  16

typedef struct { uint8_t *buf; lv_image_dsc_t dsc; } small_icon_t;

static small_icon_t s_sico_rain_prob = {0};
static small_icon_t s_sico_sun_h     = {0};
static small_icon_t s_sico_wind      = {0};
static small_icon_t s_sico_sun_up    = {0};  /* up.png – Sonnenaufgang */
static small_icon_t s_sico_sun_dn    = {0};  /* down.png – Sonnenuntergang */

/* Kleine Icon-Objekte pro Spalte */
static lv_obj_t *s_rain_ico[COL_COUNT];
static lv_obj_t *s_prec_ico[COL_COUNT];
static lv_obj_t *s_sunh_ico[COL_COUNT];
static lv_obj_t *s_wind_ico[COL_COUNT];

/* ── Meteoalarm-Warnbereich ─────────────────────────────────── */
static womo_meteoalarm_result_t s_meteoalarm     = {0};
static lv_obj_t *s_warn_hdr_lbl                  = NULL;
static lv_obj_t *s_warn_dot [WARN_MAX_DISPLAY]   = {0};
static lv_obj_t *s_warn_text[WARN_MAX_DISPLAY]   = {0};
static lv_obj_t *s_warn_more_lbl                 = NULL;

/* ── WMO-Code → Icon-Dateiname ──────────────────────────── */
static const char *forecast_wmo_icon(int code)
{
    if (code == 0)                        return "sunny.png";
    if (code <= 2)                        return "partlycloudy.png";
    if (code == 3)                        return "cloudy.png";
    if (code == 45 || code == 48)         return "fog.png";
    if (code >= 51 && code <= 57)         return "chancerain.png";
    if (code >= 61 && code <= 65)         return "rain.png";
    if (code == 66 || code == 67)         return "sleet.png";
    if (code == 71)                       return "chancesnow.png";
    if (code >= 73 && code <= 77)         return "snow.png";
    if (code >= 80 && code <= 82)         return "rain.png";
    if (code == 85)                       return "chancesnow.png";
    if (code == 86)                       return "snow.png";
    if (code >= 95)                       return "tstorms.png";
    return "unknown.png";
}

/* ── Datum "YYYY-MM-DD" → Wochentag (0=So…6=Sa) ─────────── */
static int date_weekday(const char *date)
{
    if (!date || strlen(date) < 10) return -1;
    struct tm tm = {0};
    int y, m, d;
    if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) return -1;
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) return -1;
    return (int)tm.tm_wday;   /* mktime füllt tm_wday */
}

/* ── Icon vom SD-Laufwerk laden ─────────────────────────── */
static void load_icon(int col, const char *filename)
{
    if (col < 0 || col >= COL_COUNT) return;

    /* Alten Puffer freigeben */
    if (s_icons[col].png_buf) {
        lv_image_cache_drop(&s_icons[col].img_dsc);
        heap_caps_free(s_icons[col].png_buf);
        s_icons[col].png_buf = NULL;
    }

    if (!womo_sd_is_mounted() || !filename) {
        lv_obj_add_flag(s_icon_img[col], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", WEATHER_ICON_DIR, filename);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        /* Retry einmal (SPIFFS/FatFS busy) */
        vTaskDelay(pdMS_TO_TICKS(50));
        fp = fopen(path, "rb");
    }
    if (!fp) {
        ESP_LOGW(TAG, "Icon not found: %s", path);
        lv_obj_add_flag(s_icon_img[col], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);

    if (sz <= 0 || sz > 32768) {
        ESP_LOGW(TAG, "Unexpected icon size: %ld", sz);
        fclose(fp);
        lv_obj_add_flag(s_icon_img[col], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = malloc((size_t)sz);
    }
    if (!buf) {
        ESP_LOGE(TAG, "OOM for icon %s", filename);
        fclose(fp);
        lv_obj_add_flag(s_icon_img[col], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        ESP_LOGW(TAG, "Short read: %s", path);
        free(buf);
        fclose(fp);
        lv_obj_add_flag(s_icon_img[col], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    fclose(fp);

    s_icons[col].png_buf              = buf;
    s_icons[col].img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_icons[col].img_dsc.header.w     = 0;
    s_icons[col].img_dsc.header.h     = 0;
    s_icons[col].img_dsc.data_size    = (uint32_t)sz;
    s_icons[col].img_dsc.header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
    s_icons[col].img_dsc.data         = buf;

    lv_img_set_src(s_icon_img[col], &s_icons[col].img_dsc);
    lv_obj_clear_flag(s_icon_img[col], LV_OBJ_FLAG_HIDDEN);
}

/* ── Puffer aller Icons freigeben ───────────────────────── */
static void free_icons(void)
{
    for (int i = 0; i < COL_COUNT; i++) {
        if (s_icons[i].png_buf) {
            lv_image_cache_drop(&s_icons[i].img_dsc);
            heap_caps_free(s_icons[i].png_buf);
            s_icons[i].png_buf = NULL;
        }
    }
}

/* ── Kleinen Daten-Icon von SD laden ───────────────────── */
static void load_small_icon_file(small_icon_t *si, const char *filename)
{
    if (!si || !womo_sd_is_mounted() || !filename) return;
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", DATAICON_DIR, filename);
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0 || sz > 16384) { fclose(fp); return; }
    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc((size_t)sz);
    if (!buf) { fclose(fp); return; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); fclose(fp); return; }
    fclose(fp);
    si->buf              = buf;
    si->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    si->dsc.header.w     = 0;
    si->dsc.header.h     = 0;
    si->dsc.data_size    = (uint32_t)sz;
    si->dsc.header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
    si->dsc.data         = buf;
    ESP_LOGI(TAG, "Data icon geladen: %s (%ld B)", filename, sz);
}

static void load_data_icons(void)
{
    load_small_icon_file(&s_sico_rain_prob, "ic_rain.png");  /* Regenwahrscheinlichkeit % */
    load_small_icon_file(&s_sico_sun_h,     "ic_sun.png");   /* Sonnenstunden + Header-Sonne */
    load_small_icon_file(&s_sico_wind,      "ic_wind.png");  /* Wind m/s */
    load_small_icon_file(&s_sico_sun_up,    "up.png");       /* Sonnenaufgang im Header */
    load_small_icon_file(&s_sico_sun_dn,    "down.png");     /* Sonnenuntergang im Header */
}

static void free_data_icons(void)
{
    small_icon_t *all[] = { &s_sico_rain_prob, &s_sico_sun_h, &s_sico_wind,
                            &s_sico_sun_up, &s_sico_sun_dn };
    for (int k = 0; k < 5; k++) {
        if (all[k]->buf) {
            lv_image_cache_drop(&all[k]->dsc);
            heap_caps_free(all[k]->buf);
            all[k]->buf = NULL;
        }
    }
}

static void apply_data_icon_sources(void)
{
    for (int i = 0; i < COL_COUNT; i++) {
        if (s_sico_rain_prob.buf && s_rain_ico[i]) {
            lv_img_set_src(s_rain_ico[i], &s_sico_rain_prob.dsc);
            lv_obj_clear_flag(s_rain_ico[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_sico_sun_h.buf && s_sunh_ico[i]) {
            lv_img_set_src(s_sunh_ico[i], &s_sico_sun_h.dsc);
            lv_obj_clear_flag(s_sunh_ico[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_sico_wind.buf && s_wind_ico[i]) {
            lv_img_set_src(s_wind_ico[i], &s_sico_wind.dsc);
            lv_obj_clear_flag(s_wind_ico[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* Header-Sonnen-Icons */
    if (s_sico_sun_h.buf  && s_hdr_ico_sun) { lv_img_set_src(s_hdr_ico_sun, &s_sico_sun_h.dsc);  lv_obj_clear_flag(s_hdr_ico_sun, LV_OBJ_FLAG_HIDDEN); }
    if (s_sico_sun_up.buf && s_hdr_ico_up)  { lv_img_set_src(s_hdr_ico_up,  &s_sico_sun_up.dsc); lv_obj_clear_flag(s_hdr_ico_up,  LV_OBJ_FLAG_HIDDEN); }
    if (s_sico_sun_dn.buf && s_hdr_ico_dn)  { lv_img_set_src(s_hdr_ico_dn,  &s_sico_sun_dn.dsc); lv_obj_clear_flag(s_hdr_ico_dn,  LV_OBJ_FLAG_HIDDEN); }
}

/* ── Spalten mit Forecast-Daten füllen ─────────────────── */
static void fill_columns(const womo_weather_forecast_t *forecast)
{
    for (int i = 0; i < COL_COUNT; i++) {
        if (!forecast || !forecast->valid || !forecast->day[i].valid) {
            lv_label_set_text(s_day_lbl[i],  "--");
            lv_label_set_text(s_date_lbl[i], "--");
            lv_label_set_text(s_tmax_lbl[i], "--°");
            lv_label_set_text(s_tmin_lbl[i], "--°");
            lv_label_set_text(s_prec_lbl[i], "--");
            lv_label_set_text(s_wind_lbl[i], "--");
            lv_label_set_text(s_rain_lbl[i], "--");
            lv_label_set_text(s_sun_lbl[i],  "--");
            lv_obj_add_flag(s_icon_img[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const womo_weather_forecast_day_t *d = &forecast->day[i];

        /* Wochentag */
        int wd = date_weekday(d->date);
        if (wd >= 0) {
            lv_label_set_text(s_day_lbl[i], womo_locale_get_string(STR_WEEKDAY_SUN + wd));
        } else {
            lv_label_set_text(s_day_lbl[i], d->date);
        }

        /* Datum "TT.MM" */
        {
            int y, m, day;
            char date_buf[8] = "--";
            if (sscanf(d->date, "%d-%d-%d", &y, &m, &day) == 3) {
                snprintf(date_buf, sizeof(date_buf), "%02d.%02d.", day, m);
            }
            lv_label_set_text(s_date_lbl[i], date_buf);
        }

        /* Temperaturen */
        {
            char buf[10];
            snprintf(buf, sizeof(buf), "%.0f°", d->temp_max_c);
            lv_label_set_text(s_tmax_lbl[i], buf);
            snprintf(buf, sizeof(buf), "%.0f°", d->temp_min_c);
            lv_label_set_text(s_tmin_lbl[i], buf);
        }

        /* Regenwahrscheinlichkeit */
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d %%", d->rain_prob_pct);
            lv_label_set_text(s_rain_lbl[i], buf);
        }

        /* Niederschlag */
        {
            char buf[12];
            if (d->precip_mm >= 0.1f) {
                snprintf(buf, sizeof(buf), "%.1f mm", d->precip_mm);
            } else {
                snprintf(buf, sizeof(buf), "0 mm");
            }
            lv_label_set_text(s_prec_lbl[i], buf);
        }

        /* Sonnenstunden */
        {
            char buf[10];
            snprintf(buf, sizeof(buf), "%.1f h", d->sunshine_h);
            lv_label_set_text(s_sun_lbl[i], buf);
        }

        /* Wind */
        {
            char buf[12];
            snprintf(buf, sizeof(buf), "%.1f m/s", d->wind_max_ms);
            lv_label_set_text(s_wind_lbl[i], buf);
        }

        /* Icon */
        load_icon(i, forecast_wmo_icon(d->weather_code));
    }
}

/* ── Hilfsfunktion: Modal schließen ─────────────────────── */
static void close_modal(void)
{
    if (!s_overlay) return;
    free_icons();
    free_data_icons();
    lv_obj_del_async(s_overlay);
    s_overlay      = NULL;
    s_panel        = NULL;
    s_title_lbl    = NULL;
    s_loc_lbl      = NULL;
    s_hdr_ico_sun  = NULL;
    s_hdr_ico_up   = NULL;
    s_hdr_lbl_sr   = NULL;
    s_hdr_ico_dn   = NULL;
    s_hdr_lbl_ss   = NULL;
    s_warn_hdr_lbl = NULL;
    s_warn_more_lbl = NULL;
    for (int i = 0; i < WARN_MAX_DISPLAY; i++) {
        s_warn_dot[i]  = NULL;
        s_warn_text[i] = NULL;
    }
    for (int i = 0; i < COL_COUNT; i++) {
        s_day_lbl[i] = s_date_lbl[i] = s_icon_img[i] = NULL;
        s_tmax_lbl[i] = s_tmin_lbl[i] = s_prec_lbl[i] = s_wind_lbl[i] = NULL;
        s_rain_lbl[i] = s_sun_lbl[i] = NULL;
        s_rain_ico[i] = s_prec_ico[i] = s_sunh_ico[i] = s_wind_ico[i] = NULL;
    }
}

static void close_btn_cb(lv_event_t *e)   { (void)e; close_modal(); }

/* ── Meteoalarm-Warnbereich: Widgets aufbauen ───────────────── */
static void build_warn_section(lv_obj_t *col_frame)
{
    /* Trennlinie */
    lv_obj_t *sep = lv_obj_create(col_frame);
    lv_obj_set_size(sep, MODAL_W - 16, 1);
    lv_obj_set_pos(sep, 8, WARN_Y0 + 4);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x999999), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Kopfzeile ("Keine Wetterwarnungen" / "⚠ N Warnung(en)") */
    s_warn_hdr_lbl = lv_label_create(col_frame);
    lv_label_set_text(s_warn_hdr_lbl, "Keine Wetterwarnungen");
    lv_obj_set_style_text_font(s_warn_hdr_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_warn_hdr_lbl, lv_color_hex(0x546E7A), 0);
    lv_obj_set_size(s_warn_hdr_lbl, MODAL_W - 20, 16);
    lv_obj_set_pos(s_warn_hdr_lbl, 10, WARN_Y0 + 14);

    /* Warnung-Zeilen */
    for (int i = 0; i < WARN_MAX_DISPLAY; i++) {
        int row_y = WARN_Y0 + 34 + i * WARN_ROW_H;

        /* Severity-Indikator (gefüllter Kreis) */
        s_warn_dot[i] = lv_obj_create(col_frame);
        lv_obj_remove_style_all(s_warn_dot[i]);
        lv_obj_set_size(s_warn_dot[i], WARN_DOT_SZ, WARN_DOT_SZ);
        lv_obj_set_pos(s_warn_dot[i], 10, row_y + (WARN_ROW_H - WARN_DOT_SZ) / 2);
        lv_obj_set_style_radius(s_warn_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_warn_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_warn_dot[i], lv_color_hex(0xFB8C00), 0);
        lv_obj_clear_flag(s_warn_dot[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_warn_dot[i], LV_OBJ_FLAG_HIDDEN);

        /* Text: "EVENT – Headline  |  bis EXPIRES" */
        s_warn_text[i] = lv_label_create(col_frame);
        lv_label_set_text(s_warn_text[i], "");
        lv_label_set_long_mode(s_warn_text[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(s_warn_text[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_warn_text[i], lv_color_hex(0x333333), 0);
        lv_obj_set_size(s_warn_text[i], MODAL_W - 36, 16);
        lv_obj_set_pos(s_warn_text[i], 28, row_y + (WARN_ROW_H - 16) / 2);
        lv_obj_add_flag(s_warn_text[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* "+ N weitere..." Label */
    s_warn_more_lbl = lv_label_create(col_frame);
    lv_label_set_text(s_warn_more_lbl, "");
    lv_obj_set_style_text_font(s_warn_more_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_warn_more_lbl, lv_color_hex(0x757575), 0);
    lv_obj_set_size(s_warn_more_lbl, 300, 14);
    lv_obj_set_pos(s_warn_more_lbl, 28, WARN_Y0 + 34 + WARN_MAX_DISPLAY * WARN_ROW_H + 2);
    lv_obj_add_flag(s_warn_more_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ── Meteoalarm-Warnbereich: Daten einsetzen ────────────────── */
static void update_warn_section(void)
{
    if (!s_warn_hdr_lbl) return;

    uint8_t count = s_meteoalarm.valid ? s_meteoalarm.count : 0;

    if (count == 0) {
        lv_label_set_text(s_warn_hdr_lbl, "Keine Wetterwarnungen");
        lv_obj_set_style_text_color(s_warn_hdr_lbl, lv_color_hex(0x546E7A), 0);
        for (int i = 0; i < WARN_MAX_DISPLAY; i++) {
            if (s_warn_dot[i])  lv_obj_add_flag(s_warn_dot[i],  LV_OBJ_FLAG_HIDDEN);
            if (s_warn_text[i]) lv_obj_add_flag(s_warn_text[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_warn_more_lbl) lv_obj_add_flag(s_warn_more_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Kopfzeile mit Anzahl */
    {
        char hdr[48];
        snprintf(hdr, sizeof(hdr), LV_SYMBOL_WARNING " %u Wetterwarnung%s (meteoalarm.org)",
                 count, count == 1 ? "" : "en");
        lv_label_set_text(s_warn_hdr_lbl, hdr);
        uint32_t hdr_color = (s_meteoalarm.max_severity >= WOMO_WARN_SEV_SEVERE)
                             ? 0xC62828 : 0xE65100;
        lv_obj_set_style_text_color(s_warn_hdr_lbl, lv_color_hex(hdr_color), 0);
    }

    /* Bis zu WARN_MAX_DISPLAY Zeilen */
    int shown = (count < WARN_MAX_DISPLAY) ? count : WARN_MAX_DISPLAY;
    for (int i = 0; i < shown; i++) {
        const womo_meteoalarm_warning_t *w = &s_meteoalarm.warnings[i];

        /* Farbe nach Severity */
        uint32_t dot_color = (w->severity >= WOMO_WARN_SEV_SEVERE) ? 0xC62828 : 0xFB8C00;
        lv_obj_set_style_bg_color(s_warn_dot[i], lv_color_hex(dot_color), 0);
        lv_obj_clear_flag(s_warn_dot[i],  LV_OBJ_FLAG_HIDDEN);

        /* Text aufbauen: "EVENT – Headline  |  bis EXPIRES" */
        char row_buf[120];
        if (w->event[0] && w->expires[0]) {
            snprintf(row_buf, sizeof(row_buf), "%s \xe2\x80\x93 %s  |  bis %s",
                     w->event, w->headline, w->expires);
        } else if (w->event[0]) {
            snprintf(row_buf, sizeof(row_buf), "%s \xe2\x80\x93 %s", w->event, w->headline);
        } else if (w->expires[0]) {
            snprintf(row_buf, sizeof(row_buf), "%s  |  bis %s", w->headline, w->expires);
        } else {
            snprintf(row_buf, sizeof(row_buf), "%s", w->headline);
        }
        lv_label_set_text(s_warn_text[i], row_buf);
        lv_obj_clear_flag(s_warn_text[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Restliche Zeilen ausblenden */
    for (int i = shown; i < WARN_MAX_DISPLAY; i++) {
        if (s_warn_dot[i])  lv_obj_add_flag(s_warn_dot[i],  LV_OBJ_FLAG_HIDDEN);
        if (s_warn_text[i]) lv_obj_add_flag(s_warn_text[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* "+ N weitere" wenn mehr als WARN_MAX_DISPLAY Warnungen */
    if (count > WARN_MAX_DISPLAY) {
        char more_buf[32];
        snprintf(more_buf, sizeof(more_buf), "+ %u weitere...", count - WARN_MAX_DISPLAY);
        lv_label_set_text(s_warn_more_lbl, more_buf);
        lv_obj_clear_flag(s_warn_more_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_warn_more_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  Public API                                                 */
/* ─────────────────────────────────────────────────────────── */

bool womo_forecast_modal_is_open(void)
{
    return s_overlay != NULL;
}

void womo_forecast_modal_close(void)
{
    close_modal();
}

void womo_forecast_modal_update(const womo_weather_forecast_t *forecast)
{
    if (!s_overlay) return;
    fill_columns(forecast);
}

void womo_forecast_modal_set_warnings(const womo_meteoalarm_result_t *result)
{
    if (result) {
        s_meteoalarm = *result;
    } else {
        memset(&s_meteoalarm, 0, sizeof(s_meteoalarm));
    }
    /* Wenn das Modal gerade offen ist → sofort aktualisieren */
    if (s_overlay) {
        update_warn_section();
    }
}

void womo_forecast_modal_set_location(const char *location,
                                      uint8_t sr_h, uint8_t sr_m,
                                      uint8_t ss_h, uint8_t ss_m)
{
    /* Werte immer speichern (auch wenn Modal noch nicht offen) */
    if (location && location[0]) {
        strlcpy(s_location, location, sizeof(s_location));
    }
    s_sr_h = sr_h; s_sr_m = sr_m;
    s_ss_h = ss_h; s_ss_m = ss_m;

    if (!s_overlay) return;

    if (s_loc_lbl && s_location[0]) {
        lv_label_set_text(s_loc_lbl, s_location);
    }
    /* Zeitlabels im Header aktualisieren */
    if (s_hdr_lbl_sr) {
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%02u:%02u", sr_h, sr_m);
        lv_label_set_text(s_hdr_lbl_sr, tbuf);
    }
    if (s_hdr_lbl_ss) {
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%02u:%02u", ss_h, ss_m);
        lv_label_set_text(s_hdr_lbl_ss, tbuf);
    }
}

void womo_forecast_modal_show(lv_obj_t *parent, const womo_weather_forecast_t *forecast)
{
    if (s_overlay) {
        lv_obj_move_foreground(s_overlay);
        fill_columns(forecast);
        return;
    }

    /* ── Overlay ────────────────────────────────────────── */
    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* ── Panel ──────────────────────────────────────────── */
    s_panel = lv_obj_create(s_overlay);
    lv_obj_set_size(s_panel, MODAL_W, MODAL_H);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 12, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* ── Kopfzeile ──────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(s_panel);
    lv_obj_set_size(header, MODAL_W, HDR_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Titel "5-Tage Vorhersage" links im Header – wie Sonnenzeiten: feste Höhe + manuelles Y */
    s_title_lbl = lv_label_create(header);
    lv_label_set_text(s_title_lbl, womo_locale_get_string(STR_FORECAST_TITLE));
    lv_obj_set_style_text_color(s_title_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_title_lbl, &lv_font_montserrat_16_german, 0);
    lv_obj_set_size(s_title_lbl, 200, 20);
    lv_obj_set_style_text_align(s_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(s_title_lbl, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_title_lbl, 16, (HDR_H - 20) / 2);

    /* Ortsname mittig im Header – wie Sonnenzeiten: feste Höhe + manuelles Y */
    s_loc_lbl = lv_label_create(header);
    lv_label_set_text(s_loc_lbl, s_location[0] ? s_location : "");
    lv_obj_set_style_text_color(s_loc_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_loc_lbl, &lv_font_montserrat_20_german, 0);
    lv_obj_set_size(s_loc_lbl, 220, 24);
    lv_obj_set_style_text_align(s_loc_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_loc_lbl, (MODAL_W - 220) / 2 - 20, (HDR_H - 24) / 2);

    /* Sonnenzeiten rechts im Header: [☀] [↑] [SR] [↓] [SS]
     * Layout von rechts (Close-Btn endet bei x=632):
     * SS-Zeit x=554 w=38 | ↓-ico x=538 | SR-Zeit x=498 w=38 | ↑-ico x=482 | ☀-ico x=466
     * Alle Icons 14×14 px, y=(48-14)/2=17; Labels Höhe 16, y=(48-16)/2=16             */
    #define HDR_ICO_SZ  14
    #define HDR_ICO_Y   ((HDR_H - HDR_ICO_SZ) / 2)
    #define HDR_LBL_Y   ((HDR_H - 16) / 2)

    s_hdr_ico_sun = lv_img_create(header);
    lv_obj_set_size(s_hdr_ico_sun, HDR_ICO_SZ, HDR_ICO_SZ);
    lv_obj_set_pos(s_hdr_ico_sun, MODAL_W - 190, HDR_ICO_Y);
    lv_obj_add_flag(s_hdr_ico_sun, LV_OBJ_FLAG_HIDDEN);

    s_hdr_ico_up = lv_img_create(header);
    lv_obj_set_size(s_hdr_ico_up, HDR_ICO_SZ, HDR_ICO_SZ);
    lv_obj_set_pos(s_hdr_ico_up, MODAL_W - 172, HDR_ICO_Y);
    lv_obj_add_flag(s_hdr_ico_up, LV_OBJ_FLAG_HIDDEN);

    s_hdr_lbl_sr = lv_label_create(header);
    { char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%02u:%02u", s_sr_h, s_sr_m); lv_label_set_text(s_hdr_lbl_sr, tbuf); }
    lv_obj_set_style_text_color(s_hdr_lbl_sr, lv_color_hex(0xFFEE88), 0);
    lv_obj_set_style_text_font(s_hdr_lbl_sr, &lv_font_montserrat_14, 0);
    lv_obj_set_size(s_hdr_lbl_sr, 38, 16);
    lv_obj_set_pos(s_hdr_lbl_sr, MODAL_W - 156, HDR_LBL_Y);

    s_hdr_ico_dn = lv_img_create(header);
    lv_obj_set_size(s_hdr_ico_dn, HDR_ICO_SZ, HDR_ICO_SZ);
    lv_obj_set_pos(s_hdr_ico_dn, MODAL_W - 114, HDR_ICO_Y);
    lv_obj_add_flag(s_hdr_ico_dn, LV_OBJ_FLAG_HIDDEN);

    s_hdr_lbl_ss = lv_label_create(header);
    { char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%02u:%02u", s_ss_h, s_ss_m); lv_label_set_text(s_hdr_lbl_ss, tbuf); }
    lv_obj_set_style_text_color(s_hdr_lbl_ss, lv_color_hex(0xFFEE88), 0);
    lv_obj_set_style_text_font(s_hdr_lbl_ss, &lv_font_montserrat_14, 0);
    lv_obj_set_size(s_hdr_lbl_ss, 38, 16);
    lv_obj_set_pos(s_hdr_lbl_ss, MODAL_W - 96, HDR_LBL_Y);

    /* Schliessen-Button */
    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 36, 32);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xC62828), 0);
    lv_obj_set_style_radius(close_btn, 4, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, close_btn_cb, LV_EVENT_CLICKED, NULL);

    /* ── Inhaltsbereich: exakt wie Header aufgebaut (lv_obj_set_pos, radius=0, pad=0) ── */
    /* Inhaltshöhe: Summe aller y-Schritte + letzte Elementhöhe = 242 px              */
    #define COL_CONTENT_H    242
    #define COL_CONTENT_Y0   4   /* direkt unter dem Header */
    /* col_frame: direkt auf s_panel, identische Struktur wie header */
    lv_obj_t *col_frame = lv_obj_create(s_panel);
    lv_obj_set_size(col_frame, MODAL_W, MODAL_H - HDR_H);
    lv_obj_set_pos(col_frame, 0, HDR_H);
    lv_obj_set_style_bg_opa(col_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_frame, 0, 0);
    lv_obj_set_style_pad_all(col_frame, 0, 0);
    lv_obj_set_style_radius(col_frame, 0, 0);
    lv_obj_clear_flag(col_frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    ESP_LOGI(TAG, "col_frame: HDR_H=%d COL_CONTENT_Y0=%d COL_CONTENT_H=%d",
             HDR_H, COL_CONTENT_Y0, COL_CONTENT_H);

    /* ── Trennlinien zwischen Spalten ── */
    for (int i = 1; i < COL_COUNT; i++) {
        int x_div = SIDE_PAD + i * (COL_W + COL_GAP) - COL_GAP / 2 - 1;
        lv_obj_t *div = lv_obj_create(col_frame);
        lv_obj_set_size(div, 1, COL_CONTENT_H);
        lv_obj_set_pos(div, x_div, COL_CONTENT_Y0);
        lv_obj_set_style_bg_color(div, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(div, 0, 0);
        lv_obj_set_style_pad_all(div, 0, 0);
        lv_obj_set_style_radius(div, 0, 0);
        lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    /* ── 5 Tages-Spalten aufbauen ── */
    for (int i = 0; i < COL_COUNT; i++) {
        int x = SIDE_PAD + i * (COL_W + COL_GAP);
        int y = COL_CONTENT_Y0;

        /* Heute-Highlight */
        if (i == 0) {
            lv_obj_t *hl = lv_obj_create(col_frame);
            lv_obj_set_size(hl, COL_W + 2, COL_CONTENT_H + 4);
            lv_obj_set_pos(hl, x - 1, COL_CONTENT_Y0 - 2);
            lv_obj_set_style_bg_color(hl, lv_color_hex(0xE3F2FD), 0);
            lv_obj_set_style_bg_opa(hl, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(hl, 4, 0);
            lv_obj_set_style_border_width(hl, 1, 0);
            lv_obj_set_style_border_color(hl, lv_color_hex(0x90CAF9), 0);
            lv_obj_set_style_pad_all(hl, 0, 0);
            lv_obj_clear_flag(hl, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }

        /* Wochentag */
        s_day_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_day_lbl[i], "--");
        lv_obj_set_style_text_font(s_day_lbl[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_day_lbl[i], lv_color_hex(0x1565C0), 0);
        lv_obj_set_size(s_day_lbl[i], COL_W, 20);
        lv_obj_set_style_text_align(s_day_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_day_lbl[i], x, y);
        y += 22;

        /* Datum */
        s_date_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_date_lbl[i], "--");
        lv_obj_set_style_text_font(s_date_lbl[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_date_lbl[i], lv_color_hex(0x757575), 0);
        lv_obj_set_size(s_date_lbl[i], COL_W, 16);
        lv_obj_set_style_text_align(s_date_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_date_lbl[i], x, y);
        y += 20;

        /* Icon */
        s_icon_img[i] = lv_img_create(col_frame);
        lv_obj_set_size(s_icon_img[i], ICON_SIZE, ICON_SIZE);
        lv_obj_set_pos(s_icon_img[i], x + (COL_W - ICON_SIZE) / 2, y);
        lv_obj_add_flag(s_icon_img[i], LV_OBJ_FLAG_HIDDEN);
        y += ICON_SIZE + 6;

        /* Max-Temperatur */
        s_tmax_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_tmax_lbl[i], "--°");
        lv_obj_set_style_text_font(s_tmax_lbl[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_tmax_lbl[i], lv_color_hex(0xC62828), 0);
        lv_obj_set_size(s_tmax_lbl[i], COL_W, 20);
        lv_obj_set_style_text_align(s_tmax_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_tmax_lbl[i], x, y);
        y += 22;

        /* Min-Temperatur */
        s_tmin_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_tmin_lbl[i], "--°");
        lv_obj_set_style_text_font(s_tmin_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_tmin_lbl[i], lv_color_hex(0x1565C0), 0);
        lv_obj_set_size(s_tmin_lbl[i], COL_W, 20);
        lv_obj_set_style_text_align(s_tmin_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_tmin_lbl[i], x, y);
        y += 22;

        /* ── Horizontale Trennlinie zwischen Temp-Block und Daten-Block ── */
        {
            lv_obj_t *hdiv = lv_obj_create(col_frame);
            lv_obj_set_size(hdiv, COL_W - 8, 1);
            lv_obj_set_pos(hdiv, x + 4, y + 2);
            lv_obj_set_style_bg_color(hdiv, lv_color_hex(0xBBBBBB), 0);
            lv_obj_set_style_bg_opa(hdiv, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(hdiv, 0, 0);
            lv_obj_set_style_pad_all(hdiv, 0, 0);
            lv_obj_set_style_radius(hdiv, 0, 0);
            lv_obj_clear_flag(hdiv, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
        y += 8;

        /* Regenwahrscheinlichkeit */
        s_rain_ico[i] = lv_img_create(col_frame);
        lv_obj_set_size(s_rain_ico[i], DATA_ICON_SIZE, DATA_ICON_SIZE);
        lv_obj_set_pos(s_rain_ico[i], x + 22, y + 1);
        lv_obj_add_flag(s_rain_ico[i], LV_OBJ_FLAG_HIDDEN);
        s_rain_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_rain_lbl[i], "--");
        lv_obj_set_style_text_font(s_rain_lbl[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_rain_lbl[i], lv_color_hex(0x0277BD), 0);
        lv_obj_set_size(s_rain_lbl[i], 70, 18);
        lv_obj_set_style_text_align(s_rain_lbl[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_rain_lbl[i], x + 40, y);
        y += 20;

        /* Niederschlag mm – kein Symbol, zentriert */
        s_prec_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_prec_lbl[i], "--");
        lv_obj_set_style_text_font(s_prec_lbl[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_prec_lbl[i], lv_color_hex(0x0277BD), 0);
        lv_obj_set_size(s_prec_lbl[i], COL_W - 8, 18);
        lv_obj_set_style_text_align(s_prec_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_prec_lbl[i], x + 4, y);
        y += 20;

        /* Sonnenstunden */
        s_sunh_ico[i] = lv_img_create(col_frame);
        lv_obj_set_size(s_sunh_ico[i], DATA_ICON_SIZE, DATA_ICON_SIZE);
        lv_obj_set_pos(s_sunh_ico[i], x + 22, y + 1);
        lv_obj_add_flag(s_sunh_ico[i], LV_OBJ_FLAG_HIDDEN);
        s_sun_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_sun_lbl[i], "--");
        lv_obj_set_style_text_font(s_sun_lbl[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_sun_lbl[i], lv_color_hex(0xF57F17), 0);
        lv_obj_set_size(s_sun_lbl[i], 70, 18);
        lv_obj_set_style_text_align(s_sun_lbl[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_sun_lbl[i], x + 40, y);
        y += 20;

        /* Wind */
        s_wind_ico[i] = lv_img_create(col_frame);
        lv_obj_set_size(s_wind_ico[i], DATA_ICON_SIZE, DATA_ICON_SIZE);
        lv_obj_set_pos(s_wind_ico[i], x + 22, y + 1);
        lv_obj_add_flag(s_wind_ico[i], LV_OBJ_FLAG_HIDDEN);
        s_wind_lbl[i] = lv_label_create(col_frame);
        lv_label_set_text(s_wind_lbl[i], "--");
        lv_obj_set_style_text_font(s_wind_lbl[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_wind_lbl[i], lv_color_hex(0x546E7A), 0);
        lv_obj_set_size(s_wind_lbl[i], 70, 18);
        lv_obj_set_style_text_align(s_wind_lbl[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_wind_lbl[i], x + 40, y);
    }

    /* Daten einsetzen */
    fill_columns(forecast);
    /* Kleine Daten-Icons von SD laden und auf alle Spalten anwenden */
    load_data_icons();
    apply_data_icon_sources();
    /* Warnbereich aufbauen und mit letzten bekannten Warnungen füllen */
    build_warn_section(col_frame);
    update_warn_section();
}
