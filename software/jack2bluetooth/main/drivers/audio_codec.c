// Author : Paul Capgras
// Date   : Oct 6, 2025

// to configure and control GPIO pins
#include "driver/gpio.h"

#include "driver/i2c_master.h"
#include "i2c.h"
#include "esp_log.h"

#include "audio_codec.h"
#include "io_expander.h"

// Provides essential definitions for task management and delays
#include "freertos/FreeRTOS.h"

// Provide functions for task management and delays
#include "freertos/task.h"

void audio_codec_reset(void)
{
    io_expander_clear(IO_EXPANDER_CODEC_RESET_L_MASK);
    vTaskDelay(pdMS_TO_TICKS(10));
    io_expander_set(IO_EXPANDER_CODEC_RESET_L_MASK);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void audio_codec_sw_reset(void)
{
    // Register 1 - SW reset
    i2c_set(CODEC_ADDR, 1, 0x80);
}

void audio_codec_configure_pll(void)
{
    // Register 3 - PLL Programming Register A
    // D7   - 1    - PLL is enabled
    // D6-3 - 0000 - Q (ignored here)
    // D2-0 - 001  - P = 1
    i2c_set(CODEC_ADDR, 3, 0b10000001);
    is_expected(CODEC_TAG, 3, i2c_get(CODEC_ADDR, 3), 0b10000001);

    // Register 4 - PLL Programming Register B
    // D7-2 - 1000 00 - Set J to 32
    // D1-0 - 00      - Reserved
    i2c_set(CODEC_ADDR, 4, 0b10000000);
    is_expected(CODEC_TAG, 4, i2c_get(CODEC_ADDR, 4), 0b10000000);

    // Register 5 and 6
    // Set D to 0
    // MSB D7-0 (reg 5) 0
    // LSB D7-2 (reg 6) 0
    // D1-0             0
    // always write both, and in the order reg5 then reg6.
    is_expected(CODEC_TAG, 5, i2c_get(CODEC_ADDR, 5), 0b00000000);
    is_expected(CODEC_TAG, 6, i2c_get(CODEC_ADDR, 6), 0b00000000);

    // Register 11 - Audio Codec Overflow Flag Register
    // D7-4 - 0    - Ignore
    // D3-0 - 0001 - Set R to 1
    i2c_set(CODEC_ADDR, 11, 0b00000001);
    is_expected(CODEC_TAG, 11, i2c_get(CODEC_ADDR, 11), 0b00000001);

    // Register 101 - Clock register
    // D7-1 - 0
    // D0   - 0 - CODEC_CLKIN uses PLLDIV_OUT
    i2c_set(CODEC_ADDR, 101, 0b00000000);
    is_expected(CODEC_TAG, 101, i2c_get(CODEC_ADDR, 101), 0b00000000);

    // Register 102 - Clock Generation Control Register
    // D7-6 - 0    -
    // D5-4 - 10   - PLLCLK_IN uses BCLK
    // D3-0 - 0010 - Reserved
    i2c_set(CODEC_ADDR, 102, 0b00100010);
    is_expected(CODEC_TAG, 102, i2c_get(CODEC_ADDR, 102), 0b00100010);
}

void audio_codec_configure_i2s_settings(void)
{
    // Register 2 - default is good
    // Register 8 - default is good
    // Register 9 - default is good
    // Register 10 - default is good

    // Register 7 - Codec Data-Path Setup Register
    // D7   - 1  - Set fs=44.1kHz
    // D6-5 - 00
    // D4-3 - 00
    // D2-1 - 00
    // D0   - 0
    // 1000 0000
    i2c_set(CODEC_ADDR, 7, 0b10000000);
    is_expected(CODEC_TAG, 7, i2c_get(CODEC_ADDR, 7), 0b10000000);
}

void audio_codec_connect_line2_to_adc()
{
    // Register 17 - MIC2L/R to Left-ADC Control Register
    // D7-4 - 0000 - Connect LINE2L to left-ADC PGA - Input level control gain = 0 dB
    // D3-0 - 1111 - LINE2R is not connected to the left-ADC PGA
    // 0000 1111
    i2c_set(CODEC_ADDR, 17, 0b00001111);
    is_expected(CODEC_TAG, 17, i2c_get(CODEC_ADDR, 17), 0b00001111);

    // Register 18 - MIC2/LINE2 to Right-ADC Control Register
    // D7-4 - 1111 - LINE2L is not connected to the right-ADC PGA
    // D3-0 - 0000 - Connect LINE2R to right-ADC PGA - Input level control gain = 0 dB
    // 1111 0000
    i2c_set(CODEC_ADDR, 18, 0b11110000);
    is_expected(CODEC_TAG, 18, i2c_get(CODEC_ADDR, 18), 0b11110000);
}

void audio_codec_power_up_adc(void)
{
    // Register 19 - Power up Left-ADC
    // D7   - 0
    // D6-3 - 1111
    // D2   - 1    - Left-ADC channel is powered up
    // D1-0 - 00
    // 0111 1100
    i2c_set(CODEC_ADDR, 19, 0b01111100);
    is_expected(CODEC_TAG, 19, i2c_get(CODEC_ADDR, 19), 0b01111100);

    // Register 22 - Power up right-ADC
    // D7   - 0
    // D6-3 - 1111
    // D2   - 1    - Right-ADC channel is powered up
    // D1-0 - 00
    // 0111 1100
    i2c_set(CODEC_ADDR, 22, 0b01111100);
    is_expected(CODEC_TAG, 22, i2c_get(CODEC_ADDR, 22), 0b01111100);
}

void audio_codec_unmute_adc()
{
    // Register 15 - Left-ADC PGA Gain Control Register
    // D7   - 0 - The left-ADC PGA is not muted.
    // D6-0 - 0
    i2c_set(CODEC_ADDR, 15, 0b00000000);
    is_expected(CODEC_TAG, 15, i2c_get(CODEC_ADDR, 15), 0b00000000);

    // Register 16 - Right-ADC PGA Gain Control Register
    // D7   - 0 - The right-ADC PGA is not muted.
    // D6-0 - 0
    i2c_set(CODEC_ADDR, 16, 0b00000000);
    is_expected(CODEC_TAG, 16, i2c_get(CODEC_ADDR, 16), 0b00000000);
}

void audio_codec_check_power_ready(void)
{
    uint8_t status = 0;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        status = i2c_get(0x18, 94);
        // 0xff is also the current I2C helper's error return.
        if (status != 0xff && (status & 0xc6) == 0xc6)
        {
            ESP_LOGI("AUDIO_CODEC", "DACs and headphone outputs powered up");
            return;
        }
    }
    ESP_LOGW("AUDIO_CODEC", "Power readiness timed out: register 94 = 0x%02x", status);
}
