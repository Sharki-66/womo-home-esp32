/*
 * WoMo SD Card Manager
 * 
 * Manages SD card access for:
 * - Loading background images
 * - Storing configuration files
 * - Logging data
 */

#ifndef WOMO_SD_H
#define WOMO_SD_H

#include "esp_err.h"
#include <stdbool.h>

// SD Card mount point
#define WOMO_SD_MOUNT_POINT "/sdcard"

/**
 * @brief Initialize and mount SD card
 * 
 * Mounts SD card to /sdcard using SPI interface
 * 
 * @return ESP_OK on success
 */
esp_err_t womo_sd_init(void);

/**
 * @brief Unmount and deinitialize SD card
 * 
 * @return ESP_OK on success
 */
esp_err_t womo_sd_deinit(void);

/**
 * @brief Check if SD card is mounted
 * 
 * @return true if mounted, false otherwise
 */
bool womo_sd_is_mounted(void);

/**
 * @brief Check if file exists on SD card
 * 
 * @param path File path relative to mount point (e.g., "/images/bg.png")
 * @return true if file exists
 */
bool womo_sd_file_exists(const char *path);

/**
 * @brief Get full path for file on SD card
 * 
 * @param relative_path Path relative to mount point
 * @param full_path Buffer to store full path
 * @param max_len Maximum buffer length
 * @return ESP_OK on success
 */
esp_err_t womo_sd_get_full_path(const char *relative_path, char *full_path, size_t max_len);

/**
 * @brief Get SD card size in MB
 * 
 * @param total_mb Total size in MB
 * @param used_mb Used space in MB
 * @return ESP_OK on success
 */
esp_err_t womo_sd_get_size(uint64_t *total_mb, uint64_t *used_mb);

#endif // WOMO_SD_H
