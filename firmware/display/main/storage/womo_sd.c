/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * WoMo SD Card Manager - Implementation
 */

#include "womo_sd.h"
#include "hardware/waveshare_rgb_lcd_port.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include <sys/stat.h>
#include <string.h>

static const char *TAG = "womo_sd";

static void reassert_sd_cs(void)
{
    esp_err_t err = womo_ch422g_assert_sd_cs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CH422G SD-CS reassert failed: %s", esp_err_to_name(err));
    }
}

// SD card handle
static sdmmc_card_t *sd_card = NULL;
static bool sd_mounted = false;
static spi_host_device_t spi_host = SPI2_HOST;  // Store SPI host for cleanup

// Pin definitions for Waveshare ESP32-S3 Touch LCD 7"
// SD Card uses SPI with I2C GPIO expander for CS control
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_CLK   12
#define PIN_CS    SDSPI_SLOT_NO_CS  // CS controlled via I2C GPIO expander

esp_err_t womo_sd_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card");
    
    if (sd_mounted) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }
    
    esp_err_t ret;

    // Control CH422G to pull down the CS pin of the SD card (EXIO4) using the shared I2C bus
    ret = womo_ch422g_assert_sd_cs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Proceeding without CH422G SD-CS assert (may cause SD I/O errors): %s", esp_err_to_name(ret));
    }
    
    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // Use SPI mode (reduce clock to improve signal stability)
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 5000;      // extra margin for signal integrity
    host.command_timeout_ms = 1000; // pro Kommando max 1 s → Worst-Case-Mount ~5 s statt 30 s
    spi_host = host.slot;          // Store for cleanup in deinit
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* Ohne DMA darf das Host-Limit nicht zu klein sein, sonst scheitern schon
         * die fruehen Mount-Transfers mit "txdata transfer > host maximum".
         * 16 KiB ist fuer FAT-Mount/Directory-Zugriffe klein genug und deutlich
         * robuster als 4096. */
        .max_transfer_sz = 16 * 1024,
    };

    /* SD ist hier nur fuer Icons, Logo, Wetterbilder und Screenshots da.
     * Der SD-SPI-Treiber benötigt DMA, damit große Mount-/Filesystem-Transfers
     * nicht mit "txdata transfer > host maximum" abbrechen. */
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Attach the SD card to the SPI bus
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_CS;  // SDSPI_SLOT_NO_CS: CS controlled via I2C expander
    slot_config.host_id = host.slot;
    
    ret = esp_vfs_fat_sdspi_mount(WOMO_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &sd_card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Check SD card or format as FAT32.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        spi_bus_free(host.slot);
        return ret;
    }
    
    sd_mounted = true;
    
    // Print card info
    sdmmc_card_print_info(stdout, sd_card);
    ESP_LOGI(TAG, "SD card mounted at %s", WOMO_SD_MOUNT_POINT);
    
    return ESP_OK;
}

esp_err_t womo_sd_deinit(void)
{
    if (!sd_mounted) {
        ESP_LOGW(TAG, "SD card not mounted");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Unmounting SD card");
    
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(WOMO_SD_MOUNT_POINT, sd_card);
    if (ret == ESP_OK) {
        sd_mounted = false;
        sd_card = NULL;
        ESP_LOGI(TAG, "SD card unmounted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
    }
    
    // Free SPI bus
    spi_bus_free(spi_host);
    
    return ret;
}

bool womo_sd_is_mounted(void)
{
    return sd_mounted;
}

bool womo_sd_file_exists(const char *path)
{
    if (!sd_mounted) {
        return false;
    }

    // Make sure SD-CS stays asserted on the expander before accessing
    reassert_sd_cs();
    
    char full_path[256];
    if (womo_sd_get_full_path(path, full_path, sizeof(full_path)) != ESP_OK) {
        return false;
    }
    
    struct stat st;
    return (stat(full_path, &st) == 0);
}

esp_err_t womo_sd_get_full_path(const char *relative_path, char *full_path, size_t max_len)
{
    if (relative_path == NULL || full_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Remove leading slash if present
    const char *path = relative_path;
    if (path[0] == '/') {
        path++;
    }
    
    int len = snprintf(full_path, max_len, "%s/%s", WOMO_SD_MOUNT_POINT, path);
    
    if (len < 0 || len >= max_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    return ESP_OK;
}

esp_err_t womo_sd_get_size(uint64_t *total_mb, uint64_t *used_mb)
{
    if (!sd_mounted || sd_card == NULL) {
        return ESP_FAIL;
    }
    
    if (total_mb != NULL) {
        *total_mb = ((uint64_t)sd_card->csd.capacity) * sd_card->csd.sector_size / (1024 * 1024);
    }
    
    // Used space calculation would require filesystem stats
    // For now, set to 0
    if (used_mb != NULL) {
        *used_mb = 0;
    }
    
    return ESP_OK;
}
