// Author : Paul Capgras
// Date   : Sept 19, 2026

#pragma once

#include <stdint.h>
#include <stddef.h>

// | pin name       | esp32 |
// | codec_i2s_mclk | TXD0  | (it was supposed to be IO0... PCB error...)
// | codec_i2s_bclk | IO2   |
// | codec_i2s_wclk | IO5   |
// | codec_i2s_din  | IO4   | So IO18 for esp32
// | codec_i2s_dout | IO18  | So IO4  for esp32
#define I2S_GPIO_MCLK I2S_GPIO_UNUSED
#define I2S_GPIO_BCLK GPIO_NUM_2
#define I2S_GPIO_WCLK GPIO_NUM_5
#define I2S_GPIO_DIN GPIO_NUM_18
#define I2S_GPIO_DOUT GPIO_NUM_4

typedef enum
{
    I2S_MODE_TX,
    I2S_MODE_RX
} is2_mode_t;

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void i2s_driver_install(is2_mode_t mode);
void i2s_driver_uninstall(void);

// Returns the number of PCM bytes received; may be less than requested.
size_t audio_i2s_read_pcm(void *buffer, size_t bytes);

size_t audio_i2s_write_ringbuf(const uint8_t *data, size_t size);

// Discard queued TX audio on stream changes; I2S stays installed.
void audio_i2s_flush_tx(void);
