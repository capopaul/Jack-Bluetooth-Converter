
#include "button.h"
#include "audio_codec.h"
#include "pcm_capture.h"
#include <stdbool.h>

#include <esp_system.h>

// For print
#include "esp_log.h"

#define BUTTON_TAG "BUTTON"

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

static bool adc_muted = false;

void on_enter_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button ENTER pushed");
    if (!adc_muted)
    {
        audio_codec_mute_adc();
        adc_muted = true;
        ESP_LOGI(BUTTON_TAG, "ADC MUTE");
    }
    else
    {
        audio_codec_unmute_adc();
        adc_muted = false;
        ESP_LOGI(BUTTON_TAG, "ADC UNMUTE");
    }
}

void on_back_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button BACK pushed");
    pcm_capture_request(1);
}

void on_next_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button NEXT pushed");
    pcm_capture_request(0);
}

void on_direction_changed(direction_state new_state)
{

    // Restart is the easiest to clean everything and make sure there is no memory leak.
    // It is much easier than to deallocate all the ressources
    esp_restart();
}
