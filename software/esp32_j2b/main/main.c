/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

// Non volatile storage drivers
#include "nvs_flash.h"

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

    ///////////////////////
    //     Bluetooth     //
    ///////////////////////

    init_non_volatile_storage();

    bt_app_init();

    bt_app_task_start_up();

    ESP_ERROR_CHECK(bt_app_a2dp_source_start());
}
