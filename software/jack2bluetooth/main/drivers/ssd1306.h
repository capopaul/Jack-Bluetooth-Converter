#pragma once

#include <stddef.h>
#include "esp_err.h"

// Initialize the 128x64 SSD1306 OLED at I2C address 0x3C and show "Paul" with LVGL.
esp_err_t ssd1306_show_name(void);

// Replace the text shown on the first, second, and third display lines.
// Pass NULL or an empty string to clear a line.
esp_err_t ssd1306_set_text_lines(const char *first_line,
                                 const char *second_line,
                                 const char *third_line);

// Show a selectable two-row list. The screen displays "Jack -> Bluetooth"
// on line 1, then items[i] and items[i + 1] on lines 2 and 3. The caller must
// keep the array and its strings valid while this list is displayed.
esp_err_t ssd1306_show_string_list(const char *const items[], size_t item_count);

// Move the list's first visible item by one. Navigation stops at each end.
esp_err_t ssd1306_list_next(void);
esp_err_t ssd1306_list_back(void);
