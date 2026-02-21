/**
 * ble_gas.c – BLE GATT Server für GasBee (NimBLE)
 *
 * Service:        Custom (128-bit UUID)
 * Characteristics:
 *   0x2B01  Weight_kg   float32, little-endian, Notify
 *   0x2B02  Gas_pct     uint8,   Notify
 *   0x2B03  Net_gas_kg  float32, little-endian, Notify
 *   0x2B10  Tare        uint8 Write (0x01 = auslösen)
 */

#include "ble_gas.h"
#include "gasbee_config.h"

#include <string.h>
#include <esp_log.h>
#include <esp_nimble_hci.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

static const char *TAG = "ble_gas";

// ── Interne Zustandsvariablen ─────────────────────────────────────────────────
static ble_gas_tare_cb_t s_tare_cb = NULL;

static uint16_t s_conn_handle       = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_chr_weight_handle = 0;
static uint16_t s_chr_pct_handle    = 0;
static uint16_t s_chr_net_handle    = 0;

static float   s_weight_kg  = 0.0f;
static float   s_net_gas_kg = 0.0f;
static uint8_t s_gas_pct    = 0;

// ── GATT Access Callbacks ─────────────────────────────────────────────────────

static int chr_weight_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_weight_kg, sizeof(s_weight_kg));
    }
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int chr_pct_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_gas_pct, sizeof(s_gas_pct));
    }
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int chr_net_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_net_gas_kg, sizeof(s_net_gas_kg));
    }
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int chr_tare_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t val = 0;
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= 1) {
            os_mbuf_copydata(ctxt->om, 0, 1, &val);
        }
        if (val == 0x01 && s_tare_cb) {
            ESP_LOGI(TAG, "Tare-Befehl empfangen");
            s_tare_cb();
        }
        return 0;
    }
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

// ── GATT Service-Definition ───────────────────────────────────────────────────

// 128-bit Service UUID (little-endian byte array)
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x93, 0x82, 0x71, 0x60, 0x5f, 0x4e, 0x3d, 0x2c,
    0x1b, 0x0a, 0x9f, 0x8e, 0x7d, 0x6c, 0x5b, 0x4a
);

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   // Rohgewicht (float, Notify + Read)
                .uuid       = BLE_UUID16_DECLARE(GASBEE_CHR_WEIGHT_KG),
                .access_cb  = chr_weight_access,
                .val_handle = &s_chr_weight_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {   // Füllstand % (uint8, Notify + Read)
                .uuid       = BLE_UUID16_DECLARE(GASBEE_CHR_GAS_PCT),
                .access_cb  = chr_pct_access,
                .val_handle = &s_chr_pct_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {   // Netto-Gas kg (float, Notify + Read)
                .uuid       = BLE_UUID16_DECLARE(GASBEE_CHR_GAS_NET_KG),
                .access_cb  = chr_net_access,
                .val_handle = &s_chr_net_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {   // Tare-Befehl (Write)
                .uuid       = BLE_UUID16_DECLARE(GASBEE_CHR_TARE),
                .access_cb  = chr_tare_access,
                .flags      = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }  // Terminator
        },
    },
    { 0 }  // Terminator
};

// ── GAP Event Handler ─────────────────────────────────────────────────────────

static void ble_gas_advertise(void);

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Verbunden: handle=%d", s_conn_handle);
            } else {
                ESP_LOGI(TAG, "Verbindung fehlgeschlagen, starte Advertising neu");
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ble_gas_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Getrennt: reason=%d", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_gas_advertise();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe: attr=%d cur_notify=%d",
                     event->subscribe.attr_handle, event->subscribe.cur_notify);
            break;

        default:
            break;
    }
    return 0;
}

// ── Advertising ───────────────────────────────────────────────────────────────

static void ble_gas_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};

    fields.flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name                  = (const uint8_t *)GASBEE_BLE_DEVICE_NAME;
    fields.name_len              = strlen(GASBEE_BLE_DEVICE_NAME);
    fields.name_is_complete      = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {
        .conn_mode  = BLE_GAP_CONN_MODE_UND,
        .disc_mode  = BLE_GAP_DISC_MODE_GEN,
        .itvl_min   = (GASBEE_BLE_ADV_INTERVAL_MS * 1000) / 625,
        .itvl_max   = (GASBEE_BLE_ADV_INTERVAL_MS * 1000) / 625,
    };

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising gestartet als \"%s\"", GASBEE_BLE_DEVICE_NAME);
    }
}

// ── NimBLE Host Callbacks ─────────────────────────────────────────────────────

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    ble_gas_advertise();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE Reset: reason=%d", reason);
}

static void nimble_host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ── Öffentliche API ───────────────────────────────────────────────────────────

esp_err_t ble_gas_init(void)
{
    esp_err_t err = esp_nimble_hci_and_controller_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HCI init fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    nimble_port_init();

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "count_cfg rc=%d", rc); return ESP_FAIL; }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "add_svcs rc=%d", rc); return ESP_FAIL; }

    rc = ble_svc_gap_device_name_set(GASBEE_BLE_DEVICE_NAME);
    if (rc != 0) { ESP_LOGE(TAG, "device_name_set rc=%d", rc); return ESP_FAIL; }

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE GATT Server initialisiert");
    return ESP_OK;
}

void ble_gas_notify(float weight_kg, float net_gas_kg, uint8_t gas_pct)
{
    s_weight_kg  = weight_kg;
    s_net_gas_kg = net_gas_kg;
    s_gas_pct    = gas_pct;

    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;  // Kein Client verbunden
    }

    struct os_mbuf *om;

    om = ble_hs_mbuf_from_flat(&s_weight_kg, sizeof(s_weight_kg));
    if (om) ble_gatts_notify_custom(s_conn_handle, s_chr_weight_handle, om);

    om = ble_hs_mbuf_from_flat(&s_net_gas_kg, sizeof(s_net_gas_kg));
    if (om) ble_gatts_notify_custom(s_conn_handle, s_chr_net_handle, om);

    om = ble_hs_mbuf_from_flat(&s_gas_pct, sizeof(s_gas_pct));
    if (om) ble_gatts_notify_custom(s_conn_handle, s_chr_pct_handle, om);
}

void ble_gas_set_tare_callback(ble_gas_tare_cb_t cb)
{
    s_tare_cb = cb;
}
