// Author : Paul Capgras
// Date   : Oct 10, 2025

#include <stdint.h>
#include <assert.h>
#include "audio_codec.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
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
#include "bt_app_a2dp_sink.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

#include "i2s.h"

#include "sys/lock.h"

/* Application layer causes delay value */
#define APP_DELAY_VALUE 50 // 5ms
#define BT_A2DP "BT_A2DP_SINK"

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

static void register_a2dp_sink_callback_function(uint16_t event, void *p_param);
static void bt_app_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
static void bt_app_a2dp_data_cb(const uint8_t *data, uint32_t len);
static void handle_a2dp_event(uint16_t event, void *p_param);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static uint32_t s_pkt_cnt = 0; /* count for audio packet */
static esp_a2d_audio_state_t s_audio_state = ESP_A2D_AUDIO_STATE_SUSPEND;
/* audio stream datapath state */
static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
/* connection state in string */
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

static void register_a2dp_sink_callback_function(uint16_t event, void *p_param)
{
    // both parameters : event and p_param are ignored.

    // Initialize and register the A2DP Sink profile (Advanced Audio Distribution Profile)
    esp_err_t err = esp_a2d_sink_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_A2DP, "esp_a2d_sink_init failed with code %x", err);
    }

    // Register callback for handling A2DP connection events (e.g., connection state, codec configuration)
    err = esp_a2d_register_callback(&bt_app_a2dp_cb);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_A2DP, "esp_a2d_register_callback failed with code %x", err);
    }

    // Register callback for receiving audio data streamed over A2DP and processing it (e.g., forwarding to I2S)
    err = esp_a2d_sink_register_data_callback(bt_app_a2dp_data_cb);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_A2DP, "esp_a2d_sink_register_data_callback failed with code %x", err);
    }

    /* Get the default value of the delay value */
    err = esp_a2d_sink_get_delay_value();
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_A2DP, "esp_a2d_sink_get_delay_value failed with code %x", err);
    }

    /* Get local device name */
    err = esp_bt_gap_get_device_name();
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_A2DP, "esp_bt_gap_get_device_name failed with code %x", err);
    }

    /* set discoverable and connectable mode, wait to be connected */
    err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_A2DP, "esp_bt_gap_set_scan_mode failed with code %x", err);
    }
}

// Callback function for bluetooth HW events - A2DP events
// Create a new bluetooth message
static void bt_app_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
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
        bt_app_work_dispatch(handle_a2dp_event, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL);
        break;
    }
    default:
        ESP_LOGE(BT_A2DP, "Invalid A2DP event: %d", event);
        break;
    }
}

// Callback function for bluetooth HW events - A2DP data events
// Write data to ring buffer
static void bt_app_a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    audio_i2s_write_ringbuf(data, len);

    /* log the number every 100 packets */
    if (++s_pkt_cnt % 100 == 0)
    {
        ESP_LOGI(BT_A2DP, "Audio packet count: %" PRIu32, s_pkt_cnt);
    }
}

static void handle_a2dp_event(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_A2DP, "%s event: %d", __func__, event);

    esp_a2d_cb_param_t *a2d = NULL;

    switch (event)
    {
    /* when connection state changed, this event comes */
    case ESP_A2D_CONNECTION_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGI(BT_A2DP, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            i2s_driver_uninstall();
        }
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
        {
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        }
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING)
        {
            // i2s_driver_install(I2S_MODE_TX);
        }
        break;
    }
    /* when audio stream transmission state changed, this event comes */
    case ESP_A2D_AUDIO_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_A2DP, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
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
        ESP_LOGI(BT_A2DP, "A2DP audio stream configuration, codec type: %d", p_mcc->type);
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

            // TBD Support i2s reconfiguration
            ESP_LOGW(BT_A2DP, "Reconfiguration of I2S not supported yet.");

            // i2s_channel_disable(tx_chan);
            // i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
            // i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg);
            // i2s_channel_enable(tx_chan);

            // ESP_LOGI(BT_A2DP, "Configure audio player: 0x%x-0x%x-0x%x-0x%x-0x%x-%d-%d",
            //          p_mcc->cie.sbc_info.samp_freq,
            //          p_mcc->cie.sbc_info.ch_mode,
            //          p_mcc->cie.sbc_info.block_len,
            //          p_mcc->cie.sbc_info.num_subbands,
            //          p_mcc->cie.sbc_info.alloc_mthd,
            //          p_mcc->cie.sbc_info.min_bitpool,
            //          p_mcc->cie.sbc_info.max_bitpool);
            // ESP_LOGI(BT_A2DP, "Audio player configured, sample rate: %d", sample_rate);
        }
        break;
    }
    /* when a2dp init or deinit completed, this event comes */
    case ESP_A2D_PROF_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (ESP_A2D_INIT_SUCCESS == a2d->a2d_prof_stat.init_state)
        {
            ESP_LOGI(BT_A2DP, "A2DP PROF STATE: Init Complete");
        }
        else
        {
            ESP_LOGI(BT_A2DP, "A2DP PROF STATE: Deinit Complete");
        }
        break;
    }
    /* when using external codec, after sep registration done, this event comes */
    case ESP_A2D_SEP_REG_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (a2d->a2d_sep_reg_stat.reg_state == ESP_A2D_SEP_REG_SUCCESS)
        {
            ESP_LOGI(BT_A2DP, "A2DP register SEP success, seid: %d", a2d->a2d_sep_reg_stat.seid);
        }
        else
        {
            ESP_LOGI(BT_A2DP, "A2DP register SEP fail, seid: %d, state: %d", a2d->a2d_sep_reg_stat.seid, a2d->a2d_sep_reg_stat.reg_state);
        }
        break;
    }
    /* When protocol service capabilities configured, this event comes */
    case ESP_A2D_SNK_PSC_CFG_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_A2DP, "protocol service capabilities configured: 0x%x ", a2d->a2d_psc_cfg_stat.psc_mask);
        if (a2d->a2d_psc_cfg_stat.psc_mask & ESP_A2D_PSC_DELAY_RPT)
        {
            ESP_LOGI(BT_A2DP, "Peer device support delay reporting");
        }
        else
        {
            ESP_LOGI(BT_A2DP, "Peer device unsupported delay reporting");
        }
        break;
    }
    /* when set delay value completed, this event comes */
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (ESP_A2D_SET_INVALID_PARAMS == a2d->a2d_set_delay_value_stat.set_state)
        {
            ESP_LOGI(BT_A2DP, "Set delay report value: fail");
        }
        else
        {
            ESP_LOGI(BT_A2DP, "Set delay report value: success, delay_value: %u * 1/10 ms", a2d->a2d_set_delay_value_stat.delay_value);
        }
        break;
    }
    /* when get delay value completed, this event comes */
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_A2DP, "Get delay report value: delay_value: %u * 1/10 ms", a2d->a2d_get_delay_value_stat.delay_value);
        /* Default delay value plus delay caused by application layer */
        esp_a2d_sink_set_delay_value(a2d->a2d_get_delay_value_stat.delay_value + APP_DELAY_VALUE);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_A2DP, "%s unhandled event: %d", __func__, event);
        break;
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

esp_err_t bt_app_a2dp_sink_start(void)
{
    if (!bt_app_work_dispatch(register_a2dp_sink_callback_function, 0, NULL, 0, NULL, NULL))
    {
        ESP_LOGE(BT_A2DP, "failed to dispatch Bluetooth stack initialization");
        bt_app_task_shut_down();
        return ESP_FAIL;
    }

    return ESP_OK;
}
