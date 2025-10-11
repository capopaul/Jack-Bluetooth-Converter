// Author : Paul Capgras
// Date   : Oct 10, 2025

#include <stdint.h>
#include "esp_a2dp_api.h"

/* log tags */
#define BT_APP_A2DP_TAG "BT_APP_A2DP"

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/
void esp_i2s_driver_install(void);
void bt_app_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
void bt_app_a2dp_data_cb(const uint8_t *data, uint32_t len);
