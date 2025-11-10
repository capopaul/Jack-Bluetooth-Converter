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

static gpio_num_t i2c_gpio_sda = 16;
static gpio_num_t i2c_gpio_scl = 17;

#define CODEC_ADDR 0x18
#define CODEC_TAG "AUDIO_CODEC"

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

static void is_expected(int register_address, uint8_t read_value, uint8_t expected_value)
{
    if (read_value != expected_value)
    {
        ESP_LOGW(CODEC_TAG, "Reg [%d] Read : 0x%02x vs Expected 0x%02x", register_address, read_value, expected_value);
    }
}

void app_main(void)
{
    printf("Hello world!\n");

    reset_audio_codec();

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
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &tool_bus_handle));

    ///////////////////////
    //    Audio Codec    //
    ///////////////////////

    i2c_detect();

    uint8_t read_value;
    uint8_t expected_value;

    // Register 1 - SW reset
    i2c_set(CODEC_ADDR, 1, 0x80);

    /*
     * Clock
     */

    // Register 3 - PLL Programming Register A
    // D7   - 1    - PLL is enabled
    // D6-3 - 0000 -
    // D2-0 - 001  - P= 1
    i2c_set(CODEC_ADDR, 3, 0b10000001);
    is_expected(3, i2c_get(CODEC_ADDR, 3), 0b10000001);

    // Register 4 - PLL Programming Register B
    // D7-2 - 100000 // Set J to 32
    // D1-0 - 00
    i2c_set(CODEC_ADDR, 4, 0b10000000);
    is_expected(4, i2c_get(CODEC_ADDR, 4), 0b10000000);

    // Register 5 and 6
    // Set D to 0
    // MSB D7-0 (reg 5) 0
    // LSB D7-2 (reg 6) 0
    // D1-0             0
    // always write both, and in the order reg5 then reg6.
    is_expected(5, i2c_get(CODEC_ADDR, 5), 0b00000000);
    is_expected(6, i2c_get(CODEC_ADDR, 6), 0b00000000);

    // Register 11 - Audio Codec Overflow Flag Register
    // D7-4 0
    // D3-0 0010 Set R to 0001
    i2c_set(CODEC_ADDR, 11, 0b00000010);
    is_expected(11, i2c_get(CODEC_ADDR, 11), 0b00000010);

    // Register 102 - Clock Generation Control Register
    // D7-6 - 0  -
    // D5-4 - 10 - PLLCLK_IN uses BCLK
    // D3-0 - 0010
    // No need to write
    i2c_set(CODEC_ADDR, 102, 0b00100010);
    is_expected(102, i2c_get(CODEC_ADDR, 102), 0b00100010);

    // Register 101 - Clock register
    // D7-1 - 0
    // D0   - 0 - CODEC_CLKIN uses PLLDIV_OUT
    i2c_set(CODEC_ADDR, 101, 0b00000000);
    is_expected(101, i2c_get(CODEC_ADDR, 101), 0b00000000);

    /*
     * Audio codec
     */

    // Register 7 - Codec Data-Path Setup Register
    // D7   - 1  - Set fs=44.1kHz
    // D6-5 - 00
    // D4-3 - 01 - Left DAC plays left input data
    // D2-1 - 01 - Right DAC plays right input data
    // D0   - 0
    // 1000 1010
    i2c_set(CODEC_ADDR, 7, 0b10001010);
    // // read again
    is_expected(7, i2c_get(CODEC_ADDR, 7), 0b10001010);

    // Register 37 - DAC Power and Output Driver Control Register
    // D7   - 1 - Left DAC is powered up
    // D6   - 1 - Right DAC is powered up
    // D5-0 - 0
    // Write 1100 0000
    i2c_set(CODEC_ADDR, 37, 0b11000000);
    is_expected(37, i2c_get(CODEC_ADDR, 37), 0b11000000);

    // Register 41 - DAC Output Switching Control Register
    // D7-6 - 10 - Left DAC output selects DAC-L2 path to left high power output drivers
    // D5-4 - 10 - Right DAC output selects DAC-R2 path to right high power output drivers
    // D3-0 - 0000
    i2c_set(CODEC_ADDR, 41, 0b10100000);
    is_expected(41, i2c_get(CODEC_ADDR, 41), 0b10100000);

    // Register 51 - HPLout output level control register
    // D7-4 - 0000
    // D3   - 1 - Unmute
    // D2   - 1
    // D1   - 1
    // D0   - 1 - Power up
    // write 0000 1111
    i2c_set(CODEC_ADDR, 51, 0b00001111);
    is_expected(51, i2c_get(CODEC_ADDR, 51), 0b00001111);

    // Register 65 - HPRout output level control register
    // D7-4 - 0000
    // D3   - 1 - Unmute
    // D2   - 1
    // D1   - 1
    // D0   - 1 - Power up
    // write 0000 1111
    i2c_set(CODEC_ADDR, 65, 0b00001111);
    is_expected(65, i2c_get(CODEC_ADDR, 65), 0b00001111);

    // Register 43 - Left-DAC Digital Volume Control Register
    // D7   - 0 - Unmute left-DAC channel
    // D6-0 - 0
    i2c_set(CODEC_ADDR, 43, 0b00000000);
    is_expected(43, i2c_get(CODEC_ADDR, 43), 0b00000000);

    // Register 44 - Right-DAC Digital Volume Control Register
    // D7   - 0 - Unmute right-DAC channel
    // D6-0 - 0
    i2c_set(CODEC_ADDR, 44, 0b00000000);
    is_expected(44, i2c_get(CODEC_ADDR, 44), 0b00000000);

    // Status

    // Register 94 - Module power status register
    is_expected(94, i2c_get(CODEC_ADDR, 94), 0b11000110);

    // Register 95 - Output driver short circuit detection status register
    is_expected(95, i2c_get(CODEC_ADDR, 95), 0b00000000);
    // -> 0c
    // DEBUG - WHY is HPRCOM Power up???
    // Read register 72
    is_expected(72, i2c_get(CODEC_ADDR, 72), 0b00000110);
    // Read register 97
    is_expected(97, i2c_get(CODEC_ADDR, 97), 0b00000011);
    //
    // Maybe a yield problem?

    // Register 96 - Sticky Interrupt Flags register
    is_expected(96, i2c_get(CODEC_ADDR, 96), 0b00000000);

    ///////////////////////
    //     Bluetooth     //
    ///////////////////////

    init_non_volatile_storage();

    bt_app_init();

    bt_app_task_start_up();

    bt_app_work_dispatch(bt_app_register_callback_function, BT_APP_EVT_REGISTER_CB_FUNCTION, NULL, 0, NULL);

    // Wait 1 millisecond
    vTaskDelay(pdMS_TO_TICKS(1000));
}
