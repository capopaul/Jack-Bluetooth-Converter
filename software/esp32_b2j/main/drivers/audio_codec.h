// Author : Paul Capgras
// Date   : Oct 6, 2025

#define AUDIO_CODEC_RESETN_GPIO 32
#define AUDIO_CODEC_RESETN_MASK (1ULL << AUDIO_CODEC_RESETN_GPIO)

void reset_audio_codec(void);
