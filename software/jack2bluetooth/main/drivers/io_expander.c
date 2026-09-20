#include "io_expander.h"
#include "driver/gpio.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c.h"

static uint8_t output_state = 0x00;
static uint8_t input_state;

static void update_input_state(void);

/********************************
 * INTERNAL FUNCTION DECLARATIONS
 *******************************/

static void update_input_state(void)
{
    // Register GPIO - GENERAL PURPOSE I/O PORT REGISTER
    input_state = i2c_get(IO_EXPANDER_ADDR, 0x09) & IO_EXPANDER_IODIR;
}

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void io_expander_reset(void)
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
    output_state = 0x00;
}

void io_expander_init(void)
{
    // Set I/O direction
    // Register IODIR - I/O DIRECTION REGISTER
    i2c_set(IO_EXPANDER_ADDR, 0x0, IO_EXPANDER_IODIR);
    is_expected(IO_EXPANDER_TAG, 0x00, i2c_get(IO_EXPANDER_ADDR, 0x00), IO_EXPANDER_IODIR);

    // Set Outputs
    // Register GPIO - GENERAL PURPOSE I/O PORT REGISTER
    i2c_set(IO_EXPANDER_ADDR, 0x09, output_state);

    // Read inputs
    update_input_state();

    // Create a task to monitor IO expander interrupt
    // TBD later
    // the task will read INTF to know the cause of the IRQ
    // it can also read
    // to clear the interrupt read INTCAP or GPIO

    // Enable interrupts
    // Register INTCON is ok
    // Register GPINTEN - INTERRUPT-ON-CHANGE PINS (ADDR 0x02)
    i2c_set(IO_EXPANDER_ADDR, 0x02, IO_EXPANDER_IODIR);
}

void io_expander_set(uint8_t mask)
{
    output_state |= mask & (uint8_t)~IO_EXPANDER_IODIR;
    i2c_set(IO_EXPANDER_ADDR, 0x09, output_state);
}

void io_expander_clear(uint8_t mask)
{
    output_state &= ~mask & (uint8_t)~IO_EXPANDER_IODIR;
    i2c_set(IO_EXPANDER_ADDR, 0x09, output_state);
}

// Mask should only be one mask.
// This perform a cached read based on the value stored in memory.
// For inputs, these values are updated with interrupts.
// For oututs, these values are updated when using _set and _clear functions.
int io_expander_read(uint8_t mask)
{
    uint8_t inputs = input_state & IO_EXPANDER_IODIR;
    uint8_t outputs = output_state & (uint8_t)~IO_EXPANDER_IODIR;
    uint8_t pins = (inputs | outputs) & mask;
    return pins != 0;
}
