// Author : Paul Capgras
// Date   : Sept 19, 2026

#include "i2s.h"
#include "audio_codec.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include "freertos/stream_buffer.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"

#define BUFF_SIZE 512
#define I2S_PCM_CHUNK_BYTES (240 * 6)
#define I2S_TAG "I2S"
#define RINGBUF_HIGHEST_WATER_LEVEL (64 * 1024)
#define RINGBUF_PREFETCH_WATER_LEVEL (48 * 1024)
typedef struct
{
    bool have_low_byte;
    uint8_t low_byte;
} pcm_conversion_state_t;

enum
{
    RINGBUFFER_MODE_PROCESSING,  /* ringbuffer is buffering incoming audio data, I2S is working */
    RINGBUFFER_MODE_PREFETCHING, /* ringbuffer is buffering incoming audio data, I2S is waiting */
    RINGBUFFER_MODE_DROPPING     /* ringbuffer is not buffering (dropping) incoming audio data, I2S is working */
};

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

static void task__i2s_rx(void *arg);
static void task__i2s_tx(void *arg);
static void i2s_rx_task_start_up(void);
static void i2s_tx_task_start_up(void);
static void i2s_rx_task_shut_down(void);
static void i2s_tx_task_shut_down(void);
static void i2s_init_rx();
static void i2s_init_tx();
static void i2s_deinit_rx();
static void i2s_deinit_tx();
static void write_pcm16_to_i2s(const uint8_t *data, size_t size, pcm_conversion_state_t *state);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

// Stream for rx task
static StreamBufferHandle_t pcm_stream;

static TaskHandle_t s_bt_i2s_task_handle = NULL; /* handle of I2S task */
static RingbufHandle_t s_ringbuf_i2s = NULL;     /* handle of ringbuffer for I2S */
static SemaphoreHandle_t s_i2s_write_semaphore = NULL;
static SemaphoreHandle_t tx_mutex;
static uint16_t ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;

static is2_mode_t mode;
static i2s_chan_handle_t rx_chan = NULL;
static i2s_chan_handle_t tx_chan = NULL;

static i2s_std_clk_config_t clk_cfg = {
    .sample_rate_hz = 44100,
    .clk_src = I2S_CLK_SRC_DEFAULT,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    .bclk_div = 8};

// So MCLK = 11.2896 MHz

static i2s_std_slot_config_t slot_cfg = {
    // Original ESP32 requires matching data and slot widths.
    .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
    .slot_mode = I2S_SLOT_MODE_STEREO,
    .slot_mask = I2S_STD_SLOT_BOTH,
    .ws_width = 32,
    .ws_pol = false,
    .bit_shift = true,
    .msb_right = false};

// So BCLK = 2.822 MHz

static i2s_std_gpio_config_t gpio_cfg = {
    .mclk = I2S_GPIO_MCLK,
    .bclk = I2S_GPIO_BCLK,
    .ws = I2S_GPIO_WCLK,
    .dout = I2S_GPIO_DOUT,
    .din = I2S_GPIO_DIN,
    .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
    }};

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

static void task__i2s_rx(void *arg)
{
    // Keep capture and conversion buffers off the task's 4096-byte stack.
    uint32_t *samples = malloc(BUFF_SIZE * sizeof(*samples));
    int16_t *pcm = malloc(BUFF_SIZE * sizeof(*pcm));
    if (samples == NULL || pcm == NULL)
    {
        ESP_LOGE("I2S", "Failed to allocate capture buffers");
        free(samples);
        free(pcm);
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(
            rx_chan,
            samples,
            BUFF_SIZE * sizeof(*samples),
            &bytes_read,
            1000 // Timeout in milliseconds
        );

        // A timeout may still return valid partial data.
        if (err == ESP_OK || err == ESP_ERR_TIMEOUT)
        {
            size_t sample_count = bytes_read / sizeof(samples[0]);
            for (size_t i = 0; i < sample_count; ++i)
            {
                // Codec sends 16-bit samples in the upper half of each
                // 32-bit I2S word. Preserve interleaved L, R ordering.
                pcm[i] = (int16_t)(samples[i] >> 16);
            }
            // pcm[0..sample_count-1] is ready for the future PCM buffer.
            // No Bluetooth handoff yet; this block is overwritten next read.
            size_t pcm_bytes = sample_count * sizeof(int16_t);

            if (pcm_bytes > 0 &&
                sample_count % 2 == 0 &&
                xStreamBufferSpacesAvailable(pcm_stream) >= pcm_bytes)
            {
                xStreamBufferSend(pcm_stream, pcm, pcm_bytes, 0);
            }
        }
        else
        {
            ESP_LOGE("I2S", "Read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    vTaskDelete(NULL);
}

static void task__i2s_tx(void *arg)
{
    uint8_t *data = NULL;
    size_t item_size = 0;
    /**
     * The total length of DMA buffer of I2S is:
     * `dma_frame_num * dma_desc_num * i2s_channel_num * i2s_data_bit_width / 8`.
     * Transmit `dma_frame_num * dma_desc_num` bytes to DMA is trade-off.
     */
    const size_t item_size_upto = I2S_PCM_CHUNK_BYTES;
    pcm_conversion_state_t pcm_state = {0};

    for (;;)
    {
        if (pdTRUE == xSemaphoreTake(s_i2s_write_semaphore, portMAX_DELAY))
        {
            for (;;)
            {
                xSemaphoreTake(tx_mutex, portMAX_DELAY);
                if (ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING)
                {
                    pcm_state = (pcm_conversion_state_t){0};
                    xSemaphoreGive(tx_mutex);
                    break;
                }
                item_size = 0;
                /* receive data from ringbuffer and write it to I2S DMA transmit buffer */
                data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, 0, item_size_upto);
                if (item_size == 0)
                {
                    ESP_LOGI(I2S_TAG, "ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING");
                    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
                    xSemaphoreGive(tx_mutex);
                    break;
                }
                write_pcm16_to_i2s(data, item_size, &pcm_state);
                vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
                xSemaphoreGive(tx_mutex);
            }
        }
    }
}

static void i2s_rx_task_start_up(void)
{ // Pulse-Code Modulate (PCM) stream buffer
    pcm_stream = xStreamBufferCreate(8192, 1);
    configASSERT(pcm_stream != NULL);

    xTaskCreate(task__i2s_rx, "i2s_rx_task", 4096, NULL, 5, NULL);
}

// Create the data ring and the task to empty it
static void i2s_tx_task_start_up(void)
{
    ESP_LOGI(I2S_TAG, "ringbuffer data empty! mode changed: RINGBUFFER_MODE_PREFETCHING");
    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    if ((s_i2s_write_semaphore = xSemaphoreCreateBinary()) == NULL)
    {
        ESP_LOGE(I2S_TAG, "%s, Semaphore create failed", __func__);
        return;
    }
    if ((s_ringbuf_i2s = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL)
    {
        ESP_LOGE(I2S_TAG, "%s, ringbuffer create failed", __func__);
        return;
    }
    tx_mutex = xSemaphoreCreateMutex();
    configASSERT(tx_mutex != NULL);
    // This task handles the new data added to the ring.
    xTaskCreate(task__i2s_tx, "i2s_tx_task", 8192, NULL, configMAX_PRIORITIES - 3, &s_bt_i2s_task_handle);
}

static void i2s_rx_task_shut_down(void)
{
    // TBD end the rx task and the stream
}

static void i2s_tx_task_shut_down(void)
{
    if (s_bt_i2s_task_handle)
    {
        vTaskDelete(s_bt_i2s_task_handle);
        s_bt_i2s_task_handle = NULL;
    }
    if (s_ringbuf_i2s)
    {
        vRingbufferDelete(s_ringbuf_i2s);
        s_ringbuf_i2s = NULL;
    }
    if (s_i2s_write_semaphore)
    {
        vSemaphoreDelete(s_i2s_write_semaphore);
        s_i2s_write_semaphore = NULL;
    }
}

static void i2s_init_rx()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg};

    /* enable I2S */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    i2s_rx_task_start_up();
}

static void i2s_init_tx()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg};

    /* enable I2S */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    i2s_tx_task_start_up();
}

static void i2s_deinit_rx()
{
    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
    ESP_ERROR_CHECK(i2s_del_channel(rx_chan));
    rx_chan = NULL;
    i2s_rx_task_shut_down();
}

static void i2s_deinit_tx()
{
    ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
    ESP_ERROR_CHECK(i2s_del_channel(tx_chan));
    tx_chan = NULL;
    i2s_tx_task_shut_down();
}

// Usefull task to task__i2s_tx
// Accept up to I2S_PCM_CHUNK_BYTES of PCM; state persists between chunks.
static void write_pcm16_to_i2s(const uint8_t *data, size_t size, pcm_conversion_state_t *state)
{
    uint32_t samples_32[I2S_PCM_CHUNK_BYTES / 2];
    assert(size <= I2S_PCM_CHUNK_BYTES);
    // Bluetooth PCM is little-endian, signed 16-bit. Preserve its
    // bit pattern in the upper 16 bits of each 32-bit I2S word.
    // Keep a trailing byte across ring-buffer boundaries if needed.
    size_t sample_count = 0;
    for (size_t i = 0; i < size; ++i)
    {
        if (!state->have_low_byte)
        {
            state->low_byte = data[i];
            state->have_low_byte = true;
        }
        else
        {
            uint32_t sample = (uint32_t)state->low_byte | ((uint32_t)data[i] << 8);
            samples_32[sample_count++] = sample << 16;
            state->have_low_byte = false;
        }
    }
    size_t output_size = sample_count * sizeof(samples_32[0]);
    size_t offset = 0;
    while (offset < output_size)
    {
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(tx_chan,
                                          (const uint8_t *)samples_32 + offset, output_size - offset,
                                          &bytes_written, portMAX_DELAY);
        offset += bytes_written;
        if (err != ESP_OK || bytes_written == 0)
        {
            ESP_LOGE(I2S_TAG, "I2S write failed: %s, sent %u/%u bytes",
                     esp_err_to_name(err), (unsigned)offset, (unsigned)output_size);
            break;
        }
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

void i2s_driver_install(is2_mode_t new_mode)
{
    if (new_mode == I2S_MODE_RX)
    {
        i2s_init_rx();
    }
    else
    {
        i2s_init_tx();
    }
    mode = new_mode;
}

void i2s_driver_uninstall(void)
{
    if (mode == I2S_MODE_RX)
    {
        i2s_deinit_rx();
    }
    else
    {
        i2s_deinit_tx();
    }
}

size_t audio_i2s_read_pcm(void *buffer, size_t bytes)
{
    return xStreamBufferReceive(
        pcm_stream,
        buffer,
        bytes,
        pdMS_TO_TICKS(100));
}

static size_t write_ringbuf_locked(const uint8_t *data, size_t size)
{
    size_t item_size = 0;
    BaseType_t done = pdFALSE;

    if (ringbuffer_mode == RINGBUFFER_MODE_DROPPING)
    {
        ESP_LOGW(I2S_TAG, "ringbuffer is full, drop this packet!");
        vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL)
        {
            ESP_LOGI(I2S_TAG, "ringbuffer data decreased! mode changed: RINGBUFFER_MODE_PROCESSING");
            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        return 0;
    }

    done = xRingbufferSend(s_ringbuf_i2s, (void *)data, size, (TickType_t)0);

    if (!done)
    {
        ESP_LOGW(I2S_TAG, "ringbuffer overflowed, ready to decrease data! mode changed: RINGBUFFER_MODE_DROPPING");
        ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
    }

    if (ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING)
    {
        vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL)
        {
            ESP_LOGI(I2S_TAG, "ringbuffer data increased! mode changed: RINGBUFFER_MODE_PROCESSING");
            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
            if (pdFALSE == xSemaphoreGive(s_i2s_write_semaphore))
            {
                ESP_LOGE(I2S_TAG, "semphore give failed");
            }
        }
    }

    return done ? size : 0;
}

size_t audio_i2s_write_ringbuf(const uint8_t *data, size_t size)
{
    if (!tx_mutex || !s_ringbuf_i2s || !data || size == 0) return 0;
    xSemaphoreTake(tx_mutex, portMAX_DELAY);
    size_t written = write_ringbuf_locked(data, size);
    xSemaphoreGive(tx_mutex);
    return written;
}

void audio_i2s_flush_tx(void)
{
    if (!tx_mutex) return;
    xSemaphoreTake(tx_mutex, portMAX_DELAY);
    size_t size;
    void *data;
    while ((data = xRingbufferReceive(s_ringbuf_i2s, &size, 0)) != NULL)
    {
        vRingbufferReturnItem(s_ringbuf_i2s, data);
    }
    xSemaphoreTake(s_i2s_write_semaphore, 0);
    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    xSemaphoreGive(tx_mutex);
}
