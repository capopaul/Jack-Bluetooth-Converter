// Author : Paul Capgras
// Date   : Oct 6, 2025
// Bluetooth is pairing using Legacy Pairing

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

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"

// Bluetooth
#include "bt_app_core.h"
#include "bt_app_a2dp.h"

// Non volatile storage drivers
#include "nvs.h"
#include "nvs_flash.h"

#include "driver/i2c_master.h"

// Include i2c tools example
#include "./drivers/cmd_i2ctools.h"

// Include audio codec
#include "./drivers/audio_codec.h"

// Include IO expander
#include "./drivers/io_expander.h"
#include "./drivers/utils.h"

#define CODEC_ADDR 0x18
#define CODEC_TAG "AUDIO_CODEC"

static gpio_num_t i2c_gpio_sda = 21;
static gpio_num_t i2c_gpio_scl = 19;

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/

static void init_non_volatile_storage()
{
    /* initialize NVS (Non-volatile storage) — it is used to store PHY calibration data */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
};

void app_main(void)
{
    printf("Hello world!\n");

    ///////////////////////
    //     Setup I2C     //
    ///////////////////////

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = i2c_gpio_scl,
        .sda_io_num = i2c_gpio_sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &tool_bus_handle));

    ///////////////////////
    //    IO Expander    //
    ///////////////////////

    reset_io_expander();
    i2c_detect();

    io_expander_init();

    set_io_expander(IO_EXPANDER_LED_ADC_MASK);
    set_io_expander(IO_EXPANDER_LED_DAC_MASK);

    ///////////////////////
    //    Audio Codec    //
    ///////////////////////

    // Codec reset uses I2C through the initialized IO expander.
    reset_audio_codec();
    i2c_detect();

    // Register 1 - SW reset
    i2c_set(CODEC_ADDR, 1, 0x80);

    /*
     * Clock
     */

    // Register 3 - PLL Programming Register A
    // D7   - 1    - PLL is enabled
    // D6-3 - 0000 - Q (ignored here)
    // D2-0 - 001  - P = 1
    i2c_set(CODEC_ADDR, 3, 0b10000001);
    is_expected(CODEC_TAG, 3, i2c_get(CODEC_ADDR, 3), 0b10000001);

    // Register 4 - PLL Programming Register B
    // D7-2 - 1000 00 - Set J to 32
    // D1-0 - 00      - Reserved
    i2c_set(CODEC_ADDR, 4, 0b10000000);
    is_expected(CODEC_TAG, 4, i2c_get(CODEC_ADDR, 4), 0b10000000);

    // Register 5 and 6
    // Set D to 0
    // MSB D7-0 (reg 5) 0
    // LSB D7-2 (reg 6) 0
    // D1-0             0
    // always write both, and in the order reg5 then reg6.
    is_expected(CODEC_TAG, 5, i2c_get(CODEC_ADDR, 5), 0b00000000);
    is_expected(CODEC_TAG, 6, i2c_get(CODEC_ADDR, 6), 0b00000000);

    // Register 11 - Audio Codec Overflow Flag Register
    // D7-4 - 0    - Ignore
    // D3-0 - 0001 - Set R to 1
    i2c_set(CODEC_ADDR, 11, 0b00000001);
    is_expected(CODEC_TAG, 11, i2c_get(CODEC_ADDR, 11), 0b00000001);

    // Register 101 - Clock register
    // D7-1 - 0
    // D0   - 0 - CODEC_CLKIN uses PLLDIV_OUT
    i2c_set(CODEC_ADDR, 101, 0b00000000);
    is_expected(CODEC_TAG, 101, i2c_get(CODEC_ADDR, 101), 0b00000000);

    // Register 102 - Clock Generation Control Register
    // D7-6 - 0    -
    // D5-4 - 10   - PLLCLK_IN uses BCLK
    // D3-0 - 0010 - Reserved
    i2c_set(CODEC_ADDR, 102, 0b00100010);
    is_expected(CODEC_TAG, 102, i2c_get(CODEC_ADDR, 102), 0b00100010);

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
    // // read again
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

    // // Read register 97 - Real-Time Interrupt Flags Register
    // is_expected(CODEC_TAG, 97, i2c_get(CODEC_ADDR, 97), 0b00000011);

    // ///////////////////////
    // //     Bluetooth     //
    // ///////////////////////

    init_non_volatile_storage();

    bt_app_init();

    bt_app_task_start_up();

    // create a bluetooth message to register callback functions
    bt_app_work_dispatch(bt_app_register_callback_function, BT_APP_EVT_REGISTER_CB_FUNCTION, NULL, 0, NULL);

    // Wait 1 second
    vTaskDelay(pdMS_TO_TICKS(1000));
}
