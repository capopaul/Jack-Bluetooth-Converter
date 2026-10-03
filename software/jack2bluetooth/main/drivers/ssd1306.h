#pragma once

#include "esp_err.h"

// Initialize the 128x64 SSD1306 OLED at I2C address 0x3C and show "Paul" with LVGL.
esp_err_t ssd1306_show_name(void);
