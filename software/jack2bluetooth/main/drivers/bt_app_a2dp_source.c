/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "bt_app_core.h"
#include "decode_task.h"
#include "bt_app_a2dp_source.h"
#include "bt_app_gap.h"

/* log tags */
#define A2DP_TAG "BT_A2DP_SOURCE"
#define BT_RC_CT_TAG "RC_CT"

/* AVRCP used transaction label */
#define APP_RC_CT_TL_GET_CAPS (0)
#define APP_RC_CT_TL_RN_VOLUME_CHANGE (1)

enum
{
    BT_APP_HEART_BEAT_EVT = 0xff00 /* event for heart beat */
};

/* A2DP global states */
typedef enum
{
    APP_AV_STATE_IDLE,
    APP_AV_STATE_DISCOVERING,
    APP_AV_STATE_UNCONNECTED,
    APP_AV_STATE_CONNECTING,
    APP_AV_STATE_CONNECTED,
    APP_AV_STATE_DISCONNECTING,
} bt_app_a2dp_state_t;

/* sub states of APP_AV_STATE_CONNECTED */
enum
{
    APP_AV_MEDIA_STATE_IDLE,
    APP_AV_MEDIA_STATE_STARTING,
    APP_AV_MEDIA_STATE_STARTED,
    APP_AV_MEDIA_STATE_STOPPING,
};

/*********************************
 * STATIC FUNCTION DECLARATIONS
 ********************************/

static void register_a2dp_source_callback_function(uint16_t event, void *p_param);

static void a2dp_cb_1(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
static void a2dp_cb_2(uint16_t event, void *param);
static void a2dp_cb_3(const uint8_t *address);

static void avrcp_controller_cb_1(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
static void avrcp_controller_cb_2(uint16_t event, void *p_param);

static void avrcp_volume_changed(void);
static void avrcp_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter);

static void bt_app_a2d_heart_beat(TimerHandle_t arg);

static void handler_idle(uint16_t event, void *param);
static void handler_unconnected(uint16_t event, void *param);
static void handler_connecting(uint16_t event, void *param);
static void handler_connected(uint16_t event, void *param);
static void handler_disconnected(uint16_t event, void *param);

/*********************************
 * STATIC VARIABLE DEFINITIONS
 ********************************/

static esp_bd_addr_t verified_address = {0};               /* Selected peer for connection and reconnection. */
static bt_app_a2dp_state_t a2dp_state = APP_AV_STATE_IDLE; /* A2DP global state */
static int s_media_state = APP_AV_MEDIA_STATE_IDLE;        /* sub states of APP_AV_STATE_CONNECTED */
static int s_connecting_intv = 0;                          /* count of heart beat intervals for connecting */
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;      /* AVRC target notification event capability bit mask */
static TimerHandle_t s_tmr;                                /* handle of heart beat timer */
static esp_a2d_conn_hdl_t s_a2d_conn_hndl = 0;
static uint16_t s_a2d_audio_mtu;

/*********************************
 * STATIC FUNCTION DEFINITIONS
 ********************************/

static void bt_app_encode_stream_stop(void)
{
    esp_err_t err = encode_task_stop();

    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "AAC shutdown failed: %s",
                 esp_err_to_name(err));
        // Do not proceed with deleting resources AAC could still use.
    }
}

static void bt_app_register_a2dp_src_seps(void)
{
    esp_a2d_mcc_t mcc_aac = {0};
    mcc_aac.type = ESP_A2D_MCT_M24;
    mcc_aac.cie.m24_info.drc = ESP_A2D_M24_CIE_DRC_NS;
    mcc_aac.cie.m24_info.obj_type = ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC |
                                    ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC;
    mcc_aac.cie.m24_info.samp_freq1 = ESP_A2D_M24_CIE_SF1_8K |
                                      ESP_A2D_M24_CIE_SF1_11K |
                                      ESP_A2D_M24_CIE_SF1_12K |
                                      ESP_A2D_M24_CIE_SF1_16K |
                                      ESP_A2D_M24_CIE_SF1_22K |
                                      ESP_A2D_M24_CIE_SF1_24K |
                                      ESP_A2D_M24_CIE_SF1_32K |
                                      ESP_A2D_M24_CIE_SF1_44K;
    mcc_aac.cie.m24_info.samp_freq2 = ESP_A2D_M24_CIE_SF2_48K |
                                      ESP_A2D_M24_CIE_SF2_64K |
                                      ESP_A2D_M24_CIE_SF2_88K |
                                      ESP_A2D_M24_CIE_SF2_96K;
    mcc_aac.cie.m24_info.ch = ESP_A2D_M24_CIE_CH_1 |
                              ESP_A2D_M24_CIE_CH_2;
    mcc_aac.cie.m24_info.vbr = ESP_A2D_M24_CIE_VBR_SUPPORT;
    /* bit rate: 160000 */
    mcc_aac.cie.m24_info.br1 = 0x02 & ESP_A2D_M24_CIE_BR1_MSK;
    mcc_aac.cie.m24_info.br2 = 0x71 & ESP_A2D_M24_CIE_BR2_MSK;
    mcc_aac.cie.m24_info.br3 = 0x00 & ESP_A2D_M24_CIE_BR3_MSK;
    esp_err_t err = esp_a2d_source_register_stream_endpoint(0, &mcc_aac);
    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_a2d_source_register_stream_endpoint_aac failed with code %x", err);
    }
    else
    {
        ESP_LOGI(A2DP_TAG, "esp_a2d_source_register_stream_endpoint_aac completed successfully.");
    }

    esp_a2d_mcc_t mcc_sbc = {0};
    mcc_sbc.type = ESP_A2D_MCT_SBC;
    mcc_sbc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_16K |
                                     ESP_A2D_SBC_CIE_SF_32K |
                                     ESP_A2D_SBC_CIE_SF_44K |
                                     ESP_A2D_SBC_CIE_SF_48K;
    mcc_sbc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_MONO |
                                   ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL |
                                   ESP_A2D_SBC_CIE_CH_MODE_STEREO |
                                   ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    mcc_sbc.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_4 |
                                     ESP_A2D_SBC_CIE_BLOCK_LEN_8 |
                                     ESP_A2D_SBC_CIE_BLOCK_LEN_12 |
                                     ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    mcc_sbc.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 | ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    mcc_sbc.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR | ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    mcc_sbc.cie.sbc_info.min_bitpool = 2;
    mcc_sbc.cie.sbc_info.max_bitpool = 250;
    err = esp_a2d_source_register_stream_endpoint(1, &mcc_sbc);
    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_a2d_source_register_stream_endpoint_sbc failed with code %x", err);
    }
    else
    {
        ESP_LOGI(A2DP_TAG, "esp_a2d_source_register_stream_endpoint_sbc completed successfully.");
    }
}

// This function is called by task : "bt_app"
static void register_a2dp_source_callback_function(uint16_t event, void *p_param)
{
    // both parameters : event and p_param are ignored.

    esp_err_t err = esp_avrc_ct_register_callback(avrcp_controller_cb_1);
    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_avrc_ct_register_callback failed with code %x", err);
        return;
    }
    err = esp_avrc_ct_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_avrc_ct_init failed with code %x", err);
        return;
    }

    /* Register before asynchronous init so its completion event cannot be lost.
     * That event registers the endpoints and starts GAP discovery. */
    err = esp_a2d_register_callback(a2dp_cb_1);
    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_a2d_register_callback failed with code %x", err);
        return;
    }
    err = esp_a2d_source_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_a2d_source_init failed with code %x", err);
        return;
    }

    //
    // This has not much to do with A2DP source
    //

    /* Avoid the state error of a2dp_state caused by the connection initiated by the peer device. */
    err = esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    if (err != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_bt_gap_set_scan_mode failed with code %x", err);
    }
}

// callback function for A2DP
// this function is called by task : "BTC_TASK"
// The processing is not done here to keep it fast.
// It creates a message to the task "bt_app"
static void a2dp_cb_1(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    if (event == ESP_A2D_PROF_STATE_EVT)
    {
        ESP_LOGI(A2DP_TAG,
                 "A2DP profile state: %u",
                 (unsigned)param->a2d_prof_stat.init_state);

        if (param->a2d_prof_stat.init_state ==
            ESP_A2D_INIT_SUCCESS)
        {
            ESP_LOGI(A2DP_TAG,
                     "A2DP initialized; registering endpoints");

            bt_app_register_a2dp_src_seps();
        }
    }
    if (!bt_app_work_dispatch(a2dp_cb_2, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL))
    {
        ESP_LOGE(A2DP_TAG, "Failed to dispatch A2DP event: %d", event);
    }
}

// this function is called by task "bt_app"
static void a2dp_cb_2(uint16_t event, void *param)
{
    /* select handler according to different states */
    switch (a2dp_state)
    {
    case APP_AV_STATE_IDLE:
        handler_idle(event, param);
        break;
    case APP_AV_STATE_DISCOVERING:
        break;
    case APP_AV_STATE_UNCONNECTED:
        handler_unconnected(event, param);
        break;
    case APP_AV_STATE_CONNECTING:
        handler_connecting(event, param);
        break;
    case APP_AV_STATE_CONNECTED:
        handler_connected(event, param);
        break;
    case APP_AV_STATE_DISCONNECTING:
        handler_disconnected(event, param);
        break;
    default:
        ESP_LOGW(A2DP_TAG, "%s invalid state: %d", __func__, a2dp_state);
        break;
    }
}

// this function is called by GAP from task : "bt_app"
static void a2dp_cb_3(const uint8_t *address)
{
    memcpy(verified_address, address, sizeof(verified_address));

    if (esp_a2d_source_connect(verified_address) != ESP_OK)
    {
        ESP_LOGE(A2DP_TAG, "esp_a2d_source_connect failed.");
        a2dp_state = APP_AV_STATE_UNCONNECTED;
        return;
    }
    s_connecting_intv = 0;
    a2dp_state = APP_AV_STATE_CONNECTING;
}

static void bt_app_a2d_heart_beat(TimerHandle_t arg)
{
    bt_app_work_dispatch(a2dp_cb_2, BT_APP_HEART_BEAT_EVT, NULL, 0, NULL, NULL);
}

// this function is called by task "bt_app"
static void handler_idle(uint16_t event, void *p_param)
{
    esp_a2d_cb_param_t *param = (esp_a2d_cb_param_t *)(p_param);

    if (event == ESP_A2D_PROF_STATE_EVT && (param->a2d_prof_stat.init_state ==
                                            ESP_A2D_INIT_SUCCESS))
    {

        // Call GAP to start discovery
        if (bt_app_gap_start_discovery(a2dp_cb_3) != ESP_OK)
        {
            return;
        }

        ESP_LOGI(A2DP_TAG, "Starting device discovery...");
        a2dp_state = APP_AV_STATE_DISCOVERING;

        /* create and start heart beat timer */
        do
        {
            s_tmr = xTimerCreate("connTmr", (10000 / portTICK_PERIOD_MS),
                                 pdTRUE, NULL, bt_app_a2d_heart_beat);
            if (s_tmr == NULL)
            {
                ESP_LOGE(A2DP_TAG, "Failed to create A2DP timer");
            }
            else if (xTimerStart(s_tmr, 0) != pdPASS)
            {
                ESP_LOGE(A2DP_TAG, "Failed to start A2DP timer");
                xTimerDelete(s_tmr, 0);
                s_tmr = NULL;
            }
        } while (0);
    }
    return;
}

// this function is called by task "bt_app"
static void handler_unconnected(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;
    /* handle the events of interest in unconnected state */
    switch (event)
    {
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        break;
    case BT_APP_HEART_BEAT_EVT:
    {
        uint8_t *bda = verified_address;
        ESP_LOGI(A2DP_TAG, "a2dp connecting to peer: %02x:%02x:%02x:%02x:%02x:%02x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        if (esp_a2d_source_connect(verified_address) != ESP_OK)
        {
            ESP_LOGE(A2DP_TAG, "esp_a2d_source_connect failed.");
            a2dp_state = APP_AV_STATE_UNCONNECTED;
            return;
        }

        a2dp_state = APP_AV_STATE_CONNECTING;
        s_connecting_intv = 0;
        break;
    }
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(A2DP_TAG, "%s, delay value: %u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    default:
    {
        ESP_LOGE(A2DP_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

// this function is called by task "bt_app"
static void handler_connecting(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in connecting state */
    switch (event)
    {
    case ESP_A2D_CONNECTION_STATE_EVT:
    {
        ESP_LOGW(A2DP_TAG, "ESP_A2D_CONNECTION_STATE_EVT");
        a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
        {
            ESP_LOGI(A2DP_TAG, "a2dp connected");
            a2dp_state = APP_AV_STATE_CONNECTED;
            s_media_state = APP_AV_MEDIA_STATE_IDLE;
            s_a2d_conn_hndl = a2d->conn_stat.conn_hdl;
            s_a2d_audio_mtu = a2d->conn_stat.audio_mtu;
        }
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            ESP_LOGI(A2DP_TAG, "a2dp disconnected");
            bt_app_encode_stream_stop();
            s_media_state = APP_AV_MEDIA_STATE_IDLE;
            a2dp_state = APP_AV_STATE_UNCONNECTED;
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGW(A2DP_TAG, "ESP_A2D_AUDIO_STATE_EVT");
        break;
    case ESP_A2D_AUDIO_CFG_EVT:
    {
        ESP_LOGW(A2DP_TAG, "ESP_A2D_AUDIO_CFG_EVT");
        a2d = (esp_a2d_cb_param_t *)(param);
        esp_a2d_mcc_t *p_mcc = &a2d->audio_cfg.mcc;
        if (p_mcc == NULL)
        {
            ESP_LOGE(A2DP_TAG, "p_mcc null");
            break;
        }
        ESP_LOGI(A2DP_TAG, "A2DP audio stream configuration, codec type: %d", p_mcc->type);
        if (p_mcc->type == ESP_A2D_MCT_M24)
        {
            ESP_LOGI(A2DP_TAG, "Configure audio player: 0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x",
                     p_mcc->cie.m24_info.drc,
                     p_mcc->cie.m24_info.obj_type,
                     p_mcc->cie.m24_info.samp_freq1,
                     p_mcc->cie.m24_info.samp_freq2,
                     p_mcc->cie.m24_info.ch,
                     p_mcc->cie.m24_info.vbr,
                     p_mcc->cie.m24_info.br1,
                     p_mcc->cie.m24_info.br2,
                     p_mcc->cie.m24_info.br3);
            encode_task_set_stream_config(p_mcc);
        }
        break;
    }
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        ESP_LOGW(A2DP_TAG, "ESP_A2D_MEDIA_CTRL_ACK_EVT");
        break;
    case BT_APP_HEART_BEAT_EVT:
        ++s_connecting_intv;

        ESP_LOGW(A2DP_TAG,
                 "A2DP connection still pending after %d seconds",
                 s_connecting_intv * 10);
        break;
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
    {
        ESP_LOGW(A2DP_TAG, "ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT");
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(A2DP_TAG, "%s, delay value: %u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    case ESP_A2D_REPORT_SNK_ALL_CODEC_CAPS_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        uint8_t n = a2d->a2d_report_snk_all_codec_caps_stat.sep_num;
        ESP_LOGI(A2DP_TAG, "%s all sink caps conn_hdl=%u sep_num=%u", __func__,
                 (unsigned)a2d->a2d_report_snk_all_codec_caps_stat.conn_hdl, (unsigned)n);
        break;
    }
    default:
        ESP_LOGE(A2DP_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

// this function is called by task "bt_app"
static void bt_app_av_media_proc(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    switch (s_media_state)
    {
    case APP_AV_MEDIA_STATE_IDLE:
    {
        if (event == BT_APP_HEART_BEAT_EVT)
        {
            ESP_LOGI(A2DP_TAG, "a2dp media ready checking ...");
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
        }
        else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT)
        {
            a2d = (esp_a2d_cb_param_t *)(param);
            if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
                a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
            {
                ESP_LOGI(A2DP_TAG, "a2dp media ready, starting ...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                s_media_state = APP_AV_MEDIA_STATE_STARTING;
            }
        }
        break;
    }
    case APP_AV_MEDIA_STATE_STARTING:
    {
        if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT)
        {
            a2d = (esp_a2d_cb_param_t *)(param);
            if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START &&
                a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
            {
                ESP_LOGI(A2DP_TAG, "a2dp media start successfully.");
                s_media_state = APP_AV_MEDIA_STATE_STARTED;
                encode_task_set_a2dp_link(s_a2d_conn_hndl, s_a2d_audio_mtu);
                if (!encode_task_has_stream_config())
                {
                    ESP_LOGE(A2DP_TAG, "No negotiated AAC (M24) config, skip encode launch");
                }
                else if (encode_task_launch() != ESP_OK)
                {
                    ESP_LOGE(A2DP_TAG, "encode_task_launch failed");
                }
            }
            else
            {
                /* not started successfully, transfer to idle state */
                ESP_LOGI(A2DP_TAG, "a2dp media start failed.");
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
            }
        }
        break;
    }
    case APP_AV_MEDIA_STATE_STOPPING:
    {
        if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT)
        {
            a2d = (esp_a2d_cb_param_t *)(param);
            if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_SUSPEND &&
                a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
            {
                ESP_LOGI(A2DP_TAG, "a2dp media suspend successfully, disconnecting...");
                bt_app_encode_stream_stop();
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
                esp_a2d_source_disconnect(verified_address);
                a2dp_state = APP_AV_STATE_DISCONNECTING;
            }
            else
            {
                ESP_LOGI(A2DP_TAG, "a2dp media suspending...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
            }
        }
        break;
    }
    default:
    {
        break;
    }
    }
}

// this function is called by task "bt_app"
static void handler_connected(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in connected state */
    switch (event)
    {
    case ESP_A2D_CONNECTION_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            ESP_LOGI(A2DP_TAG, "a2dp disconnected");
            bt_app_encode_stream_stop();
            s_media_state = APP_AV_MEDIA_STATE_IDLE;
            a2dp_state = APP_AV_STATE_UNCONNECTED;
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        if (ESP_A2D_AUDIO_STATE_SUSPEND == a2d->audio_stat.state)
        {
            ESP_LOGI(A2DP_TAG, "a2dp audio suspended");
            bt_app_encode_stream_stop();
            if (s_media_state == APP_AV_MEDIA_STATE_STARTED ||
                s_media_state == APP_AV_MEDIA_STATE_STOPPING)
            {
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
            }
        }
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        esp_a2d_mcc_t *p_mcc = &a2d->audio_cfg.mcc;
        if (p_mcc == NULL)
        {
            ESP_LOGE(A2DP_TAG, "p_mcc null");
            break;
        }
        ESP_LOGI(A2DP_TAG, "A2DP audio stream configuration, codec type: %d", p_mcc->type);
        if (p_mcc->type == ESP_A2D_MCT_M24)
        {
            ESP_LOGI(A2DP_TAG, "Configure audio player: 0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x",
                     p_mcc->cie.m24_info.drc,
                     p_mcc->cie.m24_info.obj_type,
                     p_mcc->cie.m24_info.samp_freq1,
                     p_mcc->cie.m24_info.samp_freq2,
                     p_mcc->cie.m24_info.ch,
                     p_mcc->cie.m24_info.vbr,
                     p_mcc->cie.m24_info.br1,
                     p_mcc->cie.m24_info.br2,
                     p_mcc->cie.m24_info.br3);
            encode_task_set_stream_config(p_mcc);
        }
        break;
    }
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
    case BT_APP_HEART_BEAT_EVT:
    {
        bt_app_av_media_proc(event, param);
        break;
    }
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(A2DP_TAG, "%s, delay value: %u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    case ESP_A2D_REPORT_SNK_CODEC_CAPS_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        esp_a2d_mcc_t *sink_mcc = &a2d->a2d_report_snk_codec_caps_stat.mcc;
        ESP_LOGI(A2DP_TAG, "sink codec type: %d", sink_mcc->type);
        if (sink_mcc->type == ESP_A2D_MCT_SBC)
        {
            ESP_LOGI(A2DP_TAG, "sink codec capabilities: 0x%x-0x%x-0x%x-0x%x-0x%x-%d-%d",
                     sink_mcc->cie.sbc_info.samp_freq,
                     sink_mcc->cie.sbc_info.ch_mode,
                     sink_mcc->cie.sbc_info.block_len,
                     sink_mcc->cie.sbc_info.num_subbands,
                     sink_mcc->cie.sbc_info.alloc_mthd,
                     sink_mcc->cie.sbc_info.min_bitpool,
                     sink_mcc->cie.sbc_info.max_bitpool);
        }
        else if (sink_mcc->type == ESP_A2D_MCT_M24)
        {
            ESP_LOGI(A2DP_TAG, "sink codec capabilities: 0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x-0x%x",
                     sink_mcc->cie.m24_info.obj_type,
                     sink_mcc->cie.m24_info.drc,
                     sink_mcc->cie.m24_info.samp_freq1,
                     sink_mcc->cie.m24_info.samp_freq2,
                     sink_mcc->cie.m24_info.ch,
                     sink_mcc->cie.m24_info.vbr,
                     sink_mcc->cie.m24_info.br1,
                     sink_mcc->cie.m24_info.br2,
                     sink_mcc->cie.m24_info.br3);
        }
        break;
    }
    default:
    {
        ESP_LOGE(A2DP_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

// this function is called by task "bt_app"
static void handler_disconnected(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in disconnecing state */
    switch (event)
    {
    case ESP_A2D_CONNECTION_STATE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            ESP_LOGI(A2DP_TAG, "a2dp disconnected");
            bt_app_encode_stream_stop();
            s_media_state = APP_AV_MEDIA_STATE_IDLE;
            a2dp_state = APP_AV_STATE_UNCONNECTED;
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
    case BT_APP_HEART_BEAT_EVT:
        break;
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
    {
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(A2DP_TAG, "%s, delay value: 0x%u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    default:
    {
        ESP_LOGE(A2DP_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

// callback function for AVRCP controller
// this function is called by task : "BTC_TASK"
// The processing is not done here to keep it fast.
// It creates a message to the task "bt_app"
static void avrcp_controller_cb_1(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event)
    {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:
    case ESP_AVRC_CT_PROF_STATE_EVT:
    {
        bt_app_work_dispatch(avrcp_controller_cb_2, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL, NULL);
        break;
    }
    default:
    {
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
    }
}

// this function is called by task "bt_app"
static void avrcp_controller_cb_2(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_RC_CT_TAG, "%s evt %d", __func__, event);
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);

    switch (event)
    {
    /* when connection state changed, this event comes */
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    {
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGI(BT_RC_CT_TAG, "AVRC conn_state event: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        if (rc->conn_stat.connected)
        {
            esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);
        }
        else
        {
            s_avrc_peer_rn_cap.bits = 0;
        }
        break;
    }
    /* when passthrough responded, this event comes */
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC passthrough response: key_code 0x%x, key_state %d, rsp_code %d", rc->psth_rsp.key_code,
                 rc->psth_rsp.key_state, rc->psth_rsp.rsp_code);
        break;
    }
    /* when notification changed, this event comes */
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC event notification: %d", rc->change_ntf.event_id);
        avrcp_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
        break;
    }
    /* when indicate feature of remote device, this event comes */
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC remote features %" PRIx32 ", TG features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);
        break;
    }
    /* when get supported notification events capability of peer device, this event comes */
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    {
        ESP_LOGI(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count,
                 rc->get_rn_caps_rsp.evt_set.bits);
        s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;

        avrcp_volume_changed();
        break;
    }
    /* when set absolute volume responded, this event comes */
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:
    {
        ESP_LOGI(BT_RC_CT_TAG, "Set absolute volume response: volume %d", rc->set_volume_rsp.volume);
        break;
    }
    /* when avrcp controller init or deinit completed, this event comes */
    case ESP_AVRC_CT_PROF_STATE_EVT:
    {
        if (ESP_AVRC_INIT_SUCCESS == rc->avrc_ct_init_stat.state)
        {
            ESP_LOGI(BT_RC_CT_TAG, "AVRCP CT STATE: Init Complete");
        }
        else
        {
            ESP_LOGE(BT_RC_CT_TAG, "AVRCP CT STATE error: %d", rc->avrc_ct_init_stat.state);
        }
        break;
    }
    /* other */
    default:
    {
        ESP_LOGE(BT_RC_CT_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

static void avrcp_volume_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_VOLUME_CHANGE))
    {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_VOLUME_CHANGE, ESP_AVRC_RN_VOLUME_CHANGE, 0);
    }
}

static void avrcp_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)
{
    switch (event_id)
    {
    /* when volume changed locally on target, this event comes */
    case ESP_AVRC_RN_VOLUME_CHANGE:
    {
        ESP_LOGI(BT_RC_CT_TAG, "Volume changed: %d", event_parameter->volume);
        ESP_LOGI(BT_RC_CT_TAG, "Set absolute volume: volume %d", event_parameter->volume + 5);
        esp_avrc_ct_send_set_absolute_volume_cmd(APP_RC_CT_TL_RN_VOLUME_CHANGE, event_parameter->volume + 5);
        avrcp_volume_changed();
        break;
    }
    /* other */
    default:
        break;
    }
}

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void bt_app_a2dp_source_start(void)
{
    if (!bt_app_work_dispatch(register_a2dp_source_callback_function, 0, NULL, 0, NULL, NULL))
    {
        ESP_LOGE(A2DP_TAG, "failed to dispatch Bluetooth stack initialization");
        bt_app_task_shut_down();
        abort();
    }
}
