// Author : Paul Capgras
// Date   : Oct 10, 2025

#include "driver/i2c_master.h"

extern i2c_master_bus_handle_t tool_bus_handle;

// Set an i2c register
int i2c_set(int chip_address, int register_address, uint8_t data);

// Read an i2c register
uint8_t i2c_get(int chip_address, int register_address);

// Log an error under the caller's tag when a register differs from its expected value.
int is_expected(const char *tag, int register_address, uint8_t read_value, uint8_t expected_value);

int i2c_detect();
int i2c_dump(int chip_addr, int size);
