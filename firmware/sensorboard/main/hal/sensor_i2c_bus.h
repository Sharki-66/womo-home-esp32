/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <i2c_bus.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_bus_init(void);
i2c_bus_handle_t i2c_bus_get(void);
i2c_master_bus_handle_t i2c_bus_get_internal(void);
void i2c_bus_lock(void);
void i2c_bus_unlock(void);

#ifdef __cplusplus
}
#endif
