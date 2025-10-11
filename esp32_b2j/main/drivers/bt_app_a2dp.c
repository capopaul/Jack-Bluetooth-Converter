// Author : Paul Capgras
// Date   : Oct 10, 2025

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"

// My Bluetooth file
#include "bt_app_core.h"
#include "bt_app_a2dp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

#include "sys/lock.h"

/* Application layer causes delay value */
#define APP_DELAY_VALUE 50 // 5ms
#define RINGBUF_HIGHEST_WATER_LEVEL (32 * 1024)
#define RINGBUF_PREFETCH_WATER_LEVEL (20 * 1024)

enum
{
    RINGBUFFER_MODE_PROCESSING,  /* ringbuffer is buffering incoming audio data, I2S is working */
    RINGBUFFER_MODE_PREFETCHING, /* ringbuffer is buffering incoming audio data, I2S is waiting */
    RINGBUFFER_MODE_DROPPING     /* ringbuffer is not buffering (dropping) incoming audio data, I2S is working */
};

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

static void esp_i2s_driver_install(void);
static void esp_i2s_driver_uninstall(void);
static void handle_a2dp_event(uint16_t event, void *p_param);
static void i2s_task_start_up(void);
static void i2s_task_shut_down(void);
static void task__i2s_handler(void *arg);
static size_t write_ringbuf(const uint8_t *data, size_t size);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static uint32_t s_pkt_cnt = 0; /* count for audio packet */
static esp_a2d_audio_state_t s_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
/* audio stream datapath state */
static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
/* connection state in string */
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};

static TaskHandle_t s_bt_i2s_task_handle = NULL; /* handle of I2S task */
static RingbufHandle_t s_ringbuf_i2s = NULL;     /* handle of ringbuffer for I2S */
static SemaphoreHandle_t s_i2s_write_semaphore = NULL;
static uint16_t ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;

i2s_chan_handle_t tx_chan = NULL;

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

static void esp_i2s_driver_install(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    // | pin name       | esp32_c6 | esp32 |
    // | codec_reset_l  | IO18     | IO32  |
    // | codec_i2s_mclk | IO19     | IO33  |
    // | codec_i2s_bclk | IO20     | IO25  |
    // | codec_i2s_wclk | IO21     | IO26  |
    // | codec_i2s_din  | IO22     | IO27  |
    // | codec_i2s_dout | IO23     | IO14  |

    // #define I2S_STD_CLK_DEFAULT_CONFIG(rate) {
    //     .sample_rate_hz = rate,
    //     .clk_src = I2S_CLK_SRC_DEFAULT,
    //     .ext_clk_freq_hz = 0,
    //     .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    //     .bclk_div = 8,
    // }

    // So esp_i2s_mclk should be at a frequency of rate*256.

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = 33,
            .bclk = 25,
            .ws = 26,
            .dout = 14,
            .din = 27,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* enable I2S */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

static void esp_i2s_driver_uninstall(void)
{
    ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
    ESP_ERROR_CHECK(i2s_del_channel(tx_chan));
}

static void handle_a2dp_event(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_APP_A2DP_TAG, "%s event: %d", __func__, event);

    esp_a2d_cb_param_t *a2d = NULL;

    switch (event)
    {
    /* when connection state changed, this event comes */
    case ESP_A2D_CONNECTION_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGI(BT_APP_A2DP_TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            esp_i2s_driver_uninstall();
            i2s_task_shut_down();
        }
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
        {
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            i2s_task_start_up();
        }
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING)
        {
            esp_i2s_driver_install();
        }
        break;
    }
    /* when audio stream transmission state changed, this event comes */
    case ESP_A2D_AUDIO_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_APP_A2DP_TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
        s_audio_state = a2d->audio_stat.state;
        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state)
        {
            s_pkt_cnt = 0;
        }
        break;
    }
    /* when audio codec is configured, this event comes */
    case ESP_A2D_AUDIO_CFG_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        esp_a2d_mcc_t *p_mcc = &a2d->audio_cfg.mcc;
        ESP_LOGI(BT_APP_A2DP_TAG, "A2DP audio stream configuration, codec type: %d", p_mcc->type);
        /* for now only SBC stream is supported */
        if (p_mcc->type == ESP_A2D_MCT_SBC)
        {
            int sample_rate = 16000;
            int ch_count = 2;
            if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_32K)
            {
                sample_rate = 32000;
            }
            else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_44K)
            {
                sample_rate = 44100;
            }
            else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K)
            {
                sample_rate = 48000;
            }

            if (p_mcc->cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO)
            {
                ch_count = 1;
            }

            i2s_channel_disable(tx_chan);
            i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
            i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, ch_count);
            i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
            i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg);
            i2s_channel_enable(tx_chan);

            ESP_LOGI(BT_APP_A2DP_TAG, "Configure audio player: 0x%x-0x%x-0x%x-0x%x-0x%x-%d-%d",
                     p_mcc->cie.sbc_info.samp_freq,
                     p_mcc->cie.sbc_info.ch_mode,
                     p_mcc->cie.sbc_info.block_len,
                     p_mcc->cie.sbc_info.num_subbands,
                     p_mcc->cie.sbc_info.alloc_mthd,
                     p_mcc->cie.sbc_info.min_bitpool,
                     p_mcc->cie.sbc_info.max_bitpool);
            ESP_LOGI(BT_APP_A2DP_TAG, "Audio player configured, sample rate: %d", sample_rate);
        }
        break;
    }
    /* when a2dp init or deinit completed, this event comes */
    case ESP_A2D_PROF_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (ESP_A2D_INIT_SUCCESS == a2d->a2d_prof_stat.init_state)
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "A2DP PROF STATE: Init Complete");
        }
        else
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "A2DP PROF STATE: Deinit Complete");
        }
        break;
    }
    /* when using external codec, after sep registration done, this event comes */
    case ESP_A2D_SEP_REG_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (a2d->a2d_sep_reg_stat.reg_state == ESP_A2D_SEP_REG_SUCCESS)
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "A2DP register SEP success, seid: %d", a2d->a2d_sep_reg_stat.seid);
        }
        else
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "A2DP register SEP fail, seid: %d, state: %d", a2d->a2d_sep_reg_stat.seid, a2d->a2d_sep_reg_stat.reg_state);
        }
        break;
    }
    /* When protocol service capabilities configured, this event comes */
    case ESP_A2D_SNK_PSC_CFG_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_APP_A2DP_TAG, "protocol service capabilities configured: 0x%x ", a2d->a2d_psc_cfg_stat.psc_mask);
        if (a2d->a2d_psc_cfg_stat.psc_mask & ESP_A2D_PSC_DELAY_RPT)
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "Peer device support delay reporting");
        }
        else
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "Peer device unsupported delay reporting");
        }
        break;
    }
    /* when set delay value completed, this event comes */
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (ESP_A2D_SET_INVALID_PARAMS == a2d->a2d_set_delay_value_stat.set_state)
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "Set delay report value: fail");
        }
        else
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "Set delay report value: success, delay_value: %u * 1/10 ms", a2d->a2d_set_delay_value_stat.delay_value);
        }
        break;
    }
    /* when get delay value completed, this event comes */
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_APP_A2DP_TAG, "Get delay report value: delay_value: %u * 1/10 ms", a2d->a2d_get_delay_value_stat.delay_value);
        /* Default delay value plus delay caused by application layer */
        esp_a2d_sink_set_delay_value(a2d->a2d_get_delay_value_stat.delay_value + APP_DELAY_VALUE);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_APP_A2DP_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

static void i2s_task_start_up(void)
{
    ESP_LOGI(BT_APP_A2DP_TAG, "ringbuffer data empty! mode changed: RINGBUFFER_MODE_PREFETCHING");
    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    if ((s_i2s_write_semaphore = xSemaphoreCreateBinary()) == NULL)
    {
        ESP_LOGE(BT_APP_A2DP_TAG, "%s, Semaphore create failed", __func__);
        return;
    }
    if ((s_ringbuf_i2s = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL)
    {
        ESP_LOGE(BT_APP_A2DP_TAG, "%s, ringbuffer create failed", __func__);
        return;
    }
    xTaskCreate(task__i2s_handler, "BtI2STask", 2048, NULL, configMAX_PRIORITIES - 3, &s_bt_i2s_task_handle);
}

static void i2s_task_shut_down(void)
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

static void task__i2s_handler(void *arg)
{
    uint8_t *data = NULL;
    size_t item_size = 0;
    /**
     * The total length of DMA buffer of I2S is:
     * `dma_frame_num * dma_desc_num * i2s_channel_num * i2s_data_bit_width / 8`.
     * Transmit `dma_frame_num * dma_desc_num` bytes to DMA is trade-off.
     */
    const size_t item_size_upto = 240 * 6;
    size_t bytes_written = 0;

    for (;;)
    {
        if (pdTRUE == xSemaphoreTake(s_i2s_write_semaphore, portMAX_DELAY))
        {
            for (;;)
            {
                item_size = 0;
                /* receive data from ringbuffer and write it to I2S DMA transmit buffer */
                data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, (TickType_t)pdMS_TO_TICKS(20), item_size_upto);
                if (item_size == 0)
                {
                    ESP_LOGI(BT_APP_A2DP_TAG, "ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING");
                    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
                    break;
                }
                // i2s_channel_write(tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
                vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
            }
        }
    }
}

static size_t write_ringbuf(const uint8_t *data, size_t size)
{
    size_t item_size = 0;
    BaseType_t done = pdFALSE;

    if (ringbuffer_mode == RINGBUFFER_MODE_DROPPING)
    {
        ESP_LOGW(BT_APP_A2DP_TAG, "ringbuffer is full, drop this packet!");
        vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL)
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "ringbuffer data decreased! mode changed: RINGBUFFER_MODE_PROCESSING");
            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        return 0;
    }

    done = xRingbufferSend(s_ringbuf_i2s, (void *)data, size, (TickType_t)0);

    if (!done)
    {
        ESP_LOGW(BT_APP_A2DP_TAG, "ringbuffer overflowed, ready to decrease data! mode changed: RINGBUFFER_MODE_DROPPING");
        ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
    }

    if (ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING)
    {
        vRingbufferGetInfo(s_ringbuf_i2s, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL)
        {
            ESP_LOGI(BT_APP_A2DP_TAG, "ringbuffer data increased! mode changed: RINGBUFFER_MODE_PROCESSING");
            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
            if (pdFALSE == xSemaphoreGive(s_i2s_write_semaphore))
            {
                ESP_LOGE(BT_APP_A2DP_TAG, "semphore give failed");
            }
        }
    }

    return done ? size : 0;
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

// Callback function for bluetooth HW events - A2DP events
void bt_app_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event)
    {
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_PROF_STATE_EVT:
    case ESP_A2D_SEP_REG_STATE_EVT:
    case ESP_A2D_SNK_PSC_CFG_EVT:
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
    {
        bt_app_work_dispatch(handle_a2dp_event, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_APP_A2DP_TAG, "Invalid A2DP event: %d", event);
        break;
    }
}

// Callback function for bluetooth HW events - A2DP data events
void bt_app_a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    write_ringbuf(data, len);

    /* log the number every 100 packets */
    if (++s_pkt_cnt % 100 == 0)
    {
        ESP_LOGI(BT_APP_A2DP_TAG, "Audio packet count: %" PRIu32, s_pkt_cnt);
    }
}
