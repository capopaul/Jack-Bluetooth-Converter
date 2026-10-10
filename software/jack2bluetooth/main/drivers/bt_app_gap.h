#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Generic Access Profile

/* device name */
#define LOCAL_DEVICE_NAME "Jack2Bluetooth"
#define BT_APP_GAP_MAX_DISCOVERED_DEVICES 12
#define BT_APP_GAP_DEVICE_NAME_MAX_LEN 64

typedef struct
{
    uint8_t address[6];
    char name[BT_APP_GAP_DEVICE_NAME_MAX_LEN];
    int8_t rssi;
    uint32_t class_of_device;
} bt_app_gap_discovered_device_t;

typedef void (*bt_app_gap_device_found_cb_t)(const uint8_t *address);
typedef void (*bt_app_gap_remote_name_cb_t)(const uint8_t *address, const char *name);
typedef void (*bt_app_gap_device_list_changed_cb_t)(void);

void bt_app_gap_start(void);

esp_err_t bt_app_gap_start_discovery(bt_app_gap_device_found_cb_t cb_ptr);

// Read-only access to the latest inquiry results. Copy a device record before
// using it because discovery can update the model asynchronously.
size_t bt_app_gap_get_discovered_device_count(void);
esp_err_t bt_app_gap_get_discovered_device(size_t index,
                                           bt_app_gap_discovered_device_t *device);
void bt_app_gap_set_device_list_changed_callback(bt_app_gap_device_list_changed_cb_t callback);

// Read a connected peer's Bluetooth name. The callback is called with NULL
// when the peer does not provide a name or the lookup fails.
esp_err_t bt_app_gap_read_remote_name(const uint8_t *address,
                                      bt_app_gap_remote_name_cb_t callback);
