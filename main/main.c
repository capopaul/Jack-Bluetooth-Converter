// Author : Paul Capgras
// Date   : Jun 14, 2025

////////////
//  libc  //
////////////

// Standard Input Output
#include <stdio.h>

////////////////////////////
//   esp-idf framework    //
////////////////////////////

// to configure and control GPIO pins
#include "driver/gpio.h"

// Provides essential definitions for task management and delays
#include "freertos/FreeRTOS.h"

// Provide functions for task management and delays
#include "freertos/task.h"

#define AUDIO_CODEC_RESETN_GPIO 18
#define AUDIO_CODEC_RESETN_MASK (1ULL << AUDIO_CODEC_RESETN_GPIO)

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
    vTaskDelay(pdMS_TO_TICKS(1));

    // Pull reset pin HIGH
    gpio_set_level(AUDIO_CODEC_RESETN_GPIO, 1);
    printf("Audio codec reset pin HIGH\n");
}

void app_main(void)
{
    printf("Hello world!\n");

    reset_audio_codec();
}
