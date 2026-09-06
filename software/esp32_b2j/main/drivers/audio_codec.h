// Author : Paul Capgras
// Date   : Oct 6, 2025

#include "io_expander.h"

void reset_audio_codec(void);

// Poll power readiness after the I2S clock has started (up to 500 ms).
void audio_codec_check_power_ready(void);
