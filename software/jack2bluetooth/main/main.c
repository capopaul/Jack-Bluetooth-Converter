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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

// Screen
#include "./drivers/ssd1306.h"

// Include IO expander
#include "./drivers/io_expander.h"

// Include decode_task
#include "decode_task.h"

/*******************************
 * Main function
 ******************************/

void app_main(void)
{
    printf("Hello world!\n");

    // A reset button resets the ESP32 but does not reset the OLED.  If reset
    // interrupts an I2C transfer, the powered OLED can keep SDA low and lock
    // the bus. Reset the MCP23008 first: its outputs become inputs and the
    // P-channel MOSFET pull-up removes power from lcd_vdc before I2C starts.
    io_expander_reset();

    // The OLED can be parasitically powered through its SDA/SCL protection
    // diodes. Drain those lines while lcd_vdc is off so it performs a real
    // power-on reset before it is connected to the I2C peripheral.
    i2c_discharge_lines();

    i2c_init();

    ///////////////////////
    //    IO Expander    //
    ///////////////////////

    io_expander_init();

    // lcd_vcc_ctrl drives the gate of the display's P-channel power MOSFET.
    // It is active low: low connects 3.3 V to the lcd_vdc supply rail.
    io_expander_clear(IO_EXPANDER_LCD_VCC_CTRL_MASK);
    vTaskDelay(pdMS_TO_TICKS(20));

    ///////////////////////
    //    Audio Codec    //
    ///////////////////////

    audio_codec_reset();

    audio_codec_configure_pll();

    ///////////////////////
    //    I2C Screen     //
    ///////////////////////

    if (ssd1306_show_name() != ESP_OK)
    {
        printf("Failed to initialize screen!\n");
    }

    ///////////////////////
    //     Bluetooth     //
    ///////////////////////

    encode_task_init();

    bt_app_init();

    bt_app_gap_start();

    // check switch status.
    // during this phase, switch must not change state
    if (is_direction_j2b())
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

        // Remove ADC DC offset without cutting audible bass.
        audio_codec_configure_adc_high_pass();

        audio_codec_power_up_adc();

        audio_codec_unmute_adc();

        bt_app_a2dp_source_start();

        io_expander_set(IO_EXPANDER_LED_ADC_MASK);

        static const char *devices[] = {
            "Paul's iPhone",
            "MacBook Pro",
            "Kitchen Speaker",
            "Headphones",
        };

        ssd1306_show_string_list(devices, 4);
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

        io_expander_set(IO_EXPANDER_LED_DAC_MASK);
    }
}
