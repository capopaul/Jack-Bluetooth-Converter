
#include "button.h"
#include "audio_codec.h"
#include "bt_app_core.h"
#include "esp_err.h"

#include "decode_task.h"
#include "i2s.h"
#include "bt_app_a2dp_source.h"
// For print
#include "esp_log.h"

#define BUTTON_TAG "BUTTON"

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void on_enter_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button ENTER pushed");
}

void on_back_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button BACK pushed");
}

void on_next_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button NEXT pushed");
}

void on_direction_changed(direction_state new_state)
{
    // check switch status.
    // during this phase, switch must not change state
    if (new_state == J2B)
    {
        // Let's start by deactivating B2J:
        // First let's cut bluetooth communication
        // Second let's cut audio codec

        // bt_app_task_shut_down();

        // audio_codec_sw_reset();
    }
    else
    {
        // new_state == B2J

        // Let's start by deactivating J2B

        // 1: disconnect device properly if connected
        // 1: Or stop discovery
        // 2: Shutdown a2dp_aac task
        // 3: shutdown i2s_rx_task

        encode_task_launch_disable();

        esp_err_t err = encode_task_stop();
        if (err != ESP_OK)
        {
            ESP_LOGE(BUTTON_TAG, "AAC stop failed: %s",
                     esp_err_to_name(err));
            return; // Do not delete I2S resources.
        }

        bt_app_a2dp_source_disconnect();

        i2s_driver_uninstall();

        audio_codec_sw_reset();
    }
}
