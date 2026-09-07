#include "io_expander.h"
#include "utils.h"
#include "driver/gpio.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cmd_i2ctools.h"

static uint8_t io_expander_gpio = 0x00;

void reset_io_expander(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_32,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_32, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_32, 1));
    vTaskDelay(pdMS_TO_TICKS(10));
    io_expander_gpio = 0x00;
}

void io_expander_init(void)
{
    // Register IODIR - I/O DIRECTION REGISTER
    i2c_set(IO_EXPANDER_ADDR, 0x0, IO_EXPANDER_IODIR);
    is_expected(IO_EXPANDER_TAG, 0x00, i2c_get(IO_EXPANDER_ADDR, 0x00), IO_EXPANDER_IODIR);

    // default values for all the following registers
    // i2c_get(IO_EXPANDER_ADDR, 0x01);
    // i2c_get(IO_EXPANDER_ADDR, 0x02);
    // i2c_get(IO_EXPANDER_ADDR, 0x03);
    // i2c_get(IO_EXPANDER_ADDR, 0x04);
    // i2c_get(IO_EXPANDER_ADDR, 0x05);
    // i2c_get(IO_EXPANDER_ADDR, 0x06);
    // i2c_get(IO_EXPANDER_ADDR, 0x07);
    // i2c_get(IO_EXPANDER_ADDR, 0x08);

    // GPIO - GENERAL PURPOSE I/O PORT REGISTER
    i2c_set(IO_EXPANDER_ADDR, 0x09, io_expander_gpio);

    // OLAT can be ignored.
    // i2c_get(IO_EXPANDER_ADDR, 0x0A);
}

void set_io_expander(uint8_t mask)
{
    io_expander_gpio |= mask;
    i2c_set(IO_EXPANDER_ADDR, 0x09, io_expander_gpio);
}

void clear_io_expander(uint8_t mask)
{
    io_expander_gpio &= ~mask;
    i2c_set(IO_EXPANDER_ADDR, 0x09, io_expander_gpio);
}
