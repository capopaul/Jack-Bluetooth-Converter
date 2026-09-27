#pragma once

// Portrait geometry of the 1.69-inch ST7789 module.
#define LCD_WIDTH 240
#define LCD_HEIGHT 280

// Assumes a centered 240x280 window in the controller's 240x320 RAM.
// Confirm these offsets on the actual module; adjust if the image is shifted.
#define LCD_X_OFFSET 0
#define LCD_Y_OFFSET 20
