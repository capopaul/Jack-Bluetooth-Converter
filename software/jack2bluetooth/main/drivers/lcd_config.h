#pragma once

// Portrait geometry of the 172x320 ST7789 module.
#define LCD_WIDTH 172
#define LCD_HEIGHT 320

// Assumes a centered 172x320 window in the controller's 240x320 RAM.
// Confirm these offsets on the actual module; adjust if the image is shifted.
#define LCD_X_OFFSET 34
#define LCD_Y_OFFSET 0

// Temporarily show solid colors to diagnose the display connection.
#define LCD_COLOR_TEST 1
