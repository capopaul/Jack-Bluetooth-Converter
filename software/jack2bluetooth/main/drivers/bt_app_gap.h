#pragma once

#include <stdint.h>
#include "esp_err.h"

// Generic Access Profile

/* device name */
#define LOCAL_DEVICE_NAME "Jack2Bluetooth"

typedef void (*bt_app_gap_device_found_cb_t)(const uint8_t *address);
typedef void (*bt_app_gap_remote_name_cb_t)(const uint8_t *address, const char *name);

void bt_app_gap_start(void);

esp_err_t bt_app_gap_start_discovery(bt_app_gap_device_found_cb_t cb_ptr);

// Read a connected peer's Bluetooth name. The callback is called with NULL
// when the peer does not provide a name or the lookup fails.
esp_err_t bt_app_gap_read_remote_name(const uint8_t *address,
                                      bt_app_gap_remote_name_cb_t callback);
