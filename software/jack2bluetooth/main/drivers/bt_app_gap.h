#pragma once

#include <stdint.h>
#include "esp_err.h"

// Generic Access Profile

typedef void (*bt_app_gap_device_found_cb_t)(const uint8_t *address);

void bt_app_gap_start(void);

esp_err_t bt_app_gap_start_discovery(bt_app_gap_device_found_cb_t cb_ptr);
