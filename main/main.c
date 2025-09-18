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

#define AUDIO_CODEC_RESETN_GPIO 18
#define AUDIO_CODEC_RESETN_MASK (1ULL << AUDIO_CODEC_RESETN_GPIO)

#define LED_DAC_GPIO 8
#define LED_ADC_GPIO 9

#define LED_DAC_MASK (1ULL << LED_DAC_GPIO)
#define LED_ADC_MASK (1ULL << LED_ADC_GPIO)

static const char *TAG = "i2c-tools";

static gpio_num_t i2c_gpio_sda = 6;
static gpio_num_t i2c_gpio_scl = 7;

static i2c_port_t i2c_port = I2C_NUM_0;

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

void init_leds(void)
{
    // Configure GPIO as output
    gpio_config_t io_conf = {
        .pin_bit_mask = LED_DAC_MASK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    gpio_set_level(LED_DAC_GPIO, 1);

    // Configure GPIO as output
    gpio_config_t io_conf_2 = {
        .pin_bit_mask = LED_ADC_MASK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf_2);

    gpio_set_level(LED_ADC_GPIO, 1);
}

void app_main(void)
{
    printf("Hello world!\n");

    reset_audio_codec();

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = i2c_port,
        .scl_io_num = i2c_gpio_scl,
        .sda_io_num = i2c_gpio_sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &tool_bus_handle));

    register_i2ctools();

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

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
