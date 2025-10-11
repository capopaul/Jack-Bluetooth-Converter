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
    //    Audio Codec    //
    ///////////////////////

    reset_audio_codec();

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

    // do_i2cdump_cmd(0x18, 1);

    ///////////////////////
    //     Bluetooth     //
    ///////////////////////

    init_non_volatile_storage();

    bt_app_init();

    // bt_app_task_start_up();

    // bt_app_work_dispatch(bt_app_register_callback_function, BT_APP_EVT_REGISTER_CB_FUNCTION, NULL, 0, NULL);
}
