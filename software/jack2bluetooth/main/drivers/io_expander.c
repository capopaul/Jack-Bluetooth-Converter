
#include <stdbool.h>
#include "io_expander.h"
#include "driver/gpio.h"

// To print
#include "esp_log.h"

// For task
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// For I2C
#include "i2c.h"
#include "driver/i2c_master.h"

// Buttons functions
#include "button.h"

#define IO_EXPANDER_TAG "IO_EXPANDER"

static TaskHandle_t interrupt_task_handle = NULL;

static uint8_t output_state = 0x00;
static uint8_t input_state;

static void update_input_state(void);
static void interrupt_handler(void *arg);
static void interrupt_task(void *arg);
static void create_interrupt_task(void);

/********************************
 * INTERNAL FUNCTION DECLARATIONS
 *******************************/

static void update_input_state(void)
{
    // Register GPIO - GENERAL PURPOSE I/O PORT REGISTER
    input_state = i2c_get(IO_EXPANDER_ADDR, 0x09) & IO_EXPANDER_IODIR;
}

// Small function the ESP32 automatically calls when the expander’s INT pin goes low.
// Its only job is to wake up the task
static void interrupt_handler(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    vTaskNotifyGiveFromISR(interrupt_task_handle,
                           &higher_priority_task_woken);

    if (higher_priority_task_woken)
    {
        portYIELD_FROM_ISR();
    }
}

// This task is woken up the interrupt_handler
// To perform I2C transactions to understand why the interrupt happens.
// No deboucing has been done
static void interrupt_task(void *arg)
{
    for (;;)
    {
        // Sleep until the GPIO interrupt handler notifies us.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        do
        {
            uint8_t previous_state = input_state;

            // Read INTF before GPIO: GPIO clears the interrupt flags.
            uint8_t interrupt_flags = i2c_get(IO_EXPANDER_ADDR, 0x07) & IO_EXPANDER_IODIR;
            update_input_state();
            uint8_t current_state = input_state;

            // These are sampled levels, not a history of every edge.
            if (interrupt_flags & IO_EXPANDER_BUTTON_DIRECTION_MASK)
            {
                on_direction_changed((current_state & IO_EXPANDER_BUTTON_DIRECTION_MASK) != 0);
            }

            uint8_t rising_edges = (uint8_t)~previous_state & interrupt_flags;

            // For the following, only 0 -> 1 event is interesting.
            if (rising_edges & IO_EXPANDER_BUTTON_ENTER_MASK)
            {
                on_enter_pressed();
            }

            if (rising_edges & IO_EXPANDER_BUTTON_BACK_MASK)
            {
                on_back_pressed();
            }

            if (rising_edges & IO_EXPANDER_BUTTON_NEXT_MASK)
            {
                on_next_pressed();
            }

            // If INT remains low, retry without continuously using CPU.
            if (gpio_get_level(IO_EXPANDER_INT_GPIO) == 0)
            {
                vTaskDelay(1);
            }
        } while (gpio_get_level(IO_EXPANDER_INT_GPIO) == 0);
    }
}

static void create_interrupt_task(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << IO_EXPANDER_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    BaseType_t result = xTaskCreate(
        interrupt_task,
        "io_expander_irq",
        3072,
        NULL,
        tskIDLE_PRIORITY + 1,
        &interrupt_task_handle);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    // The ISR service may already have been installed elsewhere.
    esp_err_t error = gpio_install_isr_service(0);
    if (error != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(error);
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(
        IO_EXPANDER_INT_GPIO,
        interrupt_handler,
        NULL));
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
    create_interrupt_task();

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

bool is_direction_j2b()
{
    return io_expander_read(IO_EXPANDER_BUTTON_DIRECTION_MASK) == 0;
}

bool is_direction_b2j()
{
    return !is_direction_j2b();
}
