#pragma once
#include <stddef.h>
#include <stdint.h>

void pcm_capture_request(unsigned channel);
void pcm_capture_feed(const int16_t *pcm, size_t sample_count, unsigned sample_rate);
