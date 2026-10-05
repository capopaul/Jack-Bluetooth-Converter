// Author : Paul Capgras
// Date   : Oct 10, 2025
#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"

#define I2C_GPIO_SDA 21
#define I2C_GPIO_SCL 19

// Hold both I2C lines low before starting the I2C peripheral. Use this after
// removing power from an I2C device that may otherwise be parasitically
// powered through SDA or SCL.
void i2c_discharge_lines(void);

void i2c_init();

// Serialize access to the shared I2C bus. Required around direct esp_lcd
// panel operations because the register helpers add and remove devices.
void i2c_lock(void);
void i2c_unlock(void);

// Set an i2c register
int i2c_set(int chip_address, int register_address, uint8_t data);

// Read an i2c register
uint8_t i2c_get(int chip_address, int register_address);

// Log an error under the caller's tag when a register differs from its expected value.
int is_expected(const char *tag, int register_address, uint8_t read_value, uint8_t expected_value);

int i2c_detect();
int i2c_dump(int chip_addr, int size);

// Shared bus used by the codec, IO expander, and OLED panel.
i2c_master_bus_handle_t i2c_get_bus(void);
