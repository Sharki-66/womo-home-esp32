#include "womo_rs485_display.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "rs485_display";

// RS485 Hardware Configuration
#define RS485_UART_NUM      UART_NUM_1
#define RS485_TX_GPIO       16  // Hardware connector pin (Waveshare: TXD=16)
#define RS485_RX_GPIO       15  // Hardware connector pin (Waveshare: RXD=15)
#define RS485_DE_GPIO       -1  // DE handled by onboard transistor logic
#define RS485_BAUD_RATE     115200
#define RS485_BUF_SIZE      2048
#define RS485_LINE_MAX      4096

// State
static bool s_initialized = false;
static womo_sensor_data_t s_latest_data = {0};
static SemaphoreHandle_t s_data_mutex = NULL;
static womo_rs485_data_cb_t s_data_callback = NULL;
static void *s_callback_user_data = NULL;
static womo_rs485_event_cb_t s_event_callback = NULL;
static void *s_event_user_data = NULL;

static void rs485_emit_event(womo_rs485_event_t event)
{
    if (s_event_callback) {
        s_event_callback(event, s_event_user_data);
    }
}

static void update_bno_from_json(const cJSON *imu, womo_sensor_data_t *data)
{
    if (!imu || !data) {
        return;
    }

    const cJSON *yaw = cJSON_GetObjectItem(imu, "yaw_deg");
    if (cJSON_IsNumber(yaw)) {
        data->bno.heading_deg = (float)yaw->valuedouble;
    }

    const cJSON *pitch = cJSON_GetObjectItem(imu, "pitch_deg");
    if (cJSON_IsNumber(pitch)) {
        data->bno.pitch_deg = (float)pitch->valuedouble;
    }

    const cJSON *roll = cJSON_GetObjectItem(imu, "roll_deg");
    if (cJSON_IsNumber(roll)) {
        data->bno.roll_deg = (float)roll->valuedouble;
    }

    const cJSON *direction = cJSON_GetObjectItem(imu, "hdg");
    if (cJSON_IsString(direction) && direction->valuestring) {
        strncpy(data->bno.direction, direction->valuestring, sizeof(data->bno.direction) - 1);
        data->bno.direction[sizeof(data->bno.direction) - 1] = '\0';
    }

    const cJSON *cal = cJSON_GetObjectItem(imu, "cal");
    if (cJSON_IsObject(cal)) {
        const cJSON *cal_sys = cJSON_GetObjectItem(cal, "sys");
        const cJSON *cal_gyro = cJSON_GetObjectItem(cal, "gyro");
        const cJSON *cal_acc = cJSON_GetObjectItem(cal, "acc");
        const cJSON *cal_mag = cJSON_GetObjectItem(cal, "mag");

        if (cJSON_IsNumber(cal_sys)) {
            data->bno.cal_sys = (uint8_t)cal_sys->valuedouble;
        }
        if (cJSON_IsNumber(cal_gyro)) {
            data->bno.cal_gyro = (uint8_t)cal_gyro->valuedouble;
        }
        if (cJSON_IsNumber(cal_acc)) {
            data->bno.cal_accel = (uint8_t)cal_acc->valuedouble;
        }
        if (cJSON_IsNumber(cal_mag)) {
            data->bno.cal_mag = (uint8_t)cal_mag->valuedouble;
        }
    }

    data->bno.valid = true;
}

// Forward declarations
static void rs485_rx_task(void *arg);
static void parse_json_packet(const char *json_str);
static esp_err_t rs485_write_and_wait(const char *payload);

static esp_err_t rs485_write_and_wait(const char *payload)
{
    if (!s_initialized || !payload) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t len = strlen(payload);
    if (len == 0) {
        return ESP_OK;
    }

    int written = uart_write_bytes(RS485_UART_NUM, payload, len);
    if (written <= 0) {
        return ESP_FAIL;
    }

    esp_err_t wait_err = uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(20));
    if (wait_err != ESP_OK) {
        ESP_LOGW(TAG, "uart_wait_tx_done failed: %s", esp_err_to_name(wait_err));
        return wait_err;
    }

    return ESP_OK;
}

esp_err_t womo_rs485_display_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    // Create mutex for data protection
    s_data_mutex = xSemaphoreCreateMutex();
    if (!s_data_mutex) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Configure UART (like demo example)
    uart_config_t uart_config = {
        .baud_rate = RS485_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,  // Use DEFAULT instead of APB (like demo)
    };
    
    // Install driver first (RX buffer + TX buffer)
    esp_err_t err = uart_driver_install(RS485_UART_NUM, RS485_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_data_mutex);
        return err;
    }
    
    // Configure parameters second
    err = uart_param_config(RS485_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        uart_driver_delete(RS485_UART_NUM);
        vSemaphoreDelete(s_data_mutex);
        return err;
    }
    
    // Set pins third - TX must be set even for RX-only to control DE/RE via transistor
    err = uart_set_pin(RS485_UART_NUM,
                       RS485_TX_GPIO,
                       RS485_RX_GPIO,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(RS485_UART_NUM);
        vSemaphoreDelete(s_data_mutex);
        return err;
    }
    
    ESP_LOGI(TAG, "UART pins configured: TX=%d RX=%d (TX controls DE/RE via transistor)", 
             RS485_TX_GPIO, RS485_RX_GPIO);
    
    // Only set RS485 mode if we have a DE pin, otherwise use normal UART
    ESP_LOGI(TAG, "RS485 DE pin not configured - using plain UART mode (transistor auto-DE)");
    
    // Start RX task with large stack (16KB for JSON parsing + LVGL callback + buffers)
    BaseType_t task_created = xTaskCreate(rs485_rx_task, "rs485_rx", 16384, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        uart_driver_delete(RS485_UART_NUM);
        vSemaphoreDelete(s_data_mutex);
        return ESP_FAIL;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "RS485 display receiver initialized (UART%d, %d baud)", RS485_UART_NUM, RS485_BAUD_RATE);
    return ESP_OK;
}

void womo_rs485_set_data_callback(womo_rs485_data_cb_t callback, void *user_data)
{
    s_data_callback = callback;
    s_callback_user_data = user_data;
}

void womo_rs485_set_event_callback(womo_rs485_event_cb_t callback, void *user_data)
{
    s_event_callback = callback;
    s_event_user_data = user_data;
}

bool womo_rs485_get_latest_data(womo_sensor_data_t *data)
{
    if (!data || !s_initialized) {
        return false;
    }
    
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(data, &s_latest_data, sizeof(womo_sensor_data_t));
        xSemaphoreGive(s_data_mutex);
        return true;
    }
    
    return false;
}

esp_err_t womo_rs485_send_level_start(void)
{
    const char *cmd = "{\"cmd\":\"level_start\"}\n";
    esp_err_t err = rs485_write_and_wait(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent level_start command");
    }
    return err;
}

esp_err_t womo_rs485_send_level_stop(void)
{
    const char *cmd = "{\"cmd\":\"level_stop\"}\n";
    esp_err_t err = rs485_write_and_wait(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent level_stop command");
    }
    return err;
}

esp_err_t womo_rs485_send_tare_a(void)
{
    const char *cmd = "{\"cmd\":\"tare_a\"}\n";
    esp_err_t err = rs485_write_and_wait(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent tare_a command");
    }
    return err;
}

esp_err_t womo_rs485_send_tare_b(void)
{
    const char *cmd = "{\"cmd\":\"tare_b\"}\n";
    esp_err_t err = rs485_write_and_wait(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent tare_b command");
    }
    return err;
}

esp_err_t womo_rs485_send_display_ready(void)
{
    const char *cmd = "{\"cmd\":\"display_ready\"}\n";
    esp_err_t err = rs485_write_and_wait(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent display_ready acknowledgement");
    } else {
        ESP_LOGW(TAG, "Failed to send display_ready acknowledgement: %s", esp_err_to_name(err));
    }
    return err;
}

static void rs485_rx_task(void *arg)
{
    ESP_LOGI(TAG, "RS485 RX task started (stack: 8KB)");
    
    uint8_t buffer[RS485_BUF_SIZE];
    char line_buffer[RS485_LINE_MAX];
    size_t line_pos = 0;
    uint32_t packet_count = 0;
    
    while (true) {
        int len = uart_read_bytes(RS485_UART_NUM, buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            packet_count++;
            ESP_LOGI(TAG, "*** RX: %d bytes (pkt#%lu) ***", len, packet_count);
            buffer[len] = '\0';
            
            // Process byte by byte to extract complete JSON lines
            for (int i = 0; i < len; i++) {
                char c = (char)buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "Calling parse_json_packet...");
                        parse_json_packet(line_buffer);
                        ESP_LOGI(TAG, "parse_json_packet returned OK");
                        line_pos = 0;
                    }
                } else if (line_pos < sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                } else {
                    ESP_LOGW(TAG, "Line buffer overflow (%d bytes) - dropping partial JSON", line_pos);
                    line_pos = 0;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void parse_json_packet(const char *json_str)
{
    ESP_LOGI(TAG, "parse_json: start (len=%d)", strlen(json_str));
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed: %s", json_str);
        rs485_emit_event(WOMO_RS485_EVENT_INVALID_JSON);
        return;
    }
    
    ESP_LOGI(TAG, "JSON parsed OK");
    
    cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    if (!type_obj || !cJSON_IsString(type_obj)) {
        ESP_LOGW(TAG, "No type field");
        cJSON_Delete(root);
        rs485_emit_event(WOMO_RS485_EVENT_INVALID_JSON);
        return;
    }
    
    const char *type = type_obj->valuestring;
    ESP_LOGI(TAG, "Type: %s", type);

    if (strcmp(type, "hello") == 0) {
        const cJSON *fw = cJSON_GetObjectItem(root, "fw");
        const cJSON *uptime = cJSON_GetObjectItem(root, "uptime");
        if (cJSON_IsString(fw)) {
            ESP_LOGI(TAG, "Walter hello: fw=%s", fw->valuestring);
        }
        if (cJSON_IsNumber(uptime)) {
            ESP_LOGI(TAG, "Walter uptime: %.0f s", uptime->valuedouble);
        }
        rs485_emit_event(WOMO_RS485_EVENT_HELLO);
        womo_rs485_send_display_ready();
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "hb") == 0) {
        rs485_emit_event(WOMO_RS485_EVENT_HEARTBEAT);
        cJSON_Delete(root);
        return;
    }
    
    // Parse full sensor data
    if (strcmp(type, "full") == 0) {
        womo_sensor_data_t data = {0};
        
        // Timestamp
        cJSON *ts = cJSON_GetObjectItem(root, "ts");
        if (ts && cJSON_IsNumber(ts)) {
            data.timestamp_ms = (uint64_t)ts->valuedouble;
        }
        
        // BNO055 / IMU (legacy "bno" block or new "imu" object)
        bool imu_parsed = false;
        cJSON *bno = cJSON_GetObjectItem(root, "bno");
        if (bno && cJSON_IsObject(bno)) {
            cJSON *h = cJSON_GetObjectItem(bno, "h");
            cJSON *r = cJSON_GetObjectItem(bno, "r");
            cJSON *p = cJSON_GetObjectItem(bno, "p");
            cJSON *dir = cJSON_GetObjectItem(bno, "dir");
            cJSON *cal = cJSON_GetObjectItem(bno, "cal");

            if (h || r || p || (dir && cJSON_IsString(dir))) {
                data.bno.valid = true;
                if (h) data.bno.heading_deg = (float)h->valuedouble;
                if (r) data.bno.roll_deg = (float)r->valuedouble;
                if (p) data.bno.pitch_deg = (float)p->valuedouble;
                if (dir && cJSON_IsString(dir)) {
                    snprintf(data.bno.direction, sizeof(data.bno.direction), "%s", dir->valuestring);
                }
                if (cal && cJSON_IsArray(cal) && cJSON_GetArraySize(cal) == 4) {
                    data.bno.cal_sys = (uint8_t)cJSON_GetArrayItem(cal, 0)->valueint;
                    data.bno.cal_gyro = (uint8_t)cJSON_GetArrayItem(cal, 1)->valueint;
                    data.bno.cal_accel = (uint8_t)cJSON_GetArrayItem(cal, 2)->valueint;
                    data.bno.cal_mag = (uint8_t)cJSON_GetArrayItem(cal, 3)->valueint;
                }
                imu_parsed = true;
            }
        }

        if (!imu_parsed) {
            cJSON *imu = cJSON_GetObjectItem(root, "imu");
            if (imu && cJSON_IsObject(imu)) {
                cJSON *yaw = cJSON_GetObjectItem(imu, "yaw_deg");
                cJSON *pitch = cJSON_GetObjectItem(imu, "pitch_deg");
                cJSON *roll = cJSON_GetObjectItem(imu, "roll_deg");
                cJSON *heading_str = cJSON_GetObjectItem(imu, "hdg");
                cJSON *cal = cJSON_GetObjectItem(imu, "cal");

                if (yaw || pitch || roll || (heading_str && cJSON_IsString(heading_str))) {
                    data.bno.valid = true;
                    if (yaw) data.bno.heading_deg = (float)yaw->valuedouble;
                    if (roll) data.bno.roll_deg = (float)roll->valuedouble;
                    if (pitch) data.bno.pitch_deg = (float)pitch->valuedouble;
                    if (heading_str && cJSON_IsString(heading_str)) {
                        snprintf(data.bno.direction, sizeof(data.bno.direction), "%s", heading_str->valuestring);
                    }

                    if (cal && cJSON_IsObject(cal)) {
                        cJSON *cal_sys = cJSON_GetObjectItem(cal, "sys");
                        cJSON *cal_gyro = cJSON_GetObjectItem(cal, "gyro");
                        cJSON *cal_acc = cJSON_GetObjectItem(cal, "acc");
                        cJSON *cal_mag = cJSON_GetObjectItem(cal, "mag");
                        if (cal_sys) data.bno.cal_sys = (uint8_t)cal_sys->valueint;
                        if (cal_gyro) data.bno.cal_gyro = (uint8_t)cal_gyro->valueint;
                        if (cal_acc) data.bno.cal_accel = (uint8_t)cal_acc->valueint;
                        if (cal_mag) data.bno.cal_mag = (uint8_t)cal_mag->valueint;
                    }

                    imu_parsed = true;
                }
            }
        }
        
        // HX711
        cJSON *hx = cJSON_GetObjectItem(root, "hx");
        if (hx) {
            data.hx711.valid = true;
            cJSON *a = cJSON_GetObjectItem(hx, "a");
            cJSON *b = cJSON_GetObjectItem(hx, "b");
            cJSON *sum = cJSON_GetObjectItem(hx, "sum");
            
            if (a) data.hx711.weight_a_kg = (float)a->valuedouble;
            if (b) data.hx711.weight_b_kg = (float)b->valuedouble;
            if (sum) data.hx711.weight_sum_kg = (float)sum->valuedouble;
        }
        
        // BME680 (supports legacy flat payload and new per-sensor map)
        cJSON *bme = cJSON_GetObjectItem(root, "bme");
        if (bme && cJSON_IsObject(bme)) {
            bool parsed = false;

            // Legacy payload: direct values inside "bme" (t/h/p/g/iaq)
            cJSON *t = cJSON_GetObjectItem(bme, "t");
            cJSON *h = cJSON_GetObjectItem(bme, "h");
            cJSON *p = cJSON_GetObjectItem(bme, "p");
            cJSON *g = cJSON_GetObjectItem(bme, "g");
            cJSON *iaq = cJSON_GetObjectItem(bme, "iaq");

            if (t || h || p || g || iaq) {
                data.bme680.valid = true;
                if (t) data.bme680.temperature_c = (float)t->valuedouble;
                if (h) data.bme680.humidity_percent = (float)h->valuedouble;
                if (p) data.bme680.pressure_hpa = (float)p->valuedouble;
                if (g) data.bme680.gas_kohm = (float)g->valuedouble;
                if (iaq) data.bme680.iaq = (uint16_t)iaq->valueint;
                parsed = true;
            }

            // New payload: map of sensors keyed by I2C address (e.g., "0x77")
            if (!parsed) {
                for (cJSON *sensor = bme->child; sensor != NULL; sensor = sensor->next) {
                    if (!cJSON_IsObject(sensor)) {
                        continue;
                    }

                    cJSON *temp_c = cJSON_GetObjectItem(sensor, "temp_c");
                    cJSON *rh_pct = cJSON_GetObjectItem(sensor, "rh_pct");
                    cJSON *press_hpa = cJSON_GetObjectItem(sensor, "press_hpa");
                    cJSON *gas_kohm = cJSON_GetObjectItem(sensor, "gas_kohm");
                    cJSON *iaq_score = cJSON_GetObjectItem(sensor, "iaq");

                    if (!(temp_c || rh_pct || press_hpa || gas_kohm || iaq_score)) {
                        continue;
                    }

                    data.bme680.valid = true;
                    if (temp_c) data.bme680.temperature_c = (float)temp_c->valuedouble;
                    if (rh_pct) data.bme680.humidity_percent = (float)rh_pct->valuedouble;
                    if (press_hpa) data.bme680.pressure_hpa = (float)press_hpa->valuedouble;
                    if (gas_kohm) data.bme680.gas_kohm = (float)gas_kohm->valuedouble;
                    if (iaq_score) data.bme680.iaq = (uint16_t)iaq_score->valueint;
                    parsed = true;
                    break;  // Use first sensor entry with data
                }
            }

            if (!parsed) {
                ESP_LOGW(TAG, "BME680 payload present but no recognised fields");
            }
        }
        
        // Battery
        cJSON *bat = cJSON_GetObjectItem(root, "bat");
        if (bat) {
            data.battery.valid = true;
            cJSON *b1 = cJSON_GetObjectItem(bat, "b1");
            cJSON *b2 = cJSON_GetObjectItem(bat, "b2");
            
            if (b1) data.battery.battery1_v = (float)b1->valuedouble;
            if (b2) data.battery.battery2_v = (float)b2->valuedouble;
        }
        
        // Tank
        cJSON *tank = cJSON_GetObjectItem(root, "tank");
        if (tank) {
            data.tank.valid = true;
            cJSON *t1 = cJSON_GetObjectItem(tank, "t1");
            cJSON *t2 = cJSON_GetObjectItem(tank, "t2");
            
            if (t1) data.tank.tank1_percent = (uint8_t)t1->valueint;
            if (t2) data.tank.tank2_percent = (uint8_t)t2->valueint;
        }
        
        // Parse GPS data
        cJSON *gps = cJSON_GetObjectItem(root, "gps");
        if (gps) {
            data.gps.valid = true;
            cJSON *lat = cJSON_GetObjectItem(gps, "lat");
            cJSON *lon = cJSON_GetObjectItem(gps, "lon");
            cJSON *alt = cJSON_GetObjectItem(gps, "alt");
            cJSON *spd = cJSON_GetObjectItem(gps, "spd");
            cJSON *hdg = cJSON_GetObjectItem(gps, "hdg");
            cJSON *sat = cJSON_GetObjectItem(gps, "sat");
            cJSON *conf = cJSON_GetObjectItem(gps, "conf");
            
            if (lat) data.gps.latitude = lat->valuedouble;
            if (lon) data.gps.longitude = lon->valuedouble;
            if (alt) data.gps.altitude_m = alt->valuedouble;
            if (spd) data.gps.speed_kmh = (float)spd->valuedouble;
            if (hdg) data.gps.heading_deg = (float)hdg->valuedouble;
            if (sat) data.gps.satellites = (uint8_t)sat->valueint;
            if (conf) data.gps.confidence_m = (float)conf->valuedouble;
        }

        // LTE status
        cJSON *lte = cJSON_GetObjectItem(root, "lte");
        if (lte && cJSON_IsObject(lte)) {
            cJSON *registered = cJSON_GetObjectItem(lte, "registered");
            cJSON *operator_name = cJSON_GetObjectItem(lte, "operator");
            cJSON *rsrp = cJSON_GetObjectItem(lte, "rsrp_dbm");
            cJSON *signal_pct = cJSON_GetObjectItem(lte, "signal_pct");

            if (registered || operator_name || rsrp || signal_pct) {
                data.lte.valid = true;

                if (registered) {
                    if (cJSON_IsBool(registered)) {
                        data.lte.registered = cJSON_IsTrue(registered);
                    } else if (cJSON_IsNumber(registered)) {
                        data.lte.registered = registered->valueint != 0;
                    }
                }

                if (operator_name && cJSON_IsString(operator_name)) {
                    snprintf(data.lte.operator_name, sizeof(data.lte.operator_name), "%s", operator_name->valuestring);
                }

                if (rsrp && cJSON_IsNumber(rsrp)) {
                    data.lte.rsrp_dbm = (float)rsrp->valuedouble;
                }

                if (signal_pct && cJSON_IsNumber(signal_pct)) {
                    int value = signal_pct->valueint;
                    if (value < 0) value = 0;
                    if (value > 100) value = 100;
                    data.lte.signal_percent = (uint8_t)value;
                }
            }
        }
        
        // Store latest data
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memcpy(&s_latest_data, &data, sizeof(womo_sensor_data_t));
            xSemaphoreGive(s_data_mutex);
            
            // Call callback if set
            if (s_data_callback) {
                s_data_callback(&data, s_callback_user_data);
            }
        }
        
        if (data.hx711.valid) {
            ESP_LOGI(TAG, "Received full data: BNO=%s(%.1f°) Weights A=%.1fkg B=%.1fkg Temp=%.1fC",
                     data.bno.direction, data.bno.heading_deg,
                     data.hx711.weight_a_kg, data.hx711.weight_b_kg, data.bme680.temperature_c);
        } else {
            ESP_LOGI(TAG, "Received full data: BNO=%s(%.1f°) Temp=%.1fC",
                     data.bno.direction, data.bno.heading_deg, data.bme680.temperature_c);
        }
    }
    else if (strcmp(type, "imu") == 0) {
        cJSON *imu = cJSON_GetObjectItem(root, "imu");
        if (!cJSON_IsObject(imu)) {
            ESP_LOGW(TAG, "IMU packet missing imu object");
        } else {
            womo_sensor_data_t updated = {0};
            bool notify = false;

            if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                updated = s_latest_data;

                cJSON *ts = cJSON_GetObjectItem(root, "ts");
                if (cJSON_IsNumber(ts)) {
                    updated.timestamp_ms = (uint64_t)ts->valuedouble;
                }

                update_bno_from_json(imu, &updated);

                s_latest_data = updated;
                xSemaphoreGive(s_data_mutex);
                notify = true;
            }

            if (notify && s_data_callback) {
                s_data_callback(&updated, s_callback_user_data);
            }
        }
    }
    // Parse level data (legacy BNO only)
    else if (strcmp(type, "level") == 0) {
        cJSON *bno = cJSON_GetObjectItem(root, "bno");
        if (bno && xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_latest_data.bno.valid = true;

            cJSON *h = cJSON_GetObjectItem(bno, "h");
            cJSON *r = cJSON_GetObjectItem(bno, "r");
            cJSON *p = cJSON_GetObjectItem(bno, "p");
            cJSON *dir = cJSON_GetObjectItem(bno, "dir");

            if (h) s_latest_data.bno.heading_deg = (float)h->valuedouble;
            if (r) s_latest_data.bno.roll_deg = (float)r->valuedouble;
            if (p) s_latest_data.bno.pitch_deg = (float)p->valuedouble;
            if (dir && cJSON_IsString(dir)) {
                strncpy(s_latest_data.bno.direction, dir->valuestring,
                        sizeof(s_latest_data.bno.direction) - 1);
            }

            xSemaphoreGive(s_data_mutex);

            // Fast update for leveling - callback with partial data
            if (s_data_callback) {
                womo_sensor_data_t level_data = {0};
                level_data.bno = s_latest_data.bno;
                s_data_callback(&level_data, s_callback_user_data);
            }
        }
    }
    else {
        ESP_LOGW(TAG, "Unrecognized RS485 packet type: %s", type);
        rs485_emit_event(WOMO_RS485_EVENT_INVALID_JSON);
    }
    
    cJSON_Delete(root);
}
