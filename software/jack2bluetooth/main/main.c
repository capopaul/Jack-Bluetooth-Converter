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
#include "bt_app_a2dp_sink.h"
#include "bt_app_gap.h"

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

    ///////////////////////
    //     Bluetooth     //
    ///////////////////////

    bt_app_init();

    ESP_ERROR_CHECK(bt_app_gap_start());

    // check switch status.
    // during this phase, switch must not change state
    if (is_direction_b2j())
    {
        printf("=====================\n");
        printf("Jack -----> Bluetooth\n");
        printf("=====================\n");

        // Init I2S
        // This should be moved after bluetooth initialized.
        // Because i2s should be set according to bluetooth
        i2s_driver_install(I2S_MODE_RX);
        audio_codec_configure_i2s_settings(FS_44_1HZ);

        // Configure Routing
        audio_codec_connect_line2_to_adc();

        audio_codec_power_up_adc();

        audio_codec_unmute_adc();

        ESP_ERROR_CHECK(bt_app_a2dp_source_start());
    }
    else
    {
        printf("=====================\n");
        printf("Bluetooth -----> Jack\n");
        printf("=====================\n");

        // Init I2S
        i2s_driver_install(I2S_MODE_TX);
        audio_codec_configure_i2s_settings(FS_44_1HZ);

        // Configure Routing
        audio_codec_connect_input_to_dac();

        audio_codec_configure_sink_topology();

        audio_codec_power_up_dac();

        audio_codec_power_up_headphone();

        audio_codec_unmute_dac();

        audio_codec_unmute_headphone();

        // Status

        // Register 94 - Module Power Status Register
        is_expected(CODEC_TAG, 94, i2c_get(CODEC_ADDR, 94), 0b11000110);

        // Register 95 - Output driver short circuit detection status register
        is_expected(CODEC_TAG, 95, i2c_get(CODEC_ADDR, 95), 0b00000000);

        // Register 96 - Sticky Interrupt Flags register
        is_expected(CODEC_TAG, 96, i2c_get(CODEC_ADDR, 96), 0b00000000);

        ESP_ERROR_CHECK(bt_app_a2dp_sink_start());
    }
}
