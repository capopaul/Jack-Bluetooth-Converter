#pragma once

#include <stdint.h>

#define IO_EXPANDER_ADDR 0x20
#define IO_EXPANDER_TAG "IO_EXPANDER"

// MCP23008 IODIR: 1 = input, 0 = output.
#define IO_EXPANDER_IODIR 0b00001111

// D7 - O - codec_reset_l
// D6 - O - lcd_vcc_ctrl
// D5 - O - led_dac
// D4 - O - led_adc
// D3 - I - button_direction
// D2 - I - button_next
// D1 - I - button_back
// D0 - I - button_enter
#define IO_EXPANDER_CODEC_RESET_L_MASK (1 << 7)
#define IO_EXPANDER_LCD_VCC_CTRL_MASK (1 << 6)
#define IO_EXPANDER_LED_DAC_MASK (1 << 5)
#define IO_EXPANDER_LED_ADC_MASK (1 << 4)
#define IO_EXPANDER_BUTTON_DIRECTION_MASK (1 << 3)
#define IO_EXPANDER_BUTTON_NEXT_MASK (1 << 2)
#define IO_EXPANDER_BUTTON_BACK_MASK (1 << 1)
#define IO_EXPANDER_BUTTON_ENTER_MASK (1 << 0)

// Call after initializing the shared I2C bus.
void io_expander_init(void);
// Hardware reset through ESP32 GPIO32; does not require I2C.
void reset_io_expander(void);
void set_io_expander(uint8_t mask);
void clear_io_expander(uint8_t mask);
