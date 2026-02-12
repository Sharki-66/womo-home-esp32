#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert RS485-Verbindung und startet TX/RX-Tasks
esp_err_t rs485_modem_init(void);

#ifdef __cplusplus
}
#endif
