// Author : Paul Capgras
// Date   : Oct 6, 2025

#include "io_expander.h"

#define CODEC_ADDR 0x18
#define CODEC_TAG "AUDIO_CODEC"

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

// HW Codec reset uses I2C through the initialized IO expander.
void audio_codec_reset(void);

// SW Codec reset using codec registers
void audio_codec_sw_reset(void);

// Configure the required PLL registers
void audio_codec_configure_pll(void);

void audio_codec_configure_i2s_settings(void);

void audio_codec_connect_line2_to_adc(void);

void audio_codec_power_up_adc(void);

void audio_codec_unmute_adc(void);

// Poll power readiness after the I2S clock has started (up to 500 ms).
void audio_codec_check_power_ready(void);
