/**
 * @file womo_gps.cpp
 * @brief GPS/GNSS wrapper for Walter Modem
 * 
 * Based on DPTechnics walter-modem positioning.cpp example
 */

#include "womo_gps.h"
#include "WalterModem.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "womo_gps";

// Global modem instance (extern, defined in main.cpp)
extern WalterModem modem;

// Last received GPS fix
static womo_gps_data_t s_last_fix = {};
static bool s_fix_received = false;
static SemaphoreHandle_t s_gps_mutex = NULL;

// Flag to signal when a fix is received
static volatile bool s_gnss_fix_rcvd = false;
static WalterModemGNSSFix s_latest_gnss_fix = {};

// LTE control callback
static womo_gps_lte_control_cb_t s_lte_control_cb = nullptr;

static const char *modem_state_str(WalterModemState state)
{
    switch (state) {
    case WALTER_MODEM_STATE_OK:
        return "OK";
    case WALTER_MODEM_STATE_ERROR:
        return "ERROR";
    case WALTER_MODEM_STATE_TIMEOUT:
        return "TIMEOUT";
    case WALTER_MODEM_STATE_NO_MEMORY:
        return "NO_MEMORY";
    case WALTER_MODEM_STATE_NO_FREE_PDP_CONTEXT:
        return "NO_FREE_PDP";
    case WALTER_MODEM_STATE_NO_SUCH_PDP_CONTEXT:
        return "NO_SUCH_PDP";
    case WALTER_MODEM_STATE_NO_FREE_SOCKET:
        return "NO_FREE_SOCKET";
    case WALTER_MODEM_STATE_NO_SUCH_SOCKET:
        return "NO_SUCH_SOCKET";
    case WALTER_MODEM_STATE_NO_SUCH_PROFILE:
        return "NO_SUCH_PROFILE";
    case WALTER_MODEM_STATE_NOT_EXPECTING_RING:
        return "NOT_EXPECTING_RING";
    case WALTER_MODEM_STATE_AWAITING_RING:
        return "AWAITING_RING";
    case WALTER_MODEM_STATE_AWAITING_RESPONSE:
        return "AWAITING_RESPONSE";
    case WALTER_MODEM_STATE_BUSY:
        return "BUSY";
    case WALTER_MODEM_STATE_NO_DATA:
        return "NO_DATA";
    default:
        return "UNKNOWN";
    }
}

static const char *rsp_type_str(WalterModemRspDataType type)
{
    switch (type) {
    case WALTER_MODEM_RSP_DATA_TYPE_NO_DATA:
        return "NO_DATA";
    case WALTER_MODEM_RSP_DATA_TYPE_OPSTATE:
        return "OPSTATE";
    case WALTER_MODEM_RSP_DATA_TYPE_RAT:
        return "RAT";
    case WALTER_MODEM_RSP_DATA_TYPE_RSSI:
        return "RSSI";
    case WALTER_MODEM_RSP_DATA_TYPE_SIGNAL_QUALITY:
        return "SIGNAL_QUALITY";
    case WALTER_MODEM_RSP_DATA_TYPE_CELL_INFO:
        return "CELL_INFO";
    case WALTER_MODEM_RSP_DATA_TYPE_SIM_STATE:
        return "SIM_STATE";
    case WALTER_MODEM_RSP_DATA_TYPE_SIM_CARD_ID:
        return "SIM_CARD_ID";
    case WALTER_MODEM_RSP_DATA_TYPE_SIM_CARD_IMSI:
        return "SIM_CARD_IMSI";
    case WALTER_MODEM_RSP_DATA_TYPE_CME_ERROR:
        return "CME_ERROR";
    case WALTER_MODEM_RSP_DATA_TYPE_PDP_CTX_ID:
        return "PDP_CTX_ID";
    case WALTER_MODEM_RSP_DATA_TYPE_BANDSET_CFG_SET:
        return "BANDSET_CFG";
    case WALTER_MODEM_RSP_DATA_TYPE_PDP_ADDR:
        return "PDP_ADDR";
    case WALTER_MODEM_RSP_DATA_TYPE_SOCKET_ID:
        return "SOCKET_ID";
    case WALTER_MODEM_RSP_DATA_TYPE_GNSS_ASSISTANCE_DATA:
        return "GNSS_ASSIST";
    case WALTER_MODEM_RSP_DATA_TYPE_GNSS_UTC_TIME:
        return "GNSS_UTC";
    case WALTER_MODEM_RSP_DATA_TYPE_CLOCK:
        return "CLOCK";
    case WALTER_MODEM_RSP_DATA_TYPE_IDENTITY:
        return "IDENTITY";
    case WALTER_MODEM_RSP_DATA_TYPE_BLUECHERRY:
        return "BLUECHERRY";
    case WALTER_MODEM_RSP_DATA_TYPE_HTTP_RESPONSE:
        return "HTTP";
    case WALTER_MODEM_RSP_DATA_TYPE_COAP:
        return "COAP";
    case WALTER_MODEM_RSP_DATA_TYPE_MQTT:
        return "MQTT";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Convert north/east speed to heading and ground speed
 */
static void calculate_speed_heading(double north_speed, double east_speed, 
                                    float *speed_kmh, float *heading_deg)
{
    // Calculate ground speed from north/east components
    double ground_speed_ms = sqrt(north_speed * north_speed + east_speed * east_speed);
    *speed_kmh = (float)(ground_speed_ms * 3.6); // m/s to km/h
    
    // Calculate heading (0 = North, 90 = East, 180 = South, 270 = West)
    if (ground_speed_ms < 0.1) {
        *heading_deg = 0.0f; // No meaningful heading at very low speeds
    } else {
        double heading_rad = atan2(east_speed, north_speed);
        *heading_deg = (float)(heading_rad * 180.0 / M_PI);
        if (*heading_deg < 0) {
            *heading_deg += 360.0f;
        }
    }
}

/**
 * @brief GNSS event handler callback
 * 
 * Called by Walter modem when a GNSS fix is received.
 * This runs in the modem's event context - must not block!
 * 
 * Based on positioning.cpp example
 */
static void gnss_event_handler(const WalterModemGNSSFix *fix, void *args)
{
    if (!fix) {
        ESP_LOGW(TAG, "GNSS event handler called with NULL fix");
        return;
    }
    
    // Store the raw fix
    memcpy(&s_latest_gnss_fix, fix, sizeof(WalterModemGNSSFix));
    
    // Count satellites with good signal strength
    uint8_t good_sat_count = 0;
    for(int i = 0; i < fix->satCount; ++i) {
        if(fix->sats[i].signalStrength >= 30) {
            ++good_sat_count;
        }
    }
    
    ESP_LOGI(TAG, "GNSS fix received: status=%d, conf=%.2f, lat=%.6f, lon=%.6f, sats=%d (good=%d)",
             fix->status, fix->estimatedConfidence, fix->latitude, fix->longitude, 
             fix->satCount, good_sat_count);
    
    // Take mutex and update our data structure
    if (xSemaphoreTake(s_gps_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Copy fix data
        s_last_fix.valid = (fix->status == WALTER_MODEM_GNSS_FIX_STATUS_READY);
        s_last_fix.latitude = fix->latitude;
        s_last_fix.longitude = fix->longitude;
        s_last_fix.altitude_m = fix->height;
        s_last_fix.satellites = fix->satCount;
        s_last_fix.confidence_m = (float)fix->estimatedConfidence;
        s_last_fix.timestamp = fix->timestamp;
        s_last_fix.time_to_fix_ms = fix->timeToFix;
        
        // Calculate speed and heading from north/east components
        calculate_speed_heading(fix->northSpeed, fix->eastSpeed,
                               &s_last_fix.speed_kmh, &s_last_fix.heading_deg);
        
        s_fix_received = true;
        xSemaphoreGive(s_gps_mutex);
    }
    
    s_gnss_fix_rcvd = true;
}

extern "C" esp_err_t womo_gps_init(void)
{
    ESP_LOGI(TAG, "Initializing GPS subsystem");
    
    // Create mutex
    if (s_gps_mutex == NULL) {
        s_gps_mutex = xSemaphoreCreateMutex();
        if (s_gps_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create GPS mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Register GNSS event handler
    ESP_LOGI(TAG, "Registering GNSS event handler");
    modem.gnssSetEventHandler(gnss_event_handler, NULL);
    
    // Configure GNSS subsystem (from positioning.cpp example)
    ESP_LOGI(TAG, "Configuring GNSS subsystem");
    if (!modem.gnssConfig()) {
        ESP_LOGE(TAG, "Failed to configure GNSS subsystem");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "GPS subsystem initialized successfully");
    return ESP_OK;
}

/**
 * @brief Disconnect from LTE network (required before GNSS operation)
 * Based on lteDisconnect() from positioning.cpp
 */
static bool lte_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from LTE network for GNSS operation");
    
    // Set the operational state to minimum
    if(!modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
        ESP_LOGE(TAG, "Could not set operational state to MINIMUM");
        return false;
    }
    
    // Wait for the network to disconnect
    WalterModemNetworkRegState regState = modem.getNetworkRegState();
    int timeout = 50; // 5 seconds
    while(regState != WALTER_MODEM_NETWORK_REG_NOT_SEARCHING && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        regState = modem.getNetworkRegState();
        timeout--;
    }
    
    if (regState != WALTER_MODEM_NETWORK_REG_NOT_SEARCHING) {
        ESP_LOGW(TAG, "Network disconnect timeout (state=%d)", regState);
        return false;
    }
    
    ESP_LOGI(TAG, "Disconnected from LTE network");
    return true;
}

/**
 * @brief Check if GNSS clock is valid
 * Based on validateGNSSClock() from positioning.cpp
 */
static bool validate_gnss_clock(void)
{
    WalterModemRsp rsp = {};
    
    // Check if GNSS clock is valid
    if (!modem.gnssGetUTCTime(&rsp)) {
        ESP_LOGW(TAG, "Could not read GNSS UTC time");
        return false;
    }
    
    ESP_LOGI(TAG, "GNSS clock epoch time: %lld", rsp.data.clock.epochTime);
    
    if (rsp.data.clock.epochTime > 4) {
        ESP_LOGI(TAG, "GNSS clock is valid");
        return true;
    }
    
    ESP_LOGW(TAG, "GNSS clock is invalid (epoch <= 4)");
    ESP_LOGW(TAG, "Fix may take significantly longer without valid time");
    ESP_LOGW(TAG, "Consider syncing time via LTE/NTP before GNSS operation");
    
    // We continue anyway, but warn the user
    return true;
}

extern "C" esp_err_t womo_gps_request_fix(void)
{
    ESP_LOGI(TAG, "Requesting GNSS fix");
    
    // Validate GNSS clock (optional but recommended)
    validate_gnss_clock();
    
    // Disconnect from LTE (GNSS and LTE share the same radio)
    if (!lte_disconnect()) {
        ESP_LOGE(TAG, "Failed to disconnect from LTE network");
        return ESP_FAIL;
    }
    
    // Optional: Try to reconfigure GNSS for potential hot start
    // (This is from the positioning.cpp example)
    if (s_fix_received && s_last_fix.valid) {
        ESP_LOGI(TAG, "Reconfiguring GNSS for hot start (previous fix available)");
        if (!modem.gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH, WALTER_MODEM_GNSS_ACQ_MODE_HOT_START)) {
            ESP_LOGW(TAG, "Could not reconfigure GNSS for hot start");
        }
    }
    
    // Reset flag
    s_gnss_fix_rcvd = false;
    
    // Request a GNSS fix (from positioning.cpp example)
    // Try up to 5 times as in the example
    const int maxAttempts = 5;
    for(int attempt = 0; attempt < maxAttempts; ++attempt) {
        s_gnss_fix_rcvd = false;
        
        ESP_LOGI(TAG, "Starting GNSS fix request (attempt %d/%d)", attempt + 1, maxAttempts);
        
        WalterModemRsp fix_rsp = {};
        if(!modem.gnssPerformAction(WALTER_MODEM_GNSS_ACTION_GET_SINGLE_FIX, &fix_rsp)) {
            ESP_LOGE(TAG, "Could not request GNSS fix (attempt %d/%d) state=%s(%d) type=%s(%d)",
                     attempt + 1, maxAttempts, modem_state_str(fix_rsp.result),
                     (int)fix_rsp.result, rsp_type_str(fix_rsp.type), (int)fix_rsp.type);
            if(fix_rsp.type == WALTER_MODEM_RSP_DATA_TYPE_CME_ERROR) {
                ESP_LOGE(TAG, "CME error code: %d", (int)fix_rsp.data.cmeError);
            }

            
            if (attempt < maxAttempts - 1) {
                ESP_LOGI(TAG, "Retrying in 2 seconds...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "GNSS fix request sent successfully (attempt %d/%d)", attempt + 1, maxAttempts);
        ESP_LOGI(TAG, "Waiting for GNSS event callback...");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "All GNSS fix attempts failed");
    return ESP_FAIL;
}

extern "C" esp_err_t womo_gps_get_last_fix(womo_gps_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_gps_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_fix_received) {
            xSemaphoreGive(s_gps_mutex);
            return ESP_ERR_NOT_FOUND;
        }
        
        memcpy(data, &s_last_fix, sizeof(womo_gps_data_t));
        xSemaphoreGive(s_gps_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

extern "C" bool womo_gps_is_valid(void)
{
    bool valid = false;
    
    if (xSemaphoreTake(s_gps_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        valid = s_fix_received && s_last_fix.valid;
        xSemaphoreGive(s_gps_mutex);
    }
    
    return valid;
}

extern "C" esp_err_t womo_gps_get_utc_time(int64_t *epoch_time)
{
    if (!epoch_time) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WalterModemRsp rsp = {};
    if (!modem.gnssGetUTCTime(&rsp)) {
        ESP_LOGE(TAG, "Failed to get GNSS UTC time");
        return ESP_FAIL;
    }
    
    *epoch_time = rsp.data.clock.epochTime;
    return ESP_OK;
}

extern "C" esp_err_t womo_gps_set_utc_time(int64_t epoch_time)
{
    if (!modem.gnssSetUTCTime(epoch_time)) {
        ESP_LOGE(TAG, "Failed to set GNSS UTC time");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Set GNSS UTC time to %lld", epoch_time);
    return ESP_OK;
}

extern "C" esp_err_t womo_gps_register_lte_control(womo_gps_lte_control_cb_t callback)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_lte_control_cb = callback;
    ESP_LOGI(TAG, "LTE control callback registered");
    return ESP_OK;
}

/**
 * @brief Get current time from LTE network
 * Based on the positioning.cpp example which syncs time via LTE
 */
static esp_err_t fetch_time_from_lte(int64_t *epoch_time)
{
    if (!epoch_time) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WalterModemRsp rsp = {};
    
    // Try to get current clock from modem (includes network time if registered)
    if (!modem.getClock(&rsp)) {
        ESP_LOGW(TAG, "Failed to get clock from modem");
        return ESP_FAIL;
    }
    
    *epoch_time = rsp.data.clock.epochTime;
    ESP_LOGI(TAG, "Fetched time from LTE network: %lld", *epoch_time);
    return ESP_OK;
}

/**
 * @brief Wait for GNSS fix with timeout
 */
static esp_err_t wait_for_gnss_fix(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Waiting for GNSS fix (timeout %u ms)", timeout_ms);
    
    uint32_t elapsed_ms = 0;
    const uint32_t poll_interval_ms = 100;
    
    while (!s_gnss_fix_rcvd && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        elapsed_ms += poll_interval_ms;
        
        // Log progress every 10 seconds
        if (elapsed_ms % 10000 == 0) {
            ESP_LOGI(TAG, "Waiting for GNSS fix... %u/%u seconds", 
                     elapsed_ms / 1000, timeout_ms / 1000);
        }
    }
    
    if (!s_gnss_fix_rcvd) {
        ESP_LOGW(TAG, "GNSS fix timeout after %u ms", elapsed_ms);
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "GNSS fix received after %u ms", elapsed_ms);
    return ESP_OK;
}

extern "C" esp_err_t womo_gps_execute_fix_cycle(void)
{
    ESP_LOGI(TAG, "=== Starting complete GNSS fix cycle ===");
    
    esp_err_t result = ESP_OK;
    bool lte_was_disabled = false;
    
    // Step 1: Fetch time from LTE network (while still connected)
    ESP_LOGI(TAG, "Step 1: Fetching time from LTE network");
    int64_t network_time = 0;
    if (fetch_time_from_lte(&network_time) == ESP_OK) {
        // Set GNSS subsystem time for faster fix
        if (womo_gps_set_utc_time(network_time) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set GNSS time, continuing anyway");
        }
    } else {
        ESP_LOGW(TAG, "Could not fetch time from LTE, GNSS fix may take longer");
    }
    
    // Step 2: Disable LTE (required for GNSS - shared radio)
    ESP_LOGI(TAG, "Step 2: Disabling LTE for GNSS operation");
    if (s_lte_control_cb) {
        if (s_lte_control_cb(false) == ESP_OK) {
            lte_was_disabled = true;
            // Give LTE time to fully disconnect
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGI(TAG, "LTE disabled successfully");
        } else {
            ESP_LOGW(TAG, "Failed to disable LTE via callback, trying manual disconnect");
            if (!lte_disconnect()) {
                ESP_LOGE(TAG, "Failed to disconnect LTE");
                result = ESP_FAIL;
                goto cleanup;
            }
            lte_was_disabled = true;
        }
    } else {
        ESP_LOGI(TAG, "No LTE control callback, disconnecting manually");
        if (!lte_disconnect()) {
            ESP_LOGE(TAG, "Failed to disconnect LTE");
            result = ESP_FAIL;
            goto cleanup;
        }
        lte_was_disabled = true;
    }
    
    // Step 3: Execute GNSS fix
    ESP_LOGI(TAG, "Step 3: Requesting GNSS fix");
    if (womo_gps_request_fix() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to request GNSS fix");
        result = ESP_FAIL;
        goto cleanup;
    }
    
    // Wait for fix (timeout: 3 minutes for cold start, less for hot start)
    uint32_t fix_timeout_ms = s_fix_received ? 60000 : 180000;
    if (wait_for_gnss_fix(fix_timeout_ms) != ESP_OK) {
        ESP_LOGE(TAG, "GNSS fix timeout");
        result = ESP_ERR_TIMEOUT;
        // Continue to cleanup and re-enable LTE
    } else {
        ESP_LOGI(TAG, "GNSS fix completed successfully");
        result = ESP_OK;
    }
    
cleanup:
    // Step 4: Re-enable LTE
    if (lte_was_disabled) {
        ESP_LOGI(TAG, "Step 4: Re-enabling LTE");
        if (s_lte_control_cb) {
            esp_err_t lte_enable_result = s_lte_control_cb(true);
            if (lte_enable_result != ESP_OK) {
                ESP_LOGW(TAG, "Failed to re-enable LTE: %s", esp_err_to_name(lte_enable_result));
                // Don't override the main result if GNSS succeeded
                if (result == ESP_OK) {
                    result = lte_enable_result;
                }
            } else {
                ESP_LOGI(TAG, "LTE re-enabled successfully");
            }
        } else {
            ESP_LOGW(TAG, "No LTE control callback, LTE remains disabled");
            ESP_LOGW(TAG, "LTE will need to be manually re-enabled by the application");
        }
    }
    
    ESP_LOGI(TAG, "=== GNSS fix cycle complete: %s ===", 
             result == ESP_OK ? "SUCCESS" : esp_err_to_name(result));
    
    return result;
}
