/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensors/bno055_sensor.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bno055.h"
#include "sensor_config.h"
#include "hal/sensor_i2c_bus.h"

static const char *TAG = "bno055_app";

// ── IMU-Snapshot (interner Shared-State für RS485-Modem) ─────────────────
static bno055_imu_snapshot_t s_imu_last = {0};

void bno055_imu_update(const bno055_imu_snapshot_t *snap)
{
    if (!snap) return;
    s_imu_last = *snap;
    if (s_imu_last.timestamp_us == 0) {
        s_imu_last.timestamp_us = esp_timer_get_time();
    }
}

bool bno055_imu_get_snapshot(bno055_imu_snapshot_t *out)
{
    if (!out) return false;
    *out = s_imu_last;
    return true;
}

#define BNO055_NVS_NAMESPACE "bno055"
#define BNO055_NVS_KEY "calib"
#define BNO055_CAL_VERSION 1

typedef struct {
    uint32_t version;
    sensor_offset_t offsets;
} bno055_calibration_blob_t;

static bno055_t s_bno = {0};
static TaskHandle_t s_task = NULL;
static portMUX_TYPE s_fast_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_fast_until_us = 0;
static uint32_t s_fast_interval_ms = 200;
static bool s_offset_cache_valid = false;
static sensor_offset_t s_cached_offsets = {};
static int64_t s_last_persist_check_us = 0;

// ── Pitch/Roll Einbau-Offset (Nullpunkt-Kalibrierung) ───────────────────
#define BNO055_NVS_ZERO_KEY "pr_zero"
#define BNO055_ZERO_VERSION 1

typedef struct {
    uint32_t version;
    float pitch_offset;
    float roll_offset;
} bno055_zero_blob_t;

static float s_pitch_offset = 0.0f;
static float s_roll_offset  = 0.0f;

static void bno055_load_zero_offsets(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(BNO055_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return;

    bno055_zero_blob_t blob = {0};
    size_t sz = sizeof(blob);
    err = nvs_get_blob(h, BNO055_NVS_ZERO_KEY, &blob, &sz);
    nvs_close(h);

    if (err == ESP_OK && sz == sizeof(blob) && blob.version == BNO055_ZERO_VERSION) {
        s_pitch_offset = blob.pitch_offset;
        s_roll_offset  = blob.roll_offset;
        ESP_LOGI(TAG, "Pitch/Roll Offset aus NVS: pitch=%.2f° roll=%.2f°",
                 s_pitch_offset, s_roll_offset);
    }
}

static esp_err_t bno055_save_zero_offsets(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(BNO055_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    bno055_zero_blob_t blob = {
        .version = BNO055_ZERO_VERSION,
        .pitch_offset = s_pitch_offset,
        .roll_offset  = s_roll_offset,
    };
    err = nvs_set_blob(h, BNO055_NVS_ZERO_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t bno055_app_zero_pitch_roll(void)
{
    bno055_imu_snapshot_t snap;
    if (!bno055_imu_get_snapshot(&snap) || !snap.valid) {
        ESP_LOGW(TAG, "imu_zero: kein gültiger IMU-Snapshot");
        return ESP_ERR_INVALID_STATE;
    }

    // Aktuellen RAW-Wert (VOR bestehender Offset-Korrektur) als neuen Offset nehmen
    s_pitch_offset = snap.pitch_deg + s_pitch_offset;
    s_roll_offset  = snap.roll_deg  + s_roll_offset;

    esp_err_t err = bno055_save_zero_offsets();
    ESP_LOGI(TAG, "Pitch/Roll Nullpunkt gesetzt: pitch=%.2f° roll=%.2f° (NVS %s)",
             s_pitch_offset, s_roll_offset,
             (err == ESP_OK) ? "OK" : esp_err_to_name(err));
    return err;
}

static bool bno055_offsets_equal(const sensor_offset_t *lhs, const sensor_offset_t *rhs)
{
    return memcmp(lhs, rhs, sizeof(sensor_offset_t)) == 0;
}

static esp_err_t bno055_apply_axis_map(bno055_t *imu)
{
    if (!imu) {
        return ESP_ERR_INVALID_ARG;
    }
    // Achsen-Mapping aus sensor_config.h (anpassbar an Einbaulage)
    bno055_axes_t axes = {
        .x = SENSOR_BNO055_AXIS_X,
        .y = SENSOR_BNO055_AXIS_Y,
        .z = SENSOR_BNO055_AXIS_Z,
    };
    return bno055_remap_axis(imu, &axes);
}

static bool bno055_restore_calibration(bno055_t *imu)
{
    if (!imu) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BNO055_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "BNO055 NVS open failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    bno055_calibration_blob_t blob = {};
    size_t blob_size = sizeof(blob);
    err = nvs_get_blob(handle, BNO055_NVS_KEY, &blob, &blob_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "BNO055 calibration read failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    if (blob_size != sizeof(blob) || blob.version != BNO055_CAL_VERSION) {
        ESP_LOGW(TAG, "BNO055 calibration blob invalid (size=%u, version=%lu)",
                 (unsigned)blob_size, (unsigned long)blob.version);
        return false;
    }

    imu->config.offsets = blob.offsets;
    if (bno055_set_offsets(imu) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply stored BNO055 offsets");
        return false;
    }

    (void)bno055_get_calibration_status(imu);
    s_cached_offsets = blob.offsets;
    s_offset_cache_valid = true;
    ESP_LOGI(TAG, "BNO055 calibration restored from NVS");
    return true;
}

static bool bno055_persist_calibration_if_needed(bno055_t *imu)
{
    if (!imu || !imu->config.is_calibrated) {
        return false;
    }

    if (bno055_get_offsets(imu) != ESP_OK) {
        return false;
    }

    if (s_offset_cache_valid && bno055_offsets_equal(&s_cached_offsets, &imu->config.offsets)) {
        return false;
    }

    bno055_calibration_blob_t blob = {
        .version = BNO055_CAL_VERSION,
        .offsets = imu->config.offsets,
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BNO055_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BNO055 NVS open for write failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, BNO055_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        s_cached_offsets = imu->config.offsets;
        s_offset_cache_valid = true;
        ESP_LOGI(TAG, "BNO055 calibration saved to NVS");
        return true;
    } else {
        ESP_LOGW(TAG, "BNO055 calibration save failed: %s", esp_err_to_name(err));
        return false;
    }
}

void bno055_app_request_fast(uint32_t duration_ms, uint32_t interval_ms)
{
    if (duration_ms == 0 || interval_ms == 0) {
        return;
    }
    int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_fast_mux);
    s_fast_until_us = now_us + ((int64_t)duration_ms * 1000LL);
    s_fast_interval_ms = interval_ms;
    portEXIT_CRITICAL(&s_fast_mux);
}

static void bno055_task(void *arg)
{
    while (1) {
        i2c_bus_lock();
        esp_err_t err = bno055_get_readings(&s_bno, EULER_ANGLE);
        esp_err_t err_acc = bno055_get_readings(&s_bno, ACCELEROMETER);
        esp_err_t err_gyr = bno055_get_readings(&s_bno, GYROSCOPE);
        esp_err_t err_mag = bno055_get_readings(&s_bno, MAGNETOMETER);
        esp_err_t err_tmp = bno055_get_readings(&s_bno, TEMPERATURE);
        esp_err_t err_cal = bno055_get_calibration_status(&s_bno);
        i2c_bus_unlock();

        if (err == ESP_OK && err_acc == ESP_OK && err_gyr == ESP_OK && err_mag == ESP_OK && err_tmp == ESP_OK) {
            bno055_imu_snapshot_t snap = {
                .valid = true,
                .calibrated = s_bno.config.is_calibrated,
                .yaw_deg = s_bno.euler_angle.yaw,
                .roll_deg = s_bno.euler_angle.roll - s_roll_offset,
                .pitch_deg = s_bno.euler_angle.pitch - s_pitch_offset,
                .temperature_c = s_bno.temperature,
                .cal_sys = s_bno.config.calibration.sys,
                .cal_gyro = s_bno.config.calibration.gyro,
                .cal_accel = s_bno.config.calibration.xl,
                .cal_mag = s_bno.config.calibration.mag,
                .timestamp_us = esp_timer_get_time(),
            };
            bno055_imu_update(&snap);
            ESP_LOGI(TAG,
                     "Euler: roll=%.2f pitch=%.2f yaw=%.2f | Acc=%.2f/%.2f/%.2f m/s² | Gyro=%.2f/%.2f/%.2f dps | Mag=%.2f/%.2f/%.2f uT | Temp=%.1f°C | Calib S/G/A/M=%u/%u/%u/%u",
                     s_bno.euler_angle.roll,
                     s_bno.euler_angle.pitch,
                     s_bno.euler_angle.yaw,
                     s_bno.raw_acceleration.x,
                     s_bno.raw_acceleration.y,
                     s_bno.raw_acceleration.z,
                     s_bno.gyroscope.x,
                     s_bno.gyroscope.y,
                     s_bno.gyroscope.z,
                     s_bno.magnetometer.x,
                     s_bno.magnetometer.y,
                     s_bno.magnetometer.z,
                     s_bno.temperature,
                     (err_cal == ESP_OK) ? s_bno.config.calibration.sys : 0,
                     (err_cal == ESP_OK) ? s_bno.config.calibration.gyro : 0,
                     (err_cal == ESP_OK) ? s_bno.config.calibration.xl : 0,
                     (err_cal == ESP_OK) ? s_bno.config.calibration.mag : 0);
            
            // Periodisch Kalibrierung speichern (alle 60s prüfen)
            int64_t now_us = esp_timer_get_time();
            if (now_us - s_last_persist_check_us >= 60000000LL) {
                s_last_persist_check_us = now_us;
                
                // Bei totalem Kalibrierungsverlust (SYS=0 oder 1): gespeicherte Offsets neu laden
                if (err_cal == ESP_OK && s_bno.config.calibration.sys <= 1 && s_offset_cache_valid) {
                    ESP_LOGW(TAG, "BNO055 calibration lost (SYS=%u), restoring from NVS", s_bno.config.calibration.sys);
                    i2c_bus_lock();
                    bool restored = bno055_restore_calibration(&s_bno);
                    i2c_bus_unlock();
                    if (!restored) {
                        ESP_LOGW(TAG, "Failed to restore calibration");
                    }
                }
                
                // Bei guter Kalibrierung (SYS=3): Offsets speichern falls geändert
                if (s_bno.config.is_calibrated) {
                    bno055_persist_calibration_if_needed(&s_bno);
                }
            }
        } else {
            bno055_imu_snapshot_t snap = {
                .valid = false,
                .calibrated = false,
                .timestamp_us = esp_timer_get_time(),
            };
            bno055_imu_update(&snap);
            ESP_LOGW(TAG, "BNO055 read failed: eul=%s acc=%s gyr=%s mag=%s tmp=%s",
                     esp_err_to_name(err),
                     esp_err_to_name(err_acc),
                     esp_err_to_name(err_gyr),
                     esp_err_to_name(err_mag),
                     esp_err_to_name(err_tmp));
        }
        uint32_t interval_ms = 1000;
        int64_t now_us = esp_timer_get_time();
        portENTER_CRITICAL(&s_fast_mux);
        int64_t fast_until = s_fast_until_us;
        uint32_t fast_interval = s_fast_interval_ms;
        portEXIT_CRITICAL(&s_fast_mux);
        if (fast_until > now_us) {
            interval_ms = fast_interval;
        }
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}

void bno055_app_sleep(void)
{
    /* BNO055 PWR_MODE = Suspend (0x02): ~0,2 µA statt 12,3 mA.
     * Sequenz laut Datenblatt: erst CONFIG_MODE setzen, dann PWR_MODE. */
    if (s_bno.config.slave_handle == NULL) return;

    /* 1. OPR_MODE → CONFIG_MODE (0x3D = 0x00) */
    uint8_t cfg_mode[2] = {0x3D, 0x00};
    i2c_master_transmit(s_bno.config.slave_handle, cfg_mode, sizeof(cfg_mode), 50);
    vTaskDelay(pdMS_TO_TICKS(25));

    /* 2. PWR_MODE → Suspend (0x3E = 0x02) */
    uint8_t suspend[2]  = {0x3E, 0x02};
    i2c_master_transmit(s_bno.config.slave_handle, suspend, sizeof(suspend), 50);

    ESP_LOGD(TAG, "BNO055 Suspend");
}

esp_err_t bno055_app_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "I2C init fehlgeschlagen");

    i2c_master_bus_handle_t bus_internal = i2c_bus_get_internal();
    ESP_RETURN_ON_FALSE(bus_internal != NULL, ESP_FAIL, TAG, "internal bus handle missing");

    i2c_device_config_t bno_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x28,
        .flags.disable_ack_check = 0,
        .scl_speed_hz = SENSOR_I2C_EXT_SPEED_HZ,
        .scl_wait_us = 0xffff,
    };

    i2c_bus_lock();
    esp_err_t add_err = i2c_master_bus_add_device(bus_internal, &bno_conf, &s_bno.config.slave_handle);
    i2c_bus_unlock();
    ESP_RETURN_ON_ERROR(add_err, TAG, "add device failed");

    s_bno.config.reset.method = RESET_SW;
    s_bno.config.reset.pin = -1;
    s_bno.config.state.mode = CONFIG_MODE;
    s_bno.config.state.units = ACC_M_S2 | GY_DPS | EUL_DEG | TEMP_C | ORI_ANDRIOD;
    s_bno.config.state.page = 0;
    // Externes Quarz aktivieren (falls bestückt)
    s_bno.config.state.external_crystal = false;

    // Einmaliger Soft-Reset vor der Initialisierung
    i2c_bus_lock();
    esp_err_t rst_err = bno055_reset_chip(&s_bno);
    i2c_bus_unlock();
    if (rst_err != ESP_OK) {
        ESP_LOGW(TAG, "BNO055 soft reset failed: %s", esp_err_to_name(rst_err));
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    i2c_bus_lock();
    esp_err_t init_err = bno055_initialize(&s_bno);
    i2c_bus_unlock();
    ESP_RETURN_ON_ERROR(init_err, TAG, "init failed");

    i2c_bus_lock();
    esp_err_t cfg_err = bno055_configure(&s_bno, NDOF_MODE, s_bno.config.state.units);
    i2c_bus_unlock();
    ESP_RETURN_ON_ERROR(cfg_err, TAG, "configure failed");

    // Axis-Remapping anwenden
    i2c_bus_lock();
    esp_err_t axis_err = bno055_apply_axis_map(&s_bno);
    i2c_bus_unlock();
    if (axis_err != ESP_OK) {
        ESP_LOGW(TAG, "BNO055 axis remapping failed: %s", esp_err_to_name(axis_err));
    } else {
        ESP_LOGI(TAG, "BNO055 axis remapping applied (vehicle coordinates)");
    }

    // Gespeicherte Kalibrierung wiederherstellen
    vTaskDelay(pdMS_TO_TICKS(200));
    i2c_bus_lock();
    bool restored = bno055_restore_calibration(&s_bno);
    i2c_bus_unlock();
    if (!restored) {
        ESP_LOGI(TAG, "BNO055 starting without stored calibration");
    }

    // Pitch/Roll-Nullpunkt aus NVS laden
    bno055_load_zero_offsets();

    BaseType_t r = xTaskCreatePinnedToCore(bno055_task, "bno055_task", 4096, NULL, 4, &s_task, 0);
    if (r != pdPASS) {
        s_task = NULL;
        ESP_RETURN_ON_ERROR(ESP_FAIL, TAG, "task start failed");
    }

    ESP_LOGI(TAG, "BNO055 started on I2C%d (SDA=%d, SCL=%d)",
             SENSOR_I2C_EXT_PORT, SENSOR_I2C_EXT_SDA_GPIO, SENSOR_I2C_EXT_SCL_GPIO);
    return ESP_OK;
}
