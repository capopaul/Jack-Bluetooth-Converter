// Author : Paul Capgras
// Date   : Oct 6, 2025

// to configure and control GPIO pins
#include "driver/gpio.h"

#include "audio_codec.h"

// Provides essential definitions for task management and delays
#include "freertos/FreeRTOS.h"

// Provide functions for task management and delays
#include "freertos/task.h"

void reset_audio_codec(void)
{
    // Configure GPIO as output
    gpio_config_t io_conf = {
        .pin_bit_mask = AUDIO_CODEC_RESETN_MASK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Pull reset pin LOW
    gpio_set_level(AUDIO_CODEC_RESETN_GPIO, 0);
    printf("Audio codec reset pin LOW\n");

    // Wait 1 millisecond
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Pull reset pin HIGH
    gpio_set_level(AUDIO_CODEC_RESETN_GPIO, 1);
    printf("Audio codec reset pin HIGH\n");
}
