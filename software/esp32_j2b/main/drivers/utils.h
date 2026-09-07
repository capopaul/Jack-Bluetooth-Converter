#pragma once

#include <stdint.h>

// Log a warning under the caller's tag when a register differs from its expected value.
void is_expected(const char *tag, int register_address, uint8_t read_value, uint8_t expected_value);
