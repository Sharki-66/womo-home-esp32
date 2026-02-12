/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "modem_at_parser.h"
#include "../priv_include/modem_at_internal.h"
#include "at_3gpp_ts_27_007.h"

static const char *TAG = "at_3gpp_ts_27_007";

/**
* @brief Strip the tailed "\r\n"
*
* @param str string to strip
* @param len length of string
*/
static inline void strip_cr_lf_tail(char *str, uint32_t len)
{
    if (str[len - 2] == '\r') {
        str[len - 2] = '\0';
    } else if (str[len - 1] == '\r') {
        str[len - 1] = '\0';
    }
}

static bool at_response_ok(at_handle_t at_handle, const char *line)
{
    esp_err_t *ret = (esp_err_t *)modem_at_get_handle_line_ctx(at_handle);
    if (strstr(line, AT_RESULT_CODE_SUCCESS)) {
        *ret = ESP_OK;
        return true;
    } else if (strstr(line, AT_RESULT_CODE_ERROR)) {
        ESP_LOGE(TAG, "AT command \"%s\" ERROR", modem_at_get_current_cmd(at_handle));
        *ret = ESP_FAIL;
        return true;
    }
    return false;
}

esp_err_t at_send_command_response_ok(at_handle_t at_handle, const char *command)
{
    esp_err_t ret = ESP_ERR_TIMEOUT;
    modem_at_send_command(at_handle, command, CONFIG_MODEM_COMMAND_TIMEOUT_DEFAULT, at_response_ok, &ret);
    return ret;
}

static bool _handle_string(at_handle_t at_handle, const char *line)
{
    if (strstr(line, AT_RESULT_CODE_ERROR)) {
        return true;
    }

    at_common_string_t *result_str = modem_at_get_handle_line_ctx(at_handle);
    if (result_str->command != NULL && result_str->string == NULL && result_str->len == 0) {
        ESP_LOGE(TAG, "Invalid parameter of command %s", result_str->command);
    }
    const char *start = line;
    if (memcmp(start, "\r\n", 2) == 0) { //skip "\r\n" in the beginning
        start += 2;
    }
    const char *end = strstr(line, AT_RESULT_CODE_SUCCESS);
    if (end) {
        size_t response_len = strlen(AT_RESULT_CODE_SUCCESS);
        size_t available_len = end - start;
        if (available_len < response_len) {
            ESP_LOGE(TAG, "Invalid response format");
            return false;
        }
        size_t len = available_len - response_len;
        if (len > result_str->len - 1) {
            len = result_str->len - 1;
            ESP_LOGE(TAG, "The length of the result is too long, truncated to %d", len);
        }
        memcpy(result_str->string, start, len);
        result_str->string[len] = '\0';
        strip_cr_lf_tail(result_str->string, len);
    }

    if (strstr(line, AT_RESULT_CODE_SUCCESS)) {
        return true;
    }
    return false;
}

static esp_err_t at_get_common_string(at_handle_t at_handle, void *ctx)
{
    at_common_string_t *param_str = ctx;
    return modem_at_send_command(at_handle, param_str->command, CONFIG_MODEM_COMMAND_TIMEOUT_DEFAULT, _handle_string, ctx);
}

// Send a simple AT command to the modem
esp_err_t at_cmd_at(at_handle_t at_handle)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, "AT");
}

// Set the modem echo
esp_err_t at_cmd_set_echo(at_handle_t at_handle, bool echo_on)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, echo_on ? "ATE1" : "ATE0");
}

// Request manufacturer identification
esp_err_t at_cmd_get_manufacturer_id(at_handle_t at_handle, char *res_buffer, size_t res_buffer_len)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(res_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid parameter");
    ESP_RETURN_ON_FALSE(res_buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid length");
    at_common_string_t common_str = { .command = "AT+CGMI", .string = res_buffer, .len = res_buffer_len};
    return at_get_common_string(at_handle, &common_str);
}

// Request model identification
esp_err_t at_cmd_get_module_id(at_handle_t at_handle, char *res_buffer, size_t res_buffer_len)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(res_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid parameter");
    ESP_RETURN_ON_FALSE(res_buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid length");
    at_common_string_t common_str = { .command = "AT+CGMM", .string = res_buffer, .len = res_buffer_len};
    return at_get_common_string(at_handle, &common_str);
}

// Request revision identification
esp_err_t at_cmd_get_revision_id(at_handle_t at_handle, char *res_buffer, size_t res_buffer_len)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(res_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid parameter");
    ESP_RETURN_ON_FALSE(res_buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid length");
    at_common_string_t common_str = { .command = "AT+CGMR", .string = res_buffer, .len = res_buffer_len};
    return at_get_common_string(at_handle, &common_str);
}

// Request product serial number identification
esp_err_t at_cmd_get_imei_number(at_handle_t at_handle, char *res_buffer, size_t res_buffer_len)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(res_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid parameter");
    ESP_RETURN_ON_FALSE(res_buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid length");
    at_common_string_t common_str = { .command = "AT+CGSN", .string = res_buffer, .len = res_buffer_len};
    return at_get_common_string(at_handle, &common_str);
}

// Request international mobile subscriber identity
esp_err_t at_cmd_get_imsi_number(at_handle_t at_handle, char *res_buffer, size_t res_buffer_len)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(res_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid parameter");
    ESP_RETURN_ON_FALSE(res_buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid length");
    at_common_string_t common_str = { .command = "AT+CIMI", .string = res_buffer, .len = res_buffer_len};
    return at_get_common_string(at_handle, &common_str);
}

esp_err_t at_cmd_get_operator_name(at_handle_t at_handle, char *res_buffer, size_t res_buffer_len)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(res_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid parameter");
    ESP_RETURN_ON_FALSE(res_buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid length");
    at_common_string_t common_str = { .command = "AT+COPS?", .string = res_buffer, .len = res_buffer_len};
    return at_get_common_string(at_handle, &common_str);
}

static bool _handle_CEREG(at_handle_t at_handle, const char *line)
{
    // +CEREG: <n>,<stat>
    esp_modem_at_cereg_t *result = modem_at_get_handle_line_ctx(at_handle);
    line = strstr(line, "+CEREG:");
    if (line) {
        int nc = sscanf(line, "%*s%d,%d", &result->n, &result->stat);
        if (strstr(line, AT_RESULT_CODE_SUCCESS) && nc == 2) {
            return true;
        }
    }
    return false;
}

esp_err_t at_cmd_get_network_reg_status(at_handle_t at_handle, esp_modem_at_cereg_t *result)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(result != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid result");
    return modem_at_send_command(at_handle, "AT+CEREG?", MODEM_COMMAND_TIMEOUT_DEFAULT, _handle_CEREG, result);
}

static bool _handle_read_pin(at_handle_t at_handle, const char *line)
{
    esp_modem_pin_state_t *pin = (esp_modem_pin_state_t *)modem_at_get_handle_line_ctx(at_handle);
    if (strstr(line, AT_RESULT_CODE_ERROR)) {
        return true; // parse successfully, received ERROR code, just ignore it
    }
    if (strstr(line, "READY")) {
        *pin = PIN_READY;
    } else if (strstr(line, "SIM PIN")) {
        *pin = PIN_SIM_PIN;
    } else if (strstr(line, "SIM PUK")) {
        *pin = PIN_SIM_PUK;
    } else if (strstr(line, "SIM PIN2")) {
        *pin = PIN_SIM_PIN2;
    } else if (strstr(line, "SIM PUK2")) {
        *pin = PIN_SIM_PUK2;
    } else {
        *pin = PIN_UNKNOWN;
    }

    if (strstr(line, AT_RESULT_CODE_SUCCESS)) {
        return true;
    }
    return false;
}

esp_err_t at_cmd_read_pin(at_handle_t at_handle, esp_modem_pin_state_t *result)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(result != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid result");
    return modem_at_send_command(at_handle,  "AT+CPIN?", CONFIG_MODEM_COMMAND_TIMEOUT_DEFAULT, _handle_read_pin, result);
}

esp_err_t at_cmd_set_pin(at_handle_t at_handle, const char *pin)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(pin != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid parameter");
    ESP_RETURN_ON_FALSE(strlen(pin) == 4, ESP_ERR_INVALID_ARG, TAG, "PIN must be 4 digits");
    char command[] = "AT+CPIN=0000";
    memcpy(command + 8, pin, 4); // copy 4 bytes to the "0000" placeholder
    esp_err_t err = at_send_command_response_ok(at_handle,  command);
    return err;
}

esp_err_t at_cmd_store_profile(at_handle_t at_handle)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, "AT&W");
}

esp_err_t at_cmd_set_pdp_context(at_handle_t at_handle, const esp_modem_at_pdp_t *pdp)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(pdp != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid param");
    char *command;
    int len = asprintf(&command, "AT+CGDCONT=%d,\"%s\",\"%s\"", pdp->cid, pdp->type, pdp->apn);
    if (len <= 0) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = at_send_command_response_ok(at_handle, command);
    free(command);
    return err;
}

esp_err_t at_cmd_get_pdp_context(at_handle_t at_handle, char *res_buffer, size_t res_buffer_len)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(res_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid result");
    ESP_RETURN_ON_FALSE(res_buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid length");
    at_common_string_t common_str = { .command = "AT+CGDCONT?", .string = res_buffer, .len = res_buffer_len };
    return at_get_common_string(at_handle, &common_str);
}

/**
 * @brief Handle response from AT+CSQ
 */
static bool _handle_csq(at_handle_t at_handle, const char *line)
{
    if (strstr(line, AT_RESULT_CODE_ERROR)) {
        return true;
    }

    if (strstr(line, "+CSQ")) {
        /* store value of rssi and ber */
        esp_modem_at_csq_t *csq = modem_at_get_handle_line_ctx(at_handle);
        /* +CSQ: <rssi>,<ber> */
        sscanf(strstr(line, "+CSQ"), "%*[^0-9]%d,%d", &csq->rssi, &csq->ber);
    }

    if (strstr(line, AT_RESULT_CODE_SUCCESS)) {
        return true;
    }
    return false;
}

esp_err_t at_cmd_get_signal_quality(at_handle_t at_handle, esp_modem_at_csq_t *ret_csq)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(ret_csq != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid pointer ret_csq");
    return modem_at_send_command(at_handle, "AT+CSQ", CONFIG_MODEM_COMMAND_TIMEOUT_DEFAULT, _handle_csq, ret_csq);
}

/**
 * @brief Handle response from entry of the PPP mode
 */
static bool _handle_atd_ppp(at_handle_t at_handle, const char *line)
{
    int *res = (int *)modem_at_get_handle_line_ctx(at_handle);
    *res = 0;
    bool ret = true;
    if (strstr(line, AT_RESULT_CODE_CONNECT)) {
        *res = 1;
    } else if (strstr(line, AT_RESULT_CODE_ERROR)) {
    } else if (strstr(line, AT_RESULT_CODE_NO_CARRIER)) {
    } else {
        ret = false;
    }
    return ret;
}

esp_err_t at_cmd_make_call(at_handle_t at_handle)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    int res = 0;
    modem_at_send_command(at_handle, "ATD*99***1#", MODEM_COMMAND_TIMEOUT_MODE_CHANGE, _handle_atd_ppp, (void *)&res);
    return (res == 1) ? ESP_OK : ESP_FAIL;
}

esp_err_t at_cmd_hang_up_call(at_handle_t at_handle)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, "ATH");
}

esp_err_t at_cmd_resume_data_mode(at_handle_t at_handle)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    int res = 0;
    modem_at_send_command(at_handle, "ATO", MODEM_COMMAND_TIMEOUT_MODE_CHANGE, _handle_atd_ppp, (void *)&res);
    return (res == 1) ? ESP_OK : ESP_FAIL;
}

esp_err_t at_cmd_exit_data_mode(at_handle_t at_handle)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, "+++");
}

static time_t gnss_parse_utc_epoch(const char *utc_str)
{
    if (utc_str == NULL || strlen(utc_str) < 14) {
        return 0;
    }

    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf(utc_str, "%4d%2d%2d%2d%2d%2d", &year, &mon, &day, &hour, &min, &sec) != 6) {
        return 0;
    }

    struct tm tm = {
        .tm_year = year - 1900,
        .tm_mon = mon - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = min,
        .tm_sec = sec,
    };

    return mktime(&tm); // mktime reicht hier; Zeitzonen-Offset egal für relative Anzeige
}

static bool _handle_cgnsinf(at_handle_t at_handle, const char *line)
{
    if (strstr(line, AT_RESULT_CODE_ERROR)) {
        return true;
    }

    esp_modem_at_gnss_info_t *info = modem_at_get_handle_line_ctx(at_handle);
    const char *start = strstr(line, "+CGNSINF");
    if (start != NULL) {
        int run_status = 0;
        int fix_status = 0;
        char utc[32] = {0};
        double lat = 0;
        double lon = 0;
        double alt = 0;
        double speed = 0;
        double course = 0;
        double hdop = 0;
        int sats_view = 0;
        int sats_use = 0;

        // SIMCOM +CGNSINF: <run_status>,<fix_status>,<utc>,<lat>,<lon>,<alt>,<speed>,<course>,<mode>,<hdop>,...
        int matched = sscanf(start,
                             "+CGNSINF: %d,%d,%31[^,],%lf,%lf,%lf,%lf,%lf,%*[^,],%lf,%*[^,],%*[^,],%*[^,],%d,%d",
                             &run_status, &fix_status, utc, &lat, &lon, &alt, &speed, &course, &hdop, &sats_view, &sats_use);
        if (matched >= 9) {
            memset(info, 0, sizeof(*info));
            info->run_status = run_status;
            info->fix_status = fix_status;
            info->powered = (run_status == 1);
            info->fix_valid = (fix_status > 0);
            info->latitude = lat;
            info->longitude = lon;
            info->altitude_m = alt;
            info->speed_kph = speed;
            info->course_deg = course;
            info->hdop = hdop;
            info->sats_in_view = sats_view;
            info->sats_in_use = sats_use;
            info->utc_time = gnss_parse_utc_epoch(utc);
        }
    }

    if (strstr(line, AT_RESULT_CODE_SUCCESS)) {
        return true;
    }
    return false;
}

esp_err_t at_cmd_gnss_power(at_handle_t at_handle, bool enable)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, enable ? "AT+CGNSPWR=1" : "AT+CGNSPWR=0");
}

esp_err_t at_cmd_cgnss_power(at_handle_t at_handle, bool enable)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, enable ? "AT+CGNSSPWR=1" : "AT+CGNSSPWR=0");
}

esp_err_t at_cmd_cgnss_mode(at_handle_t at_handle, int mode)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CGNSSMODE=%d", mode);
    return at_send_command_response_ok(at_handle, cmd);
}

esp_err_t at_cmd_gnss_get_info(at_handle_t at_handle, esp_modem_at_gnss_info_t *info)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(info != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid info pointer");
    return modem_at_send_command(at_handle, "AT+CGNSINF", CONFIG_MODEM_COMMAND_TIMEOUT_DEFAULT, _handle_cgnsinf, info);
}

// ----------------- CGPS (Fallback) -----------------
static double _cgps_dm_to_deg(double dm)
{
    int deg = (int)(dm / 100);
    double minutes = dm - (deg * 100);
    return deg + minutes / 60.0;
}

static bool _handle_cgpsinfo(at_handle_t at_handle, const char *line)
{
    if (strstr(line, AT_RESULT_CODE_ERROR)) {
        return true;
    }

    esp_modem_at_gnss_info_t *info = modem_at_get_handle_line_ctx(at_handle);
    const char *start = strstr(line, "+CGPSINFO");
    if (start != NULL) {
        char lat_ns = 0, lon_ew = 0;
        double lat_dm = 0, lon_dm = 0;
        char date[16] = {0};
        char utc[16] = {0};
        double alt = 0, speed_knots = 0, course = 0;
        int matched = sscanf(start, "+CGPSINFO: %lf,%c,%lf,%c,%15[^,],%15[^,],%lf,%lf,%lf",
                             &lat_dm, &lat_ns, &lon_dm, &lon_ew, date, utc, &alt, &speed_knots, &course);
        if (matched >= 6 && lat_dm > 0 && lon_dm > 0) {
            memset(info, 0, sizeof(*info));
            double lat = _cgps_dm_to_deg(lat_dm);
            double lon = _cgps_dm_to_deg(lon_dm);
            if (lat_ns == 'S') lat = -lat;
            if (lon_ew == 'W') lon = -lon;
            info->latitude = lat;
            info->longitude = lon;
            info->altitude_m = alt;
            info->speed_kph = speed_knots * 1.852; // knots → km/h
            info->course_deg = course;
            info->fix_valid = true; // CGPSINFO liefert nur bei Fix Werte
            info->run_status = 1;
            info->fix_status = 1;
            // Datum/Zeit zu epoch wandeln
            if (strlen(date) >= 6 && strlen(utc) >= 6) {
                int dd = 0, mm = 0, yy = 0, hh = 0, mi = 0;
                double ss = 0;
                sscanf(date, "%2d%2d%2d", &dd, &mm, &yy);
                sscanf(utc, "%2d%2d%lf", &hh, &mi, &ss);
                int sec = (int)ss;
                struct tm tm = {
                    .tm_mday = dd,
                    .tm_mon = mm - 1,
                    .tm_year = 2000 + yy - 1900,
                    .tm_hour = hh,
                    .tm_min = mi,
                    .tm_sec = sec,
                };
                info->utc_time = mktime(&tm);
            }
        }
    }

    if (strstr(line, AT_RESULT_CODE_SUCCESS)) {
        return true;
    }
    return false;
}

static bool _handle_cgnssinfo(at_handle_t at_handle, const char *line)
{
    if (strstr(line, AT_RESULT_CODE_ERROR)) {
        return true;
    }

    esp_modem_at_gnss_info_t *info = modem_at_get_handle_line_ctx(at_handle);
    const char *start = strstr(line, "+CGNSSINFO");
    if (start != NULL) {
        char buf[256];
        strlcpy(buf, start, sizeof(buf));
        char *p = strchr(buf, ':');
        if (p && *(p + 1) == ' ') {
            p += 2; // hinter ": "
            // Format lt. Handbuch: mode, GPS-SVs, GLONASS-SVs, BEIDOU-SVs, lat, N/S, lon, E/W, date, UTC, alt(m), speed(knots), course, PDOP, HDOP, VDOP
            char *saveptr = NULL;
            int field = 0;
            double speed_knots = 0;
            int gps_sv = 0, glo_sv = 0, bei_sv = 0;
            char lat_ns = 0, lon_ew = 0;
            for (char *tok = strtok_r(p, ",", &saveptr); tok; tok = strtok_r(NULL, ",", &saveptr)) {
                switch (field) {
                case 0: info->fix_status = atoi(tok); info->fix_valid = (info->fix_status >= 2); info->run_status = info->fix_valid ? 1 : 0; break;
                case 1: gps_sv = atoi(tok); break;
                case 2: glo_sv = atoi(tok); break;
                case 3: bei_sv = atoi(tok); break;
                case 4: info->latitude = atof(tok); break;
                case 5: lat_ns = tok[0]; break;
                case 6: info->longitude = atof(tok); break;
                case 7: lon_ew = tok[0]; break;
                case 8: {
                    // date ddmmyy
                    int dd=0,mm=0,yy=0; sscanf(tok, "%2d%2d%2d", &dd, &mm, &yy);
                    struct tm tm = {.tm_mday=dd, .tm_mon=mm-1, .tm_year=2000+yy-1900};
                    info->utc_time = mktime(&tm); // später durch Zeit ergänzt
                    break; }
                case 9: {
                    int hh=0, mi=0; double ss=0;
                    sscanf(tok, "%2d%2d%lf", &hh, &mi, &ss);
                    if (info->utc_time > 0) {
                        struct tm tm = *gmtime(&info->utc_time);
                        tm.tm_hour = hh; tm.tm_min = mi; tm.tm_sec = (int)ss;
                        info->utc_time = mktime(&tm);
                    }
                    break; }
                case 10: info->altitude_m = atof(tok); break;
                case 11: speed_knots = atof(tok); break;
                case 12: info->course_deg = atof(tok); break;
                case 14: info->hdop = atof(tok); break;
                case 15: /* vdop */ break;
                default: break;
                }
                field++;
            }
            if (lat_ns == 'S') info->latitude = -info->latitude;
            if (lon_ew == 'W') info->longitude = -info->longitude;
            info->speed_kph = speed_knots * 1.852;
            info->sats_in_view = gps_sv + glo_sv + bei_sv;
            info->sats_in_use = info->sats_in_view; // keine separate Zahl, best effort
        }
    }

    if (strstr(line, AT_RESULT_CODE_SUCCESS)) {
        return true;
    }
    return false;
}

esp_err_t at_cmd_cgps_power(at_handle_t at_handle, bool enable)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    return at_send_command_response_ok(at_handle, enable ? "AT+CGPS=1,1" : "AT+CGPS=0");
}

esp_err_t at_cmd_cgps_info(at_handle_t at_handle, esp_modem_at_gnss_info_t *info)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(info != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid info pointer");
    return modem_at_send_command(at_handle, "AT+CGPSINFO", CONFIG_MODEM_COMMAND_TIMEOUT_DEFAULT, _handle_cgpsinfo, info);
}

esp_err_t at_cmd_cgnss_info(at_handle_t at_handle, esp_modem_at_gnss_info_t *info)
{
    ESP_RETURN_ON_FALSE(at_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(info != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid info pointer");
    return modem_at_send_command(at_handle, "AT+CGNSSINFO", CONFIG_MODEM_COMMAND_TIMEOUT_DEFAULT, _handle_cgnssinfo, info);
}
