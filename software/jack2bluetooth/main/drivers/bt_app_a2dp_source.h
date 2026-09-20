#pragma once

#include "esp_err.h"

/* A2DP global states */
typedef enum
{
    APP_AV_STATE_IDLE,
    APP_AV_STATE_DISCOVERING,
    APP_AV_STATE_DISCOVERED,
    APP_AV_STATE_UNCONNECTED,
    APP_AV_STATE_CONNECTING,
    APP_AV_STATE_CONNECTED,
    APP_AV_STATE_DISCONNECTING,
} bt_app_a2dp_state_t;

/* sub states of APP_AV_STATE_CONNECTED */
enum
{
    APP_AV_MEDIA_STATE_IDLE,
    APP_AV_MEDIA_STATE_STARTING,
    APP_AV_MEDIA_STATE_STARTED,
    APP_AV_MEDIA_STATE_STOPPING,
};

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

/**
 * @brief Start the A2DP source application after Bluedroid is enabled.
 *
 * This starts the Bluetooth application task and schedules initialization of
 * GAP discovery, AVRCP, A2DP, stream endpoints, and the media state machine.
 */
esp_err_t bt_app_a2dp_source_start(void);

void bt_app_a2dp_source_connect(const uint8_t *address);

bt_app_a2dp_state_t bt_app_a2dp_source_get_state();

void bt_app_a2dp_source_set_state(bt_app_a2dp_state_t new_state);
