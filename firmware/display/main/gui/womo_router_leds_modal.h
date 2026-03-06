/*
 * WoMo Router LED Status Modal
 *
 * Zeigt eine visuelle Darstellung der LEDs des RUTX11-Routers
 * (Power, WiFi, Mobile Signal, SIM-Status)
 */

#ifndef WOMO_ROUTER_LEDS_MODAL_H
#define WOMO_ROUTER_LEDS_MODAL_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Router-Status-Snapshot für LED-Anzeige
 */
typedef struct {
    bool    router_online;         /**< ESP32 hat Verbindung zum Router */
    bool    wifi_connected;        /**< Router-STA verbunden */
    char    wifi_ssid[33];         /**< Verbundenes WLAN */
    uint8_t wifi_signal_percent;   /**< WiFi-Signal 0-100% */
    uint8_t wifi_channel;          /**< WiFi-Kanal (für 2.4/5 GHz) */
    bool    router_ap_24ghz;       /**< Router-AP sendet auf 2.4 GHz */
    bool    router_ap_5ghz;        /**< Router-AP sendet auf 5 GHz */
    bool    lte_registered;        /**< LTE im Netz registriert */
    uint8_t lte_signal_percent;    /**< LTE-Signal 0-100% */
    char    lte_operator[32];      /**< LTE-Provider */
    char    lte_conn_type[16];     /**< "4G", "3G", "2G" etc. */
    char    sim_state[16];         /**< "inserted", "not inserted" */
} womo_router_leds_snapshot_t;

/**
 * @brief Router-LED-Status-Modal anzeigen
 * @param parent  Parent-Container (oder NULL für lv_scr_act())
 * @param snapshot  Router-Status-Daten
 */
void womo_router_leds_modal_show(lv_obj_t *parent, const womo_router_leds_snapshot_t *snapshot);

/**
 * @brief Modal-Daten aktualisieren (ohne neu zu erstellen)
 * @param snapshot  Neue Router-Status-Daten
 */
void womo_router_leds_modal_refresh(const womo_router_leds_snapshot_t *snapshot);

/**
 * @brief Prüfen ob das Modal gerade geöffnet ist
 * @return true wenn Modal sichtbar
 */
bool womo_router_leds_modal_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* WOMO_ROUTER_LEDS_MODAL_H */
