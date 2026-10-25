// Author : Paul Capgras
// Date   : Oct 10, 2025
#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"

// I2C0 is shared by the codec and IO expander.
#define I2C_GPIO_SDA 21
#define I2C_GPIO_SCL 19

// The SH1106 screen is wired to the former SPI screen signals (GPIO22/23)
// and uses its own I2C1 controller.
#define DISPLAY_I2C_GPIO_SDA 22
#define DISPLAY_I2C_GPIO_SCL 23

// Hold both I2C lines low before starting the I2C peripheral. Use this after
// removing power from an I2C device that may otherwise be parasitically
// powered through SDA or SCL.
void i2c_discharge_lines(void);

// Drain the display I2C lines while its supply is off to prevent parasitic
// powering through SDA/SCL after an ESP32 reset.
void i2c_display_discharge_lines(void);

void i2c_init();
void i2c_display_init();

// Serialize access to the shared codec/IO-expander I2C bus because the
// register helpers add and remove devices.
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

// Shared I2C0 bus used by the codec and IO expander.
i2c_master_bus_handle_t i2c_get_bus(void);

// Dedicated I2C1 bus used only by the SH1106 OLED panel.
i2c_master_bus_handle_t i2c_get_display_bus(void);
