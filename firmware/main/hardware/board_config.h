#pragma once

#include "sdkconfig.h"
#include "driver/i2c.h"

// Unified board-level I2C mapping so we can retarget pins for different ESP32-S3 boards
// (e.g., Waveshare ESP32-S3-A7670E-4G without camera).
// Defaults keep the original Waveshare AMOLED 7" wiring but can be changed in menuconfig.
#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SCL  CONFIG_WOMO_I2C_SCL
#define BOARD_I2C_SDA  CONFIG_WOMO_I2C_SDA

