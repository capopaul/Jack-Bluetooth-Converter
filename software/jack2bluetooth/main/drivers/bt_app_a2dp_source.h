#ifndef BT_APP_A2DP_SOURCE_H
#define BT_APP_A2DP_SOURCE_H

#include "esp_err.h"

/**
 * @brief Start the A2DP source application after Bluedroid is enabled.
 *
 * This starts the Bluetooth application task and schedules initialization of
 * GAP discovery, AVRCP, A2DP, stream endpoints, and the media state machine.
 */
esp_err_t bt_app_a2dp_source_start(void);

#endif /* BT_APP_A2DP_SOURCE_H */
