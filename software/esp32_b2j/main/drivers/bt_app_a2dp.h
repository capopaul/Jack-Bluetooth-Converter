// Author : Paul Capgras
// Date   : Oct 10, 2025

#include <stdint.h>
#include "esp_a2dp_api.h"

/* log tags */
#define BT_APP_A2DP_TAG "BT_APP_A2DP"

// | pin name       | esp32 |
// | codec_i2s_mclk | TXD0  | (it was supposed to be IO0... PCB error...)
// | codec_i2s_bclk | IO2   |
// | codec_i2s_wclk | IO5   |
// | codec_i2s_din  | IO4   | So IO18 for esp32
// | codec_i2s_dout | IO18  | So IO4  for esp32
#define I2S_GPIO_MCLK I2S_GPIO_UNUSED
#define I2S_GPIO_BCLK GPIO_NUM_2
#define I2S_GPIO_WCLK GPIO_NUM_5
#define I2S_GPIO_DIN GPIO_NUM_18
#define I2S_GPIO_DOUT GPIO_NUM_4

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/
void esp_i2s_driver_install(void);
void bt_app_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
void bt_app_a2dp_data_cb(const uint8_t *data, uint32_t len);
