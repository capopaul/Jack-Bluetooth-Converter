#pragma once

#include <stdint.h>

#include "esp_err.h"

// Starts the Bluetooth-device picker and subscribes it to GAP discovery
// updates. It is intended for Jack -> Bluetooth mode.
esp_err_t ui_device_picker_start(void);

// Move the selected device. These return ESP_ERR_INVALID_STATE while the
// picker is inactive or no device has been discovered yet.
esp_err_t ui_device_picker_next(void);
esp_err_t ui_device_picker_back(void);

// Copy the Bluetooth address of the selected device into address. This does
// not initiate a connection; the connection step will use this in step 3.
esp_err_t ui_device_picker_enter(uint8_t address[6]);

void ui_device_picker_show_connecting(const uint8_t address[6]);
void ui_device_picker_show_connected(const uint8_t address[6]);
void ui_device_picker_show_connection_failed(const uint8_t address[6]);
void ui_device_picker_show_disconnected(const uint8_t address[6]);
