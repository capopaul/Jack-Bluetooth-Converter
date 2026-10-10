#pragma once

#include <stdint.h>

#include "esp_err.h"

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

/**
 * @brief Start the A2DP source application after Bluedroid is enabled.
 *
 * This starts the Bluetooth application task and schedules initialization of
 * GAP discovery, AVRCP, A2DP, stream endpoints, and the media state machine.
 */
void bt_app_a2dp_source_start(void);

// Queue a connection to a device selected by the UI. The connection is made
// from the Bluetooth application task.
esp_err_t bt_app_a2dp_source_connect_selected(const uint8_t *address);
