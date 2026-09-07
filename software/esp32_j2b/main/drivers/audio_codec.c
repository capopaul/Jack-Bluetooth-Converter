// Author : Paul Capgras
// Date   : Oct 6, 2025

// to configure and control GPIO pins
#include "driver/gpio.h"

#include "audio_codec.h"
#include "io_expander.h"

// Provides essential definitions for task management and delays
#include "freertos/FreeRTOS.h"

// Provide functions for task management and delays
#include "freertos/task.h"

void reset_audio_codec(void)
{
    clear_io_expander(IO_EXPANDER_CODEC_RESET_L_MASK);
    vTaskDelay(pdMS_TO_TICKS(10));
    set_io_expander(IO_EXPANDER_CODEC_RESET_L_MASK);
    vTaskDelay(pdMS_TO_TICKS(10));
}


#include "driver/i2c_master.h"
#include "cmd_i2ctools.h"
#include "esp_log.h"

void audio_codec_check_power_ready(void)
{
    uint8_t status = 0;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        status = i2c_get(0x18, 94);
        // 0xff is also the current I2C helper's error return.
        if (status != 0xff && (status & 0xc6) == 0xc6)
        {
            ESP_LOGI("AUDIO_CODEC", "DACs and headphone outputs powered up");
            return;
        }
    }
    ESP_LOGW("AUDIO_CODEC", "Power readiness timed out: register 94 = 0x%02x", status);
}
