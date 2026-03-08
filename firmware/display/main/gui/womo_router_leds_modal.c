/*
 * WoMo Router LED Status Modal
 *
 * Zeigt eine visuelle Darstellung der LEDs des RUTX11-Routers
 */

#include "womo_router_leds_modal.h"
#include "womo_locale.h"
#include "womo_theme.h"
#include "womo_fonts_german.h"
#include "hardware/lvgl_port.h"
#include "esp_log.h"
#include <string.h>

#define TAG "ROUTER_LEDS"

#define LED_SIZE_LARGE  24   /* Power, WiFi, SIM */
#define LED_SIZE_SMALL  16   /* Mobile Signal Balken */
#define SIGNAL_BAR_WIDTH 12
#define SIGNAL_BAR_GAP   4

/* Kontext-Struktur */
typedef struct {
    lv_obj_t *router_box;     /* Schwarze Router-Box */
    
    /* LED-Objekte */
    lv_obj_t *power_led;
    lv_obj_t *wifi_led;
    lv_obj_t *signal_bars[5];  /* 5 LEDs für Signalstärke */
    lv_obj_t *sim1_led;
    lv_obj_t *sim2_led;
    
    /* Zusatz-Labels für Frequenz/Generation */
    lv_obj_t *wifi_freq_label;   /* 2.4/5 GHz über WiFi */
    lv_obj_t *lte_type_label;    /* "LTE", "LTE 4G" etc. */
    
    /* Daten-Snapshot */
    womo_router_leds_snapshot_t snapshot;
} router_leds_ctx_t;

static router_leds_ctx_t s_ctx = {0};

/* Forward-Deklarationen */
static void build_modal(lv_obj_t *parent);
static void destroy_modal(void);
static void update_led_states(void);
static lv_obj_t *create_led(lv_obj_t *parent, uint32_t color, int size);
static void set_led_color(lv_obj_t *led, uint32_t color);

/* ════════════════════ LED-Helfer ═════════════════════ */

static lv_obj_t *create_led(lv_obj_t *parent, uint32_t color, int size)
{
    lv_obj_t *led = lv_obj_create(parent);
    lv_obj_remove_style_all(led);
    lv_obj_set_size(led, size, size);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(led, lv_color_hex(color), 0);
    lv_obj_clear_flag(led, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return led;
}

static void set_led_color(lv_obj_t *led, uint32_t color)
{
    if (!led) return;
    lv_obj_set_style_bg_color(led, lv_color_hex(color), 0);
}

/* ════════════════════ Modal-Verwaltung ═══════════════ */

static void build_modal(lv_obj_t *parent)
{
    if (!parent) {
        parent = lv_scr_act();
    }

    /* ──────────────── Router-Box (schwarzer Kasten, zentriert) ────────────────── */
    s_ctx.router_box = lv_obj_create(parent);
    lv_obj_set_size(s_ctx.router_box, 440, 130);
    lv_obj_center(s_ctx.router_box);
    lv_obj_set_style_bg_color(s_ctx.router_box, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(s_ctx.router_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ctx.router_box, 8, 0);
    lv_obj_set_style_border_width(s_ctx.router_box, 3, 0);
    lv_obj_set_style_border_color(s_ctx.router_box, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_pad_all(s_ctx.router_box, 20, 0);
    lv_obj_set_style_shadow_width(s_ctx.router_box, 20, 0);
    lv_obj_set_style_shadow_opa(s_ctx.router_box, LV_OPA_50, 0);
    lv_obj_clear_flag(s_ctx.router_box, LV_OBJ_FLAG_SCROLLABLE);

    /* RUTX11 Label */
    lv_obj_t *router_label = lv_label_create(s_ctx.router_box);
    lv_label_set_text(router_label, "RUTX11");
    lv_obj_set_style_text_font(router_label, WOMO_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(router_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(router_label, LV_ALIGN_TOP_MID, 0, 0);

    /* LED-Container */
    lv_obj_t *led_row = lv_obj_create(s_ctx.router_box);
    lv_obj_remove_style_all(led_row);
    lv_obj_set_size(led_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(led_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(led_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(led_row, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_pad_column(led_row, 18, 0);

    /* Power LED */
    lv_obj_t *power_col = lv_obj_create(led_row);
    lv_obj_remove_style_all(power_col);
    lv_obj_set_size(power_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(power_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(power_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(power_col, 4, 0);
    
    s_ctx.power_led = create_led(power_col, 0x444444, LED_SIZE_LARGE);
    lv_obj_t *power_lbl = lv_label_create(power_col);
    lv_label_set_text(power_lbl, "PWR");
    lv_obj_set_style_text_font(power_lbl, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(power_lbl, lv_color_hex(0xAAAAAA), 0);

    /* WiFi LED */
    lv_obj_t *wifi_col = lv_obj_create(led_row);
    lv_obj_remove_style_all(wifi_col);
    lv_obj_set_size(wifi_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wifi_col, 4, 0);
    
    s_ctx.wifi_led = create_led(wifi_col, 0x444444, LED_SIZE_LARGE);
    
    /* WiFi-Label (inkl. Frequenz wie "WiFi 2.4" oder "WiFi  5") - feste Breite */
    s_ctx.wifi_freq_label = lv_label_create(wifi_col);
    lv_label_set_text(s_ctx.wifi_freq_label, "WiFi");
    lv_obj_set_style_text_font(s_ctx.wifi_freq_label, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_ctx.wifi_freq_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(s_ctx.wifi_freq_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ctx.wifi_freq_label, 50);  /* Feste Breite für konstantes Layout */

    /* Mobile Signal Bars (5 Balken) */
    lv_obj_t *signal_col = lv_obj_create(led_row);
    lv_obj_remove_style_all(signal_col);
    lv_obj_set_size(signal_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(signal_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(signal_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(signal_col, 4, 0);
    
    lv_obj_t *bars_row = lv_obj_create(signal_col);
    lv_obj_remove_style_all(bars_row);
    lv_obj_set_size(bars_row, LV_SIZE_CONTENT, 24);
    lv_obj_set_flex_flow(bars_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bars_row, SIGNAL_BAR_GAP, 0);
    
    /* 5 Signalbalken mit steigender Höhe */
    for (int i = 0; i < 5; i++) {
        int height = 6 + (i * 4);  /* 6, 10, 14, 18, 22 */
        s_ctx.signal_bars[i] = lv_obj_create(bars_row);
        lv_obj_remove_style_all(s_ctx.signal_bars[i]);
        lv_obj_set_size(s_ctx.signal_bars[i], SIGNAL_BAR_WIDTH, height);
        lv_obj_set_style_bg_opa(s_ctx.signal_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_ctx.signal_bars[i], lv_color_hex(0x444444), 0);
        lv_obj_set_style_radius(s_ctx.signal_bars[i], 2, 0);
        lv_obj_clear_flag(s_ctx.signal_bars[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    
    /* LTE-Label (zeigt Generation: 2G, 3G, 4G) */
    s_ctx.lte_type_label = lv_label_create(signal_col);
    lv_label_set_text(s_ctx.lte_type_label, "Mobil");
    lv_obj_set_style_text_font(s_ctx.lte_type_label, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_ctx.lte_type_label, lv_color_hex(0xAAAAAA), 0);

    /* SIM1 LED */
    lv_obj_t *sim1_col = lv_obj_create(led_row);
    lv_obj_remove_style_all(sim1_col);
    lv_obj_set_size(sim1_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sim1_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sim1_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(sim1_col, 4, 0);
    
    s_ctx.sim1_led = create_led(sim1_col, 0x444444, LED_SIZE_LARGE);
    lv_obj_t *sim1_lbl = lv_label_create(sim1_col);
    lv_label_set_text(sim1_lbl, "SIM1");
    lv_obj_set_style_text_font(sim1_lbl, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(sim1_lbl, lv_color_hex(0xAAAAAA), 0);

    /* SIM2 LED */
    lv_obj_t *sim2_col = lv_obj_create(led_row);
    lv_obj_remove_style_all(sim2_col);
    lv_obj_set_size(sim2_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sim2_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sim2_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(sim2_col, 4, 0);
    
    s_ctx.sim2_led = create_led(sim2_col, 0x444444, LED_SIZE_LARGE);
    lv_obj_t *sim2_lbl = lv_label_create(sim2_col);
    lv_label_set_text(sim2_lbl, "SIM2");
    lv_obj_set_style_text_font(sim2_lbl, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(sim2_lbl, lv_color_hex(0xAAAAAA), 0);

    /* LEDs aktualisieren */
    update_led_states();
}

static void destroy_modal(void)
{
    if (s_ctx.router_box) {
        lv_obj_del(s_ctx.router_box);
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
}

/* ════════════════════ LED-Status aktualisieren ═══════ */

static void update_led_states(void)
{
    ESP_LOGI(TAG, "=== Router LED Update ===");
    ESP_LOGI(TAG, "Router online: %d", s_ctx.snapshot.router_online);
    ESP_LOGI(TAG, "Router AP: 2.4GHz=%d, 5GHz=%d", 
             s_ctx.snapshot.router_ap_24ghz, s_ctx.snapshot.router_ap_5ghz);
    ESP_LOGI(TAG, "WiFi STA: connected=%d, channel=%u, ssid='%s'", 
             s_ctx.snapshot.wifi_connected, 
             s_ctx.snapshot.wifi_channel,
             s_ctx.snapshot.wifi_ssid);
    ESP_LOGI(TAG, "LTE: registered=%d, conn_type='%s', operator='%s'", 
             s_ctx.snapshot.lte_registered,
             s_ctx.snapshot.lte_conn_type,
             s_ctx.snapshot.lte_operator);

    /* Power LED: Grün wenn Router online */
    if (s_ctx.snapshot.router_online) {
        set_led_color(s_ctx.power_led, 0x00FF00);
    } else {
        set_led_color(s_ctx.power_led, 0x444444);
    }

    /* WiFi LED: Grün wenn Router-AP aktiv (sendet), nicht wenn Display verbunden ist! */
    if (s_ctx.snapshot.router_online && (s_ctx.snapshot.router_ap_24ghz || s_ctx.snapshot.router_ap_5ghz)) {
        set_led_color(s_ctx.wifi_led, 0x00FF00);  /* Grün = AP sendet */
    } else if (s_ctx.snapshot.router_online) {
        set_led_color(s_ctx.wifi_led, 0xFFAA00);  /* Orange = Router online aber AP aus */
    } else {
        set_led_color(s_ctx.wifi_led, 0x444444);  /* Grau = Router offline */
    }

    /* WiFi Frequenz: Router-AP-Bänder anzeigen (nicht Display-Verbindung!) */
    if (s_ctx.wifi_freq_label) {
        if (s_ctx.snapshot.router_ap_24ghz && s_ctx.snapshot.router_ap_5ghz) {
            lv_label_set_text(s_ctx.wifi_freq_label, "WiFi 2.4+5");
            ESP_LOGI(TAG, "WiFi label: 'WiFi 2.4+5' (dual-band active)");
        } else if (s_ctx.snapshot.router_ap_24ghz) {
            lv_label_set_text(s_ctx.wifi_freq_label, "WiFi 2.4");
            ESP_LOGI(TAG, "WiFi label: 'WiFi 2.4' (2.4 GHz band active)");
        } else if (s_ctx.snapshot.router_ap_5ghz) {
            lv_label_set_text(s_ctx.wifi_freq_label, "WiFi 5");
            ESP_LOGI(TAG, "WiFi label: 'WiFi 5' (5 GHz band active)");
        } else {
            lv_label_set_text(s_ctx.wifi_freq_label, "WiFi");
            ESP_LOGI(TAG, "WiFi label: 'WiFi' (no band info)");
        }
    }

    /* Mobile Signal Bars: 0-5 LEDs basierend auf Signal% */
    int active_bars = 0;
    if (s_ctx.snapshot.lte_registered) {
        /* 0-19%: 1 LED, 20-39%: 2 LEDs, usw. */
        active_bars = (s_ctx.snapshot.lte_signal_percent / 20) + 1;
        if (active_bars > 5) active_bars = 5;
    }

    for (int i = 0; i < 5; i++) {
        if (i < active_bars) {
            /* Grün für aktive Balken */
            set_led_color(s_ctx.signal_bars[i], 0x00FF00);
        } else {
            /* Dunkelgrau für inaktive */
            set_led_color(s_ctx.signal_bars[i], 0x444444);
        }
    }

    /* LTE Info - nicht mehr benötigt, wird über lte_type_label abgedeckt */

    /* LTE Generation: Zeige conn_type (z.B. "4G", "3G") als Haupt-Info */
    if (s_ctx.lte_type_label) {
        if (s_ctx.snapshot.lte_registered && s_ctx.snapshot.lte_conn_type[0]) {
            /* Wenn conn_type "4G (LTE)" ist, nehme nur "4G" */
            char clean_type[16] = {0};
            const char *paren = strchr(s_ctx.snapshot.lte_conn_type, '(');
            if (paren) {
                /* Bis zur Klammer kopieren, Leerzeichen am Ende entfernen */
                size_t len = paren - s_ctx.snapshot.lte_conn_type;
                while (len > 0 && s_ctx.snapshot.lte_conn_type[len-1] == ' ') len--;
                strncpy(clean_type, s_ctx.snapshot.lte_conn_type, len < sizeof(clean_type) ? len : sizeof(clean_type)-1);
            } else {
                strncpy(clean_type, s_ctx.snapshot.lte_conn_type, sizeof(clean_type)-1);
            }
            lv_label_set_text(s_ctx.lte_type_label, clean_type[0] ? clean_type : "LTE");
            ESP_LOGI(TAG, "LTE label: '%s' (from conn_type '%s')", clean_type, s_ctx.snapshot.lte_conn_type);
        } else if (s_ctx.snapshot.lte_registered) {
            /* LTE ist registriert, aber conn_type ist leer → "LTE" anzeigen */
            lv_label_set_text(s_ctx.lte_type_label, "LTE");
            ESP_LOGI(TAG, "LTE label: 'LTE' (registered but no conn_type)");
        } else {
            /* LTE nicht registriert */
            lv_label_set_text(s_ctx.lte_type_label, "Mobil");
            ESP_LOGI(TAG, "LTE label: 'Mobil' (not registered)");
        }
    }

    /* SIM-LEDs: SIM1 grün (default), SIM2 nur wenn explizit erwähnt */
    /* sim_state gibt aktuell nur "inserted" / "not inserted", 
       keine Info welche SIM aktiv ist. Default: SIM1 aktiv wenn LTE registriert */
    if (s_ctx.snapshot.lte_registered) {
        set_led_color(s_ctx.sim1_led, 0x00FF00);  /* SIM1 grün */
        set_led_color(s_ctx.sim2_led, 0x444444);  /* SIM2 aus */
    } else {
        set_led_color(s_ctx.sim1_led, 0x444444);
        set_led_color(s_ctx.sim2_led, 0x444444);
    }
}

/* ════════════════════ Public API ═════════════════════ */

void womo_router_leds_modal_show(lv_obj_t *parent, const womo_router_leds_snapshot_t *snapshot)
{
    if (snapshot) {
        s_ctx.snapshot = *snapshot;
    } else {
        memset(&s_ctx.snapshot, 0, sizeof(s_ctx.snapshot));
    }

    /* Toggle-Verhalten: Wenn bereits geöffnet, schließen */
    if (s_ctx.router_box) {
        if (lvgl_port_lock(-1)) {
            destroy_modal();
            lvgl_port_unlock();
        }
        return;
    }

    if (!parent) {
        parent = lv_scr_act();
    }

    if (lvgl_port_lock(-1)) {
        build_modal(parent);
        lvgl_port_unlock();
    }
}

void womo_router_leds_modal_refresh(const womo_router_leds_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    s_ctx.snapshot = *snapshot;

    if (!s_ctx.router_box) {
        return;
    }

    /* LVGL-Lock für Thread-sichere UI-Updates */
    if (lvgl_port_lock(-1)) {
        update_led_states();
        lvgl_port_unlock();
    }
}

bool womo_router_leds_modal_is_open(void)
{
    return (s_ctx.router_box != NULL);
}

void womo_router_leds_modal_close(void)
{
    if (s_ctx.router_box) {
        destroy_modal();
    }
}
