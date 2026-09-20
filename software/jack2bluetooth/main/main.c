/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_err.h"

// Bluetooth
#include "bt_app_core.h"
#include "bt_app_a2dp_source.h"

// Include i2c
#include "driver/i2c_master.h"
#include "./drivers/i2c.h"

// Include I2S
#include "./drivers/i2s.h"

// Include audio codec
#include "./drivers/audio_codec.h"

// Include IO expander
#include "./drivers/io_expander.h"

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/

/*******************************
 * Main function
 ******************************/

void app_main(void)
{
    printf("Hello world!\n");

    i2c_init();

    ///////////////////////
    //    IO Expander    //
    ///////////////////////

    io_expander_reset();

    io_expander_init();

    io_expander_set(IO_EXPANDER_LED_ADC_MASK);
    io_expander_set(IO_EXPANDER_LED_DAC_MASK);

    ///////////////////////
    //    Audio Codec    //
    ///////////////////////

    audio_codec_reset();

    audio_codec_configure_pll();

    // Init I2S
    // This should be moved after bluetooth initialized.
    // Because i2s should be set according to bluetooth
    esp_i2s_driver_install();
    audio_codec_configure_i2s_settings();

    // Configure Routing
    audio_codec_connect_line2_to_adc();

    audio_codec_power_up_adc();

    audio_codec_unmute_adc();

    ///////////////////////
    //     Bluetooth     //
    ///////////////////////

    bt_app_init();

    bt_app_task_start_up();

    ESP_ERROR_CHECK(bt_app_a2dp_source_start());
}
