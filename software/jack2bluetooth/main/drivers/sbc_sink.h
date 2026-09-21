#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_a2dp_api.h"

esp_err_t sbc_sink_init(void);
// Called from the application task; resets decoding and clears old PCM.
void sbc_sink_set_playing(bool playing);
// Takes ownership of audio_buf, including when playback is stopped or full.
void sbc_sink_receive(esp_a2d_conn_hdl_t conn_hdl, esp_a2d_audio_buff_t *audio_buf);
