// Author : Paul Capgras
// Date   : Oct 10, 2025
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define I2C_GPIO_SDA 21
#define I2C_GPIO_SCL 19

void i2c_init();

// Set an i2c register
int i2c_set(int chip_address, int register_address, uint8_t data);

// Read an i2c register
uint8_t i2c_get(int chip_address, int register_address);

// Log an error under the caller's tag when a register differs from its expected value.
int is_expected(const char *tag, int register_address, uint8_t read_value, uint8_t expected_value);

int i2c_detect();
int i2c_dump(int chip_addr, int size);

// Send raw bytes to an I2C device. Useful for devices such as displays that
// use a control byte rather than a register address.
esp_err_t i2c_write_raw(uint8_t chip_address, const uint8_t *data, size_t data_len);
