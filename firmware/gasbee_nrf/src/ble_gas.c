/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * ble_gas.c – BLE GATT Server für GasBee (Zephyr Bluetooth)
 *
 * Protokollgleich mit firmware/gasbee (ESP32-C3, NimBLE):
 * Service:        4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293
 * Characteristics:
 *   0x2B01  Weight_kg   float32, little-endian, Read + Notify
 *   0x2B02  Gas_pct     uint8,   Read + Notify
 *   0x2B03  Net_gas_kg  float32, little-endian, Read + Notify
 *   0x2B04  Bat_mV      uint16,  little-endian, Read + Notify (nur GasBee nRF)
 *   0x2B10  Tare        uint8 Write (0x01 = auslösen)
 */

#include "ble_gas.h"
#include "gasbee_config.h"
#include "status_led.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ble_gas, LOG_LEVEL_INF);

// ── Interne Zustandsvariablen ─────────────────────────────────────────────────
static ble_gas_tare_cb_t s_tare_cb;

static float   s_weight_kg;
static float   s_net_gas_kg;
static uint8_t s_gas_pct;
static uint16_t s_bat_mv;

static struct bt_uuid_128 s_svc_uuid = BT_UUID_INIT_128(GASBEE_BLE_SERVICE_UUID_VAL);

// ── GATT Access Callbacks ─────────────────────────────────────────────────────

static ssize_t chr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	// attr->user_data zeigt auf die jeweilige Zustandsvariable
	size_t size = (attr->user_data == &s_gas_pct) ? sizeof(s_gas_pct)
		    : (attr->user_data == &s_bat_mv)  ? sizeof(s_bat_mv)
		    : sizeof(float);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, size);
}

static ssize_t chr_tare_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len >= 1 && ((const uint8_t *)buf)[0] == 0x01 && s_tare_cb) {
		LOG_INF("Tare-Befehl empfangen");
		s_tare_cb();
	}
	return len;
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	LOG_INF("Subscribe: notify=%d", value == BT_GATT_CCC_NOTIFY);
}

// ── GATT Service-Definition ───────────────────────────────────────────────────

BT_GATT_SERVICE_DEFINE(s_gas_svc,
	BT_GATT_PRIMARY_SERVICE(&s_svc_uuid),

	// [1..3] Gewicht kg (float, Read + Notify)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_WEIGHT_KG),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, chr_read, NULL, &s_weight_kg),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	// [4..6] Füllstand % (uint8, Read + Notify)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_GAS_PCT),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, chr_read, NULL, &s_gas_pct),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	// [7..9] Netto-Gas kg (float, Read + Notify)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_GAS_NET_KG),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, chr_read, NULL, &s_net_gas_kg),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	// [10..12] Akkuspannung mV (uint16, Read + Notify)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_BAT_MV),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, chr_read, NULL, &s_bat_mv),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	// [13..14] Tare-Befehl (Write)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_TARE),
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE, NULL, chr_tare_write, NULL),
);

// Indizes der Value-Attribute in s_gas_svc.attrs[]
#define ATTR_IDX_WEIGHT  2
#define ATTR_IDX_PCT     5
#define ATTR_IDX_NET     8
#define ATTR_IDX_BAT     11

// ── Advertising (Format v1, Burst nach jeder Messung) ─────────────────────────

// Manufacturer Specific Data: Company-ID (LE) + 7 Byte Nutzdaten
enum {
	MSD_COMPANY  = 0,   // 2 Byte
	MSD_VERSION  = 2,
	MSD_WEIGHT   = 3,   // int16 LE, 10 g
	MSD_BAT_MV   = 5,   // uint16 LE
	MSD_SEQ      = 7,
	MSD_FLAGS    = 8,
	MSD_LEN      = 9,
};

static uint8_t s_msd[MSD_LEN];
static uint8_t s_seq;

static const struct bt_data s_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	// Name im Advertising (nicht Scan-Response): Sensorboard filtert darauf
	BT_DATA(BT_DATA_NAME_COMPLETE, GASBEE_BLE_DEVICE_NAME,
		sizeof(GASBEE_BLE_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, s_msd, sizeof(s_msd)),
};

#define ADV_INTERVAL ((GASBEE_BLE_ADV_INTERVAL_MS * 1000) / 625)  // Einheit 0,625 ms

static struct bt_le_ext_adv *s_adv;
static atomic_t s_connected;

// Burst beendet (alle Pakete gesendet)
static void adv_sent(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info)
{
	LOG_INF("Burst fertig (%u Pakete)", info->num_sent);
	status_led_ble_off();
}

static const struct bt_le_ext_adv_cb s_adv_cb = {
	.sent = adv_sent,
};

// ── Verbindungs-Callbacks ─────────────────────────────────────────────────────

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_INF("Verbindung fehlgeschlagen (err=%u)", err);
		return;
	}
	atomic_set(&s_connected, 1);
	LOG_INF("Verbunden");
	status_led_ble_connected();
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	atomic_set(&s_connected, 0);
	LOG_INF("Getrennt: reason=0x%02x (nächster Burst nach der nächsten Messung)", reason);
	status_led_ble_off();
}

BT_CONN_CB_DEFINE(s_conn_cb) = {
	.connected    = on_connected,
	.disconnected = on_disconnected,
};

// ── Öffentliche API ───────────────────────────────────────────────────────────

int ble_gas_init(void)
{
	int err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable err=%d", err);
		return err;
	}

	// Legacy-PDU (ohne BT_LE_ADV_OPT_EXT_ADV), verbindbar für GATT
	err = bt_le_ext_adv_create(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, ADV_INTERVAL,
						   ADV_INTERVAL, NULL),
				   &s_adv_cb, &s_adv);
	if (err) {
		LOG_ERR("ext_adv_create err=%d", err);
		return err;
	}

	LOG_INF("BLE GATT Server initialisiert");
	return 0;
}

void ble_gas_adv_publish(int16_t weight_10g, uint16_t bat_mv, uint8_t flags)
{
	if (!s_adv) {
		return;
	}

	s_seq++;
	sys_put_le16(GASBEE_ADV_COMPANY_ID, &s_msd[MSD_COMPANY]);
	s_msd[MSD_VERSION] = GASBEE_ADV_FORMAT_VERSION;
	sys_put_le16((uint16_t)weight_10g, &s_msd[MSD_WEIGHT]);
	sys_put_le16(bat_mv, &s_msd[MSD_BAT_MV]);
	s_msd[MSD_SEQ]   = s_seq;
	s_msd[MSD_FLAGS] = flags;

	if (atomic_get(&s_connected)) {
		// Verbindbares Advertising geht mit CONFIG_BT_MAX_CONN=1 nicht parallel
		LOG_INF("Verbunden – kein Burst, Werte nur per GATT (seq=%u)", s_seq);
		return;
	}

	bt_le_ext_adv_stop(s_adv);   // falls ein Burst noch läuft

	int err = bt_le_ext_adv_set_data(s_adv, s_ad, ARRAY_SIZE(s_ad), NULL, 0);
	if (err) {
		LOG_ERR("adv_set_data err=%d", err);
		return;
	}
	err = bt_le_ext_adv_start(s_adv, BT_LE_EXT_ADV_START_PARAM(0, GASBEE_ADV_BURST_COUNT));
	if (err) {
		LOG_ERR("adv_start err=%d", err);
		status_led_ble_off();
		return;
	}
	LOG_INF("Burst gestartet: seq=%u Gewicht=%d×10g Akku=%u mV Flags=0x%02x",
		s_seq, weight_10g, bat_mv, flags);
	status_led_ble_adv();
}

void ble_gas_notify(float weight_kg, float net_gas_kg, uint8_t gas_pct)
{
	s_weight_kg  = weight_kg;
	s_net_gas_kg = net_gas_kg;
	s_gas_pct    = gas_pct;

	// Ohne verbundenen/abonnierten Client liefert bt_gatt_notify einen Fehler → ignorieren
	bt_gatt_notify(NULL, &s_gas_svc.attrs[ATTR_IDX_WEIGHT], &s_weight_kg, sizeof(s_weight_kg));
	bt_gatt_notify(NULL, &s_gas_svc.attrs[ATTR_IDX_NET], &s_net_gas_kg, sizeof(s_net_gas_kg));
	bt_gatt_notify(NULL, &s_gas_svc.attrs[ATTR_IDX_PCT], &s_gas_pct, sizeof(s_gas_pct));
}

void ble_gas_notify_battery(uint16_t bat_mv)
{
	s_bat_mv = bat_mv;
	bt_gatt_notify(NULL, &s_gas_svc.attrs[ATTR_IDX_BAT], &s_bat_mv, sizeof(s_bat_mv));
}

void ble_gas_set_tare_callback(ble_gas_tare_cb_t cb)
{
	s_tare_cb = cb;
}
