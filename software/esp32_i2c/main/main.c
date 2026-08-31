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

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_fat.h"
// #include "cmd_system.h"
#include "driver/i2c_master.h"

// Include i2c tools example
#include "cmd_i2ctools.h"

#define IO_EXPANDER_RESETN_GPIO 32
#define AUDIO_CODEC_RESETN_GPIO 33

static const char *TAG = "i2c-tools";

static gpio_num_t i2c_gpio_sda = 19;
static gpio_num_t i2c_gpio_scl = 21;

static i2c_port_t i2c_port = I2C_NUM_0;

void release_reset(gpio_num_t gpio)
{
    ESP_ERROR_CHECK(gpio_set_level(gpio, 1));
}

void hold_reset_low(gpio_num_t gpio)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(gpio, 0));
}

void app_main(void)
{
    printf("Hello world!\n");

    hold_reset_low(IO_EXPANDER_RESETN_GPIO);
    hold_reset_low(AUDIO_CODEC_RESETN_GPIO);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Create UART console
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    // Create I2C bus
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = i2c_port,
        .scl_io_num = i2c_gpio_scl,
        .sda_io_num = i2c_gpio_sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &tool_bus_handle));


    printf("\n ==============================================================\n");
    printf(" |             Steps to Use i2c-tools                         |\n");
    printf(" |                                                            |\n");
    printf(" |  1. Try 'help', check all supported commands               |\n");
    printf(" |  2. Try 'i2cconfig' to configure your I2C bus              |\n");
    printf(" |  3. Try 'i2cdetect' to scan devices on the bus             |\n");
    printf(" |  4. Try 'i2cget' to get the content of specific register   |\n");
    printf(" |  5. Try 'i2cset' to set the value of specific register     |\n");
    printf(" |  6. Try 'i2cdump' to dump all the register (Experiment)    |\n");
    printf(" |                                                            |\n");
    printf(" ==============================================================\n\n");

    // Register commands
    register_i2ctools();

    // Start interactive console
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
