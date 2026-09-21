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
        audio_codec_configure_i2s_settings();

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

        /*
         * Configure Codec
         */

        // Register 2 - default is good
        // Register 8 - default is good
        // Register 9 - default is good
        // Register 10 - default is good

        // Register 7 - Codec Data-Path Setup Register
        // D7   - 1  - Set fs=44.1kHz
        // D6-5 - 00
        // D4-3 - 01 - Left DAC plays left input data
        // D2-1 - 01 - Right DAC plays right input data
        // D0   - 0
        // 1000 1010
        i2c_set(CODEC_ADDR, 7, 0b10001010);
        // read again
        is_expected(CODEC_TAG, 7, i2c_get(CODEC_ADDR, 7), 0b10001010);

        /*
         * Configure Routing
         */

        // Register 41 - DAC Output Switching Control Register
        // D7-6 - 10 - Left DAC output selects DAC-L2 path to left high power output drivers
        // D5-4 - 10 - Right DAC output selects DAC-R2 path to right high power output drivers
        // D3-0 - 0000
        i2c_set(CODEC_ADDR, 41, 0b10100000);
        is_expected(CODEC_TAG, 41, i2c_get(CODEC_ADDR, 41), 0b10100000);

        // Register 47 - default is good
        // Register 64 - default is good

        /*
         * Configure the topology
         */

        // Register 14 - Headset/Button Press Detection Register B
        // D7   - 1 - Programs HPout for AC-coupled
        // D6-0 -   - ignore
        i2c_set(CODEC_ADDR, 14, 0b10000000);

        // Register 40 - default is good
        // Register 42 - default is good

        // Register 38 - Headset/Button Press Detection Register B
        // D7-6 - 00   - reserved
        // D5-3 - 010  - HPRCOM is configured as independent single-ended output
        // D2-1 - 00   - ignored
        // D0   - 0    - reserved
        i2c_set(CODEC_ADDR, 38, 0b00010000);
        is_expected(CODEC_TAG, 38, i2c_get(CODEC_ADDR, 38), 0b00010000);

        /*
         * Power the DAC
         */

        // Register 37 - DAC Power and Output Driver Control Register
        // D7   - 1 - Left DAC is powered up
        // D6   - 1 - Right DAC is powered up
        // D5-0 - 0
        // Write 1100 0000
        i2c_set(CODEC_ADDR, 37, 0b11000000);
        is_expected(CODEC_TAG, 37, i2c_get(CODEC_ADDR, 37), 0b11000000);

        /*
         * Power the headphone outputs
         * Keep the headphone muted
         */

        // Register 51 - HPLout output level control register
        // D7-4 - 0000
        // D3   - 0 - mute
        // D2   - 1
        // D1   - 1
        // D0   - 1 - Power up
        i2c_set(CODEC_ADDR, 51, 0b00000111);

        // Register 65 - HPRout output level control register
        // D7-4 - 0000
        // D3   - 0 - mute
        // D2   - 1
        // D1   - 1
        // D0   - 1 - Power up
        i2c_set(CODEC_ADDR, 65, 0b00000111);

        /*
         * Volume control
         */

        // Register 43 - Left-DAC Digital Volume Control Register
        // D7   - 0 - Unmute left-DAC channel
        // D6-0 - 0
        i2c_set(CODEC_ADDR, 43, 0b00000000);
        is_expected(CODEC_TAG, 43, i2c_get(CODEC_ADDR, 43), 0b00000000);

        // Register 44 - Right-DAC Digital Volume Control Register
        // D7   - 0 - Unmute right-DAC channel
        // D6-0 - 0
        i2c_set(CODEC_ADDR, 44, 0b00000000);
        is_expected(CODEC_TAG, 44, i2c_get(CODEC_ADDR, 44), 0b00000000);

        /*
         * Unmute the headphone outputs
         */

        // Register 51 - HPLout output level control register
        // D7-4 - 0000
        // D3   - 1 - Unmute
        // D2   - 1
        // D1   - 1
        // D0   - 1 - Power up
        i2c_set(CODEC_ADDR, 51, 0b00001111);

        // Register 65 - HPRout output level control register
        // D7-4 - 0000
        // D3   - 1 - Unmute
        // D2   - 1
        // D1   - 1
        // D0   - 1 - Power up
        i2c_set(CODEC_ADDR, 65, 0b00001111);

        // // Status

        // Register 94 - Module Power Status Register
        is_expected(CODEC_TAG, 94, i2c_get(CODEC_ADDR, 94), 0b11000110);

        // Register 95 - Output driver short circuit detection status register
        is_expected(CODEC_TAG, 95, i2c_get(CODEC_ADDR, 95), 0b00000000);

        // Register 96 - Sticky Interrupt Flags register
        is_expected(CODEC_TAG, 96, i2c_get(CODEC_ADDR, 96), 0b00000000);

        ESP_ERROR_CHECK(bt_app_a2dp_sink_start());
    }
}
