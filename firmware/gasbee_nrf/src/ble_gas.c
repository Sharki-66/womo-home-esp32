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
 *   0x2B11  Cal_zero    uint8 Write (0x01 = Nullpunkt, Waage leer)
 *   0x2B12  Cal_ref_g   uint32 LE Write (aufgelegtes Gewicht in Gramm)
 *   0x2B13  Cal_values  int32 Offset + float32 Scale (LE), Read + Write
 *                       (Scale = 0 löscht die Kalibrierung)
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

#include <math.h>
#include <string.h>

LOG_MODULE_REGISTER(ble_gas, LOG_LEVEL_INF);

// ── Interne Zustandsvariablen ─────────────────────────────────────────────────
static ble_gas_tare_cb_t s_tare_cb;

// Konfigurationsmodus (USB/VBUS): Dauersenden, Kalibrier-Befehle freigegeben
static atomic_t s_config_mode;

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

// ── Kalibrierung (0x2B11..0x2B13) ────────────────────────────────────────────
// Nur im Konfigurationsmodus (USB) erlaubt, sonst ATT-Fehler "Write Not Permitted".
// Die Write-Callbacks prüfen nur das Format und reichen den Befehl an main.c
// weiter. Messen und Speichern passiert in der Mess-Schleife, nicht im BT-Kontext.
static const ble_gas_cal_ops_t *s_cal_ops;

static ssize_t chr_cal_zero_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (!atomic_get(&s_config_mode)) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);   // nur mit USB
	}
	if (len != 1 || ((const uint8_t *)buf)[0] != 0x01) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (!s_cal_ops || !s_cal_ops->zero) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	LOG_INF("Kalibrierung: Nullpunkt angefordert");
	s_cal_ops->zero();
	return len;
}

static ssize_t chr_cal_ref_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (!atomic_get(&s_config_mode)) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);   // nur mit USB
	}
	if (len != sizeof(uint32_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	uint32_t grams = sys_get_le32(buf);
	if (grams == 0 || grams > GASBEE_CAL_REF_MAX_G) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (!s_cal_ops || !s_cal_ops->reference) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	LOG_INF("Kalibrierung: Referenzgewicht %u g angefordert", grams);
	s_cal_ops->reference(grams);
	return len;
}

// 8 Byte LE: int32 Offset, float32 Scale
static ssize_t chr_cal_values_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				   void *buf, uint16_t len, uint16_t offset)
{
	int32_t off = 0;
	float   scale = 0.0f;
	uint8_t val[8];

	if (s_cal_ops && s_cal_ops->get) {
		s_cal_ops->get(&off, &scale);
	}
	uint32_t scale_bits;
	memcpy(&scale_bits, &scale, sizeof(scale_bits));
	sys_put_le32((uint32_t)off, &val[0]);
	sys_put_le32(scale_bits, &val[4]);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, val, sizeof(val));
}

static ssize_t chr_cal_values_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (!atomic_get(&s_config_mode)) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);   // nur mit USB
	}
	if (len != 8) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	int32_t  off = (int32_t)sys_get_le32(buf);
	uint32_t scale_bits = sys_get_le32((const uint8_t *)buf + 4);
	float    scale;
	memcpy(&scale, &scale_bits, sizeof(scale));

	// Scale = 0 → Kalibrierung löschen; sonst endlich und ≠ 0
	if (scale != 0.0f && !isfinite(scale)) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (!s_cal_ops || !s_cal_ops->set) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	LOG_INF("Kalibrierung: Werte gesetzt offset=%d scale=%.10f", off, (double)scale);
	s_cal_ops->set(off, scale);
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

	// [15..16] Kalibrierung Nullpunkt (Write 0x01, Waage leer)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_CAL_ZERO),
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE, NULL, chr_cal_zero_write, NULL),

	// [17..18] Kalibrierung Referenzgewicht (Write uint32 Gramm)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_CAL_REF_G),
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE, NULL, chr_cal_ref_write, NULL),

	// [19..20] Kalibrierwerte direkt (Read/Write int32 Offset + float32 Scale)
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(GASBEE_CHR_CAL_VALUES),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       chr_cal_values_read, chr_cal_values_write, NULL),
);

// Indizes der Value-Attribute in s_gas_svc.attrs[]
#define ATTR_IDX_WEIGHT  2
#define ATTR_IDX_PCT     5
#define ATTR_IDX_NET     8
#define ATTR_IDX_BAT     11

// ── Advertising (Format v1) ───────────────────────────────────────────────────
// Akkubetrieb:          Burst aus GASBEE_ADV_BURST_COUNT Paketen nach jeder Messung
// Konfigurationsmodus:  (USB/VBUS vorhanden) dauerhaft senden, verbindbar

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
static bool    s_msd_valid;   // erst nach der ersten Messung senden

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
static atomic_t s_adv_running;

// Burst beendet (alle Pakete gesendet) bzw. Advertising durch Verbindung beendet
static void adv_sent(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info)
{
	atomic_set(&s_adv_running, 0);
	LOG_INF("Burst fertig (%u Pakete)", info->num_sent);
	status_led_ble_off();
}

static void adv_connected(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info)
{
	atomic_set(&s_adv_running, 0);
}

static const struct bt_le_ext_adv_cb s_adv_cb = {
	.sent      = adv_sent,
	.connected = adv_connected,
};

// Daten setzen und starten: num_events = 0 → dauerhaft (Konfigurationsmodus)
static int adv_start(uint8_t num_events)
{
	bt_le_ext_adv_stop(s_adv);   // falls noch ein Burst läuft
	atomic_set(&s_adv_running, 0);

	int err = bt_le_ext_adv_set_data(s_adv, s_ad, ARRAY_SIZE(s_ad), NULL, 0);
	if (err) {
		LOG_ERR("adv_set_data err=%d", err);
		return err;
	}
	err = bt_le_ext_adv_start(s_adv, BT_LE_EXT_ADV_START_PARAM(0, num_events));
	if (err) {
		LOG_ERR("adv_start err=%d", err);
		status_led_ble_off();
		return err;
	}
	atomic_set(&s_adv_running, 1);
	status_led_ble_adv();
	return 0;
}

// Konfigurationsmodus: nach Trennen wieder dauerhaft senden (nicht im BT-Kontext)
static void adv_restart_handler(struct k_work *work)
{
	if (atomic_get(&s_config_mode) && s_msd_valid && !atomic_get(&s_connected)) {
		adv_start(0);
	}
}
static K_WORK_DEFINE(s_adv_restart_work, adv_restart_handler);

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
	LOG_INF("Getrennt: reason=0x%02x", reason);
	status_led_ble_off();
}

// Verbindungsobjekt freigegeben → im Konfigurationsmodus weiter senden
static void on_recycled(void)
{
	k_work_submit(&s_adv_restart_work);
}

BT_CONN_CB_DEFINE(s_conn_cb) = {
	.connected    = on_connected,
	.disconnected = on_disconnected,
	.recycled     = on_recycled,
};

static void disconnect_cb(struct bt_conn *conn, void *data)
{
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

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

void ble_gas_set_config_mode(bool on)
{
	if (atomic_set(&s_config_mode, on ? 1 : 0) == (on ? 1 : 0)) {
		return;   // unverändert
	}
	if (on) {
		LOG_INF("Konfigurationsmodus (USB): dauerhaft senden, Kalibrierung freigegeben");
		// gestartet wird mit der nächsten Messung (ble_gas_adv_publish)
	} else {
		LOG_INF("Akkubetrieb: Burst-Modus, Kalibrierung gesperrt");
		if (s_adv) {
			bt_le_ext_adv_stop(s_adv);
			atomic_set(&s_adv_running, 0);
		}
		bt_conn_foreach(BT_CONN_TYPE_LE, disconnect_cb, NULL);
		status_led_ble_off();
	}
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
	s_msd_valid      = true;

	if (atomic_get(&s_connected)) {
		// Verbindbares Advertising geht mit CONFIG_BT_MAX_CONN=1 nicht parallel
		LOG_INF("Verbunden – kein Advertising, Werte nur per GATT (seq=%u)", s_seq);
		return;
	}

	if (atomic_get(&s_config_mode)) {
		// Dauersenden: läuft es schon, nur die Daten tauschen
		if (atomic_get(&s_adv_running)) {
			int err = bt_le_ext_adv_set_data(s_adv, s_ad, ARRAY_SIZE(s_ad), NULL, 0);
			if (err) {
				LOG_ERR("adv_set_data err=%d", err);
			}
		} else if (adv_start(0) == 0) {
			LOG_INF("Dauersenden gestartet (Konfigurationsmodus)");
		}
		return;
	}

	if (adv_start(GASBEE_ADV_BURST_COUNT) == 0) {
		LOG_INF("Burst gestartet: seq=%u Gewicht=%d×10g Akku=%u mV Flags=0x%02x",
			s_seq, weight_10g, bat_mv, flags);
	}
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

void ble_gas_set_cal_ops(const ble_gas_cal_ops_t *ops)
{
	s_cal_ops = ops;
}
