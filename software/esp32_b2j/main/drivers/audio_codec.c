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
