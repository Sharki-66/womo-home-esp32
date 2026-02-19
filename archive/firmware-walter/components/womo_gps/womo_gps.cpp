/**
 * @file womo_gps.cpp
 * @brief GPS/GNSS wrapper for Walter Modem
 * 
 * Based on DPTechnics walter-modem positioning.cpp example
 */

#include "womo_gps.h"
#include "WalterModem.h"
#include "walter_config.h"
#include <string.h>
#include <math.h>
#include <atomic>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "womo_gps";

// Global modem instance (extern, defined in main.cpp)
extern WalterModem modem;
extern esp_err_t lte_runtime_set_enabled(bool enable);
extern esp_err_t lte_runtime_set_quiet(bool quiet);
extern std::atomic<bool> s_lte_runtime_active;

// Last received GPS fix
static womo_gps_data_t s_last_fix = {};
static bool s_fix_received = false;
static SemaphoreHandle_t s_gps_mutex = NULL;

// Flag to signal when a fix is received
static volatile bool s_gnss_fix_rcvd = false;
static WalterModemGNSSFix s_latest_gnss_fix = {};

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

static esp_err_t lte_blocking_enable_after_gnss(TickType_t timeout_ticks)
{
    esp_err_t err = lte_runtime_set_enabled(true);
    if (err != ESP_OK) {
        return err;
    }

    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        const bool active = s_lte_runtime_active.load();
        const WalterModemNetworkRegState reg = modem.getNetworkRegState();
        const bool reg_ok = (reg == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
                             reg == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING);

        if (active || reg_ok) {
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGW(TAG,
             "LTE enable timeout after GNSS (reg=%d active=%d)",
             (int)modem.getNetworkRegState(),
             s_lte_runtime_active.load() ? 1 : 0);
    return ESP_ERR_TIMEOUT;
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
 * @brief Check if GNSS clock is valid
 * Based on validateGNSSClock() from positioning.cpp
 */
static bool ensure_modem_full_state(void)
{
    WalterModemRsp rsp = {};
    if (modem.getOpState(&rsp) && rsp.type == WALTER_MODEM_RSP_DATA_TYPE_OPSTATE) {
        if (rsp.data.opState == WALTER_MODEM_OPSTATE_FULL) {
            return true;
        }
        ESP_LOGI(TAG,
                 "Modem opstate=%d before clock sync, requesting FULL",
                 (int)rsp.data.opState);
    } else {
        ESP_LOGW(TAG, "Unable to read modem opstate before clock sync (continuing)");
    }

    if (!modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
        ESP_LOGE(TAG, "setOpState(FULL) failed while preparing time sync");
        return false;
    }
    return true;
}

static bool sync_gnss_clock_via_lte(void)
{
    ESP_LOGW(TAG, "Attempting to sync GNSS clock via LTE");

    if (!ensure_modem_full_state()) {
        return false;
    }

    // Wait until LTE is registered so the modem can provide network time
    const TickType_t registration_timeout = pdMS_TO_TICKS(30000);
    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < registration_timeout) {
        WalterModemNetworkRegState reg = modem.getNetworkRegState();
        if (reg == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
            reg == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    WalterModemNetworkRegState reg = modem.getNetworkRegState();
    if (reg != WALTER_MODEM_NETWORK_REG_REGISTERED_HOME &&
        reg != WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
        ESP_LOGE(TAG, "LTE not registered -> cannot fetch network time (state=%d)", (int)reg);
        return false;
    }

    WalterModemRsp clock_rsp = {};
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (modem.getClock(&clock_rsp) &&
            clock_rsp.type == WALTER_MODEM_RSP_DATA_TYPE_CLOCK &&
            clock_rsp.data.clock.epochTime > 4) {
            int64_t epoch = clock_rsp.data.clock.epochTime;
            ESP_LOGI(TAG, "Received LTE clock epoch=%lld", (long long)epoch);
            if (modem.gnssSetUTCTime(epoch)) {
                ESP_LOGI(TAG, "GNSS UTC synced from LTE clock");
                return true;
            }
            ESP_LOGW(TAG, "Failed to push LTE clock to GNSS (attempt %d)", attempt + 1);
        } else {
            ESP_LOGW(TAG, "LTE clock unavailable (attempt %d)", attempt + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_LOGE(TAG, "Unable to synchronise GNSS clock via LTE");
    return false;
}

static bool validate_gnss_clock(void)
{
    WalterModemRsp rsp = {};
    bool need_sync = false;

    if (!modem.gnssGetUTCTime(&rsp)) {
        ESP_LOGW(TAG, "Could not read GNSS UTC time (will try LTE sync)");
        need_sync = true;
    } else if (rsp.data.clock.epochTime > 4) {
        ESP_LOGI(TAG,
                 "GNSS clock already valid (epoch=%lld)",
                 (long long)rsp.data.clock.epochTime);
        return true;
    } else {
        ESP_LOGW(TAG,
                 "GNSS clock invalid (epoch=%lld) -> sync required",
                 (long long)rsp.data.clock.epochTime);
        need_sync = true;
    }

    if (!need_sync) {
        return true;
    }

    if (!sync_gnss_clock_via_lte()) {
        return false;
    }

    // Re-read to ensure the clock was updated
    if (!modem.gnssGetUTCTime(&rsp)) {
        ESP_LOGE(TAG, "GNSS clock read failed after sync");
        return false;
    }

    if (rsp.data.clock.epochTime > 4) {
        ESP_LOGI(TAG,
                 "GNSS clock synced successfully (epoch=%lld)",
                 (long long)rsp.data.clock.epochTime);
        return true;
    }

    ESP_LOGE(TAG, "GNSS clock still invalid after LTE sync");
    return false;
}

static void log_assistance_details(const char *label,
                                   const WalterModemGNSSAssistanceTypeDetails &info)
{
    ESP_LOGI(TAG,
             "%s assistance: available=%d last=%ds next_update=%ds expiry=%ds",
             label,
             info.available ? 1 : 0,
             (int)info.lastUpdate,
             (int)info.timeToUpdate,
             (int)info.timeToExpire);
}

static bool lte_connect_for_assistance(void)
{
    // An das Beispiel angelehnt: NO_RF -> PDP -> FULL -> automatische Netzwahl -> warten auf Registrierung
    if (!modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
        ESP_LOGE(TAG, "setOpState(NO_RF) for GNSS assistance failed");
        return false;
    }

    const char *apn = (*WALTER_LTE_APN != '\0') ? WALTER_LTE_APN : nullptr;
    if (!modem.definePDPContext(1, apn, nullptr)) {
        ESP_LOGE(TAG, "definePDPContext() for GNSS assistance failed (apn=%s)",
                 apn ? apn : "<null>");
        return false;
    }

    if (!modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
        ESP_LOGE(TAG, "setOpState(FULL) for GNSS assistance failed");
        return false;
    }

    if (!modem.setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
        ESP_LOGE(TAG, "setNetworkSelectionMode(AUTO) failed during GNSS assistance connect");
        return false;
    }

    const TickType_t timeout = pdMS_TO_TICKS(30000);
    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < timeout) {
        WalterModemNetworkRegState reg = modem.getNetworkRegState();
        if (reg == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
            reg == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGE(TAG, "GNSS assistance LTE connect timed out");
    return false;
}

static bool ensure_gnss_assistance(void)
{
    WalterModemRsp rsp = {};
    if (!modem.gnssGetAssistanceStatus(&rsp) ||
        rsp.type != WALTER_MODEM_RSP_DATA_TYPE_GNSS_ASSISTANCE_DATA) {
        ESP_LOGW(TAG, "GNSS assistance status unavailable (result=%d type=%d)",
                 (int)rsp.result,
                 (int)rsp.type);
        return false;
    }

    const WalterModemGNSSAssistance &assist = rsp.data.gnssAssistance;
    log_assistance_details("Almanac", assist.almanac);
    log_assistance_details("Realtime ephemeris", assist.realtimeEphemeris);
    log_assistance_details("Predicted ephemeris", assist.predictedEphemeris);

    auto needs_refresh = [](const WalterModemGNSSAssistanceTypeDetails &info) {
        if (!info.available) {
            return true;
        }
        return info.timeToUpdate <= 0;
    };

    bool require_almanac = needs_refresh(assist.almanac);
    bool require_rt_ephem = needs_refresh(assist.realtimeEphemeris);
    bool require_pred_ephem = needs_refresh(assist.predictedEphemeris);
    bool ok = true;

    // Bei Bedarf LTE verbinden, damit Assistance-Daten geladen werden können
    WalterModemNetworkRegState reg = modem.getNetworkRegState();
    if (reg != WALTER_MODEM_NETWORK_REG_REGISTERED_HOME &&
        reg != WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
        ESP_LOGI(TAG, "Connecting LTE to refresh GNSS assistance");
        if (!lte_connect_for_assistance()) {
            ESP_LOGE(TAG, "Cannot refresh GNSS assistance (LTE connect failed)");
            return false;
        }
    }

    auto update_type = [&](WalterModemGNSSAssistanceType type, const char *label) {
        const int max_attempts = 3;
        for (int attempt = 1; attempt <= max_attempts; ++attempt) {
            ESP_LOGI(TAG, "Updating GNSS assistance: %s (attempt %d/%d)", label, attempt, max_attempts);
            if (modem.gnssUpdateAssistance(type)) {
                return;
            }
            ESP_LOGW(TAG, "Failed to update %s assistance (attempt %d/%d)", label, attempt, max_attempts);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        ESP_LOGE(TAG, "Giving up on %s assistance after retries", label);
        ok = false;
    };

    if (require_rt_ephem) {
        update_type(WALTER_MODEM_GNSS_ASSISTANCE_TYPE_REALTIME_EPHEMERIS, "realtime ephemeris");
    }
    if (require_almanac && ok) {
        update_type(WALTER_MODEM_GNSS_ASSISTANCE_TYPE_ALMANAC, "almanac");
    }
    if (require_pred_ephem && ok) {
        update_type(WALTER_MODEM_GNSS_ASSISTANCE_TYPE_PREDICTED_EPHEMERIS, "predicted ephemeris");
    }

    if (!ok) {
        return false;
    }

    if (require_almanac || require_rt_ephem || require_pred_ephem) {
        if (!modem.gnssGetAssistanceStatus(&rsp) ||
            rsp.type != WALTER_MODEM_RSP_DATA_TYPE_GNSS_ASSISTANCE_DATA) {
            ESP_LOGW(TAG, "GNSS assistance recheck failed after update");
            return false;
        }
        const WalterModemGNSSAssistance &updated = rsp.data.gnssAssistance;
        log_assistance_details("Almanac", updated.almanac);
        log_assistance_details("Realtime ephemeris", updated.realtimeEphemeris);
        log_assistance_details("Predicted ephemeris", updated.predictedEphemeris);
    }

    return true;
}

extern "C" esp_err_t womo_gps_request_fix(void)
{
    ESP_LOGI(TAG, "Requesting GNSS fix");

    // Validate GNSS clock (required for modem to accept fix requests)
    if (!validate_gnss_clock()) {
        ESP_LOGE(TAG, "Aborting GNSS request: unable to sync GNSS clock");
        return ESP_FAIL;
    }

    if (!ensure_gnss_assistance()) {
        ESP_LOGW(TAG, "Proceeding without fresh GNSS assistance data");
    }
    
    // LTE bleibt an (WALTER_GPS_PAUSE_LTE=0), um blockierende CFUN-Umschaltungen zu vermeiden

    // Optional: Try to reconfigure GNSS for potential hot start
    // (This is from the positioning.cpp example)
    if (s_fix_received && s_last_fix.valid) {
        ESP_LOGI(TAG, "Reconfiguring GNSS for hot start (previous fix available)");
        if (!modem.gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH, WALTER_MODEM_GNSS_ACQ_MODE_HOT_START)) {
            ESP_LOGW(TAG, "Could not reconfigure GNSS for hot start");
        }
    }

#if !WALTER_GPS_PAUSE_LTE
    // Funk erst jetzt für den Fix beruhigen, damit Clock/Assist im FULL-State laufen konnten
    WalterModemRsp op_rsp = {};
    bool already_no_rf = false;
    if (modem.getOpState(&op_rsp) && op_rsp.type == WALTER_MODEM_RSP_DATA_TYPE_OPSTATE) {
        already_no_rf = (op_rsp.data.opState == WALTER_MODEM_OPSTATE_NO_RF);
    }

    if (!already_no_rf) {
        if (!modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
            ESP_LOGW(TAG, "setOpState(NO_RF) before GNSS fix failed; continuing with LTE active");
        } else {
            ESP_LOGI(TAG, "setOpState(NO_RF) applied for GNSS fix (PAUSE_LTE=0)");
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    } else {
        ESP_LOGI(TAG, "Opstate already NO_RF before GNSS fix");
    }
#endif

    // Reset flag
    s_gnss_fix_rcvd = false;
    
    // Request a GNSS fix (from positioning.cpp example)
    // Try up to 5 times as in the example
    const int maxAttempts = 3;
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
                WalterModemRsp op_rsp = {};
                WalterModemNetworkRegState reg = modem.getNetworkRegState();
                if (modem.getOpState(&op_rsp) && op_rsp.type == WALTER_MODEM_RSP_DATA_TYPE_OPSTATE) {
                    ESP_LOGW(TAG, "GNSS request context: opstate=%d reg=%d", (int)op_rsp.data.opState, (int)reg);
                } else {
                    ESP_LOGW(TAG, "GNSS request context: opstate=? reg=%d (opstate read failed)", (int)reg);
                }
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

extern "C" esp_err_t womo_gps_run_cycle(uint32_t timeout_ms, womo_gps_data_t *out_fix)
{
    if (!out_fix) {
        return ESP_ERR_INVALID_ARG;
    }

    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    const TickType_t lte_wait_enable = pdMS_TO_TICKS(90000);
    const TickType_t fix_poll_delay = pdMS_TO_TICKS(200);

    // 1) LTE aktivieren für Zeit/Assistenz
    esp_err_t on_err = lte_blocking_enable_after_gnss(lte_wait_enable);
    if (on_err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to enable LTE before GNSS cycle: %s", esp_err_to_name(on_err));
        return on_err;
    }

    // 2) Merke bisherigen Fix-Zeitstempel
    womo_gps_data_t prev_fix = {};
    bool have_prev = (womo_gps_get_last_fix(&prev_fix) == ESP_OK);
    int64_t prev_ts = have_prev ? prev_fix.timestamp : 0;

    // 3) GNSS-Fix anfordern (WALTER_GPS_PAUSE_LTE wird in der Request-Funktion gehandhabt)
    esp_err_t req_err = womo_gps_request_fix();
    if (req_err != ESP_OK) {
        ESP_LOGE(TAG, "GNSS request failed: %s", esp_err_to_name(req_err));
        lte_blocking_enable_after_gnss(lte_wait_enable);
        return req_err;
    }

    // 4) Auf neuen Fix warten
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        womo_gps_data_t fix = {};
        if (womo_gps_get_last_fix(&fix) == ESP_OK) {
            if (!have_prev || fix.timestamp != prev_ts) {
                *out_fix = fix;
                ESP_LOGI(TAG, "GNSS fix received, re-enabling LTE");
                womo_gps_cancel_fix();
                if (lte_blocking_enable_after_gnss(lte_wait_enable) != ESP_OK) {
                    ESP_LOGW(TAG, "LTE enable retry after fix");
                    (void)lte_blocking_enable_after_gnss(lte_wait_enable);
                }
                return ESP_OK;
            }
        }
        vTaskDelay(fix_poll_delay);
    }

    ESP_LOGW(TAG, "GNSS fix timeout after %u ms", (unsigned)timeout_ms);
    womo_gps_cancel_fix();
    if (lte_blocking_enable_after_gnss(lte_wait_enable) != ESP_OK) {
        ESP_LOGW(TAG, "LTE enable retry after timeout");
        (void)lte_blocking_enable_after_gnss(lte_wait_enable);
    }
    return ESP_ERR_TIMEOUT;
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

extern "C" esp_err_t womo_gps_cancel_fix(void)
{
    ESP_LOGI(TAG, "Cancelling GNSS fix request");
    WalterModemRsp rsp = {};
    if (!modem.gnssPerformAction(WALTER_MODEM_GNSS_ACTION_CANCEL, &rsp)) {
        ESP_LOGW(TAG, "Failed to cancel GNSS fix (result=%d)", (int)rsp.result);
        return ESP_FAIL;
    }
    return ESP_OK;
}
