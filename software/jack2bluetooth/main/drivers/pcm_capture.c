#include "pcm_capture.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// 64 KiB by default. Set to 88200 for two seconds if sufficient heap is available.
#define CAPTURE_SAMPLES 32768
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool busy;
static int16_t *buffer;
static size_t captured;
static unsigned selected_channel, rate;

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static void capture_task(void *arg)
{
    int16_t *data = malloc(CAPTURE_SAMPLES * sizeof(*data));
    if (!data) {
        ESP_LOGE("PCM_CAPTURE", "Cannot allocate %u bytes; reduce CAPTURE_SAMPLES", (unsigned)(CAPTURE_SAMPLES * sizeof(*data)));
        goto cleanup;
    }
    portENTER_CRITICAL(&lock);
    captured = 0;
    rate = 0;
    buffer = data;
    portEXIT_CRITICAL(&lock);

    TickType_t start = xTaskGetTickCount();
    size_t count;
    do {
        vTaskDelay(pdMS_TO_TICKS(20));
        portENTER_CRITICAL(&lock);
        count = captured;
        portEXIT_CRITICAL(&lock);
    } while (count < CAPTURE_SAMPLES && xTaskGetTickCount() - start < pdMS_TO_TICKS(5000));

    // Detach under the same lock used by the RX task before reading/freeing data.
    portENTER_CRITICAL(&lock);
    buffer = NULL;
    count = captured;
    portEXIT_CRITICAL(&lock);
    if (count != CAPTURE_SAMPLES) {
        ESP_LOGE("PCM_CAPTURE", "Capture timed out (%u samples). Use source mode with ADC unmuted.", (unsigned)count);
        goto cleanup;
    }

    // One stdio call per framed line; ordinary console logs can appear between lines.
    // Hex avoids UART console CR/LF translation corrupting binary PCM.
    printf("\n@PCM_BEGIN %u %u %u\n", rate, selected_channel, (unsigned)count);
    const uint8_t *bytes = (const uint8_t *)data; // ESP32 is little endian.
    const size_t length = count * sizeof(*data);
    const char hex[] = "0123456789abcdef";
    for (size_t offset = 0; offset < length; offset += 128) {
        size_t n = length - offset < 128 ? length - offset : 128;
        char encoded[257];
        for (size_t i = 0; i < n; ++i) {
            encoded[2*i] = hex[bytes[offset+i] >> 4];
            encoded[2*i+1] = hex[bytes[offset+i] & 15];
        }
        encoded[2*n] = '\0';
        printf("@PCM_DATA %u %s\n", (unsigned)offset, encoded);
        vTaskDelay(1);
    }
    printf("@PCM_END %08lx\n", (unsigned long)crc32(bytes, length));
    fflush(stdout);
cleanup:
    free(data);
    portENTER_CRITICAL(&lock);
    busy = false;
    portEXIT_CRITICAL(&lock);
    vTaskDelete(NULL);
}

void pcm_capture_request(unsigned channel)
{
    portENTER_CRITICAL(&lock);
    bool available = !busy && channel < 2;
    if (available) {
        busy = true;
        selected_channel = channel;
    }
    portEXIT_CRITICAL(&lock);
    if (!available) {
        ESP_LOGW("PCM_CAPTURE", "Capture busy or invalid channel");
        return;
    }
    if (xTaskCreate(capture_task, "pcm_capture", 3072, NULL, 1, NULL) != pdPASS) {
        portENTER_CRITICAL(&lock);
        busy = false;
        portEXIT_CRITICAL(&lock);
        ESP_LOGE("PCM_CAPTURE", "Cannot create capture task");
    }
}

void pcm_capture_feed(const int16_t *pcm, size_t sample_count, unsigned sample_rate)
{
    portENTER_CRITICAL(&lock);
    if (buffer && sample_count % 2 == 0) {
        rate = sample_rate;
        for (size_t i = selected_channel; i < sample_count && captured < CAPTURE_SAMPLES; i += 2)
            buffer[captured++] = pcm[i];
    }
    portEXIT_CRITICAL(&lock);
}
