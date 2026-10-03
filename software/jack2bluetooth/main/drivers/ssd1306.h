#pragma once

#include "esp_err.h"

// Initialize the 128x64 SSD1306 OLED at I2C address 0x3C and show "Paul" with LVGL.
esp_err_t ssd1306_show_name(void);

// Replace the text shown on the first, second, and third display lines.
// Pass NULL or an empty string to clear a line.
esp_err_t ssd1306_set_text_lines(const char *first_line,
                                 const char *second_line,
                                 const char *third_line);
