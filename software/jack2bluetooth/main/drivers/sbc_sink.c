// SBC playback for ESP-IDF v6.1 external-codec A2DP.
// The IDF example supplies encoded buffers; esp_audio_codec decodes the frames.
#include "sbc_sink.h"
#include "i2s.h"
#include "esp_sbc_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>

#define TAG "SBC_SINK"
#define PACKET_QUEUE_LENGTH 24
// One SBC frame: at most 16 blocks * 8 subbands * 2 channels * 2 bytes.
#define PCM_FRAME_BYTES 512

typedef struct
{
    esp_a2d_audio_buff_t *buffer;
    unsigned int generation;
} sbc_packet_t;

static QueueHandle_t packet_queue;
static SemaphoreHandle_t decoder_mutex;
static void *decoder;
// Even generations are stopped, odd generations are playing.
static atomic_uint stream_generation;

static void decode_packet(esp_a2d_audio_buff_t *buffer)
{
    esp_audio_dec_in_raw_t raw = {
        .buffer = buffer->data,
        .len = buffer->data_len,
    };
    int16_t pcm[PCM_FRAME_BYTES / sizeof(int16_t)];
    unsigned int frames = 0;

    // IDF has already removed the RTP and SBC media payload headers.
    while (raw.len > 0 && frames < buffer->number_frame)
    {
        esp_audio_dec_out_frame_t out = {
            .buffer = (uint8_t *)pcm,
            .len = sizeof(pcm),
        };
        esp_audio_dec_info_t info = {0};
        raw.consumed = 0;
        esp_audio_err_t err = esp_sbc_dec_decode(decoder, &raw, &out, &info);
        if (err != ESP_AUDIO_ERR_OK || raw.consumed == 0 || raw.consumed > raw.len)
        {
            ESP_LOGW(TAG, "Invalid SBC frame: decoder status %d", err);
            esp_sbc_dec_reset(decoder);
            return;
        }
        if (info.sample_rate != 44100 || info.channel != 2 || info.bits_per_sample != 16 ||
            out.decoded_size > sizeof(pcm) || out.decoded_size % 4 != 0)
        {
            ESP_LOGE(TAG, "Unsupported PCM format: %lu Hz, %u channels, %u bits",
                     (unsigned long)info.sample_rate, info.channel, info.bits_per_sample);
            return;
        }
        audio_i2s_write_ringbuf(out.buffer, out.decoded_size);
        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;
        frames++;
    }
    if (raw.len != 0 || frames != buffer->number_frame)
    {
        ESP_LOGW(TAG, "SBC packet frame count/length mismatch");
        esp_sbc_dec_reset(decoder);
    }
}

static void task__sbc_decode(void *arg)
{
    sbc_packet_t packet;
    for (;;)
    {
        xQueueReceive(packet_queue, &packet, portMAX_DELAY);
        xSemaphoreTake(decoder_mutex, portMAX_DELAY);
        unsigned int generation = atomic_load(&stream_generation);
        if ((generation & 1U) && packet.generation == generation)
        {
            decode_packet(packet.buffer);
        }
        xSemaphoreGive(decoder_mutex);
        esp_a2d_audio_buff_free(packet.buffer);
    }
}

esp_err_t sbc_sink_init(void)
{
    if (packet_queue != NULL)
    {
        return ESP_OK;
    }
    decoder_mutex = xSemaphoreCreateMutex();
    packet_queue = xQueueCreate(PACKET_QUEUE_LENGTH, sizeof(sbc_packet_t));
    esp_sbc_dec_cfg_t config = ESP_SBC_DEC_CONFIG_DEFAULT();
    config.enable_plc = false;
    if (!decoder_mutex || !packet_queue ||
        esp_sbc_dec_open(&config, sizeof(config), &decoder) != ESP_AUDIO_ERR_OK)
    {
        goto fail;
    }
    if (xTaskCreate(task__sbc_decode, "sbc_decode", 4096, NULL, 5, NULL) != pdPASS)
    {
        goto fail;
    }
    return ESP_OK;

fail:
    if (decoder) esp_sbc_dec_close(decoder);
    if (packet_queue) vQueueDelete(packet_queue);
    if (decoder_mutex) vSemaphoreDelete(decoder_mutex);
    decoder = NULL;
    packet_queue = NULL;
    decoder_mutex = NULL;
    return ESP_ERR_NO_MEM;
}

void sbc_sink_set_playing(bool playing)
{
    if (!decoder_mutex) return;
    xSemaphoreTake(decoder_mutex, portMAX_DELAY);
    unsigned int generation = atomic_load(&stream_generation);
    // Invalidate buffers from the previous stream, including an in-flight packet.
    atomic_store(&stream_generation, (generation + 2U) & ~1U);
    sbc_packet_t packet;
    while (xQueueReceive(packet_queue, &packet, 0) == pdTRUE)
    {
        esp_a2d_audio_buff_free(packet.buffer);
    }
    esp_audio_err_t err = esp_sbc_dec_reset(decoder);
    audio_i2s_flush_tx();
    if (playing && err == ESP_AUDIO_ERR_OK)
    {
        atomic_fetch_or(&stream_generation, 1U);
    }
    xSemaphoreGive(decoder_mutex);
}

void sbc_sink_receive(esp_a2d_conn_hdl_t conn_hdl, esp_a2d_audio_buff_t *audio_buf)
{
    (void)conn_hdl;
    if (!audio_buf) return;
    sbc_packet_t packet = {
        .buffer = audio_buf,
        .generation = atomic_load(&stream_generation),
    };
    // Never block the Bluetooth callback. The worker frees accepted buffers.
    if (!packet_queue || !(packet.generation & 1U) || !audio_buf->data ||
        !audio_buf->data_len || !audio_buf->number_frame ||
        xQueueSend(packet_queue, &packet, 0) != pdTRUE)
    {
        esp_a2d_audio_buff_free(audio_buf);
    }
}
