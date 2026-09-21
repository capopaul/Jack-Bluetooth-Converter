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
#include "sbc_sink.h"

#include "sys/lock.h"

/* Application layer causes delay value */
#define APP_DELAY_VALUE 50 // 5ms
#define BT_A2DP "BT_A2DP_SINK"

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

static void register_a2dp_sink_callback_function(uint16_t event, void *p_param);
static void bt_app_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

static void handle_a2dp_event(uint16_t event, void *p_param);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static bool s_sbc_configured;
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
    (void)event;
    (void)p_param;
    ESP_ERROR_CHECK(esp_a2d_register_callback(bt_app_a2dp_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_register_audio_data_callback(sbc_sink_receive));
    ESP_ERROR_CHECK(esp_a2d_sink_init());
    // Endpoint registration follows ESP_A2D_INIT_SUCCESS below.
}

static void register_sbc_endpoint(void)
{
    // Based on IDF v6.1 a2dp_sink_stream's external-codec example.
    // Advertise only formats supported by the current 44.1 kHz stereo DAC path.
    esp_a2d_mcc_t mcc = {0};
    mcc.type = ESP_A2D_MCT_SBC;
    mcc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_44K;
    mcc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL |
                               ESP_A2D_SBC_CIE_CH_MODE_STEREO |
                               ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    mcc.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_4 |
                                 ESP_A2D_SBC_CIE_BLOCK_LEN_8 |
                                 ESP_A2D_SBC_CIE_BLOCK_LEN_12 |
                                 ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    mcc.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 |
                                    ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    mcc.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR |
                                  ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    mcc.cie.sbc_info.min_bitpool = 2;
    mcc.cie.sbc_info.max_bitpool = 53;
    ESP_ERROR_CHECK(esp_a2d_sink_register_stream_endpoint(0, &mcc));
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
            s_audio_state = ESP_A2D_AUDIO_STATE_SUSPEND;
            s_sbc_configured = false;
            sbc_sink_set_playing(false);
            // I2S is installed once by main; keep it available for reconnection.
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
        if (s_audio_state != ESP_A2D_AUDIO_STATE_STARTED)
        {
            sbc_sink_set_playing(false);
        }
        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state)
        {
            sbc_sink_set_playing(s_sbc_configured);
        }
        break;
    }
    /* when audio codec is configured, this event comes */
    case ESP_A2D_AUDIO_CFG_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        esp_a2d_mcc_t *p_mcc = &a2d->audio_cfg.mcc;
        ESP_LOGI(BT_A2DP, "A2DP audio stream configuration, codec type: %d", p_mcc->type);
        s_sbc_configured = p_mcc->type == ESP_A2D_MCT_SBC &&
            p_mcc->cie.sbc_info.samp_freq == ESP_A2D_SBC_CIE_SF_44K &&
            (p_mcc->cie.sbc_info.ch_mode == ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL ||
             p_mcc->cie.sbc_info.ch_mode == ESP_A2D_SBC_CIE_CH_MODE_STEREO ||
             p_mcc->cie.sbc_info.ch_mode == ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO);
        sbc_sink_set_playing(s_sbc_configured && s_audio_state == ESP_A2D_AUDIO_STATE_STARTED);
        if (!s_sbc_configured)
        {
            ESP_LOGE(BT_A2DP, "Unsupported sink configuration: expected SBC 44100 Hz stereo");
        }
        else
        {
            ESP_LOGI(BT_A2DP, "SBC decoder configured: 44100 Hz stereo -> 16-bit PCM");
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
            register_sbc_endpoint();
            ESP_ERROR_CHECK(esp_a2d_sink_get_delay_value());
        }
        else
        {
            ESP_LOGI(BT_A2DP, "A2DP PROF STATE: Deinit Complete");
            s_audio_state = ESP_A2D_AUDIO_STATE_SUSPEND;
            s_sbc_configured = false;
            sbc_sink_set_playing(false);
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
            ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));
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
    esp_err_t err = sbc_sink_init();
    if (err != ESP_OK) return err;
    if (!bt_app_work_dispatch(register_a2dp_sink_callback_function, 0, NULL, 0, NULL, NULL))
    {
        ESP_LOGE(BT_A2DP, "failed to dispatch Bluetooth stack initialization");
        bt_app_task_shut_down();
        return ESP_FAIL;
    }

    return ESP_OK;
}
