// Author : Paul Capgras
// Date   : Oct 10, 2025

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s_std.h"

// Bluetooth
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"

// My Bluetooth file
#include "bt_app_core.h"
#include "bt_app_a2dp.h"

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

static void init_bluetooth_controller();
static void enable_bluetooth_controller();
static void init_bluedroid_host();
static void enable_bluedroid_host();
static void set_bluetooth_pairing_parameters();
static void task__bt_msg_handler(void *arg);
static bool bt_app_send_msg(bt_app_msg_t *msg);
static void bt_app_work_dispatched(bt_app_msg_t *msg);
static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param);
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static QueueHandle_t s_bt_app_task_queue = NULL; /* handle of work queue */
static TaskHandle_t s_bt_app_task_handle = NULL; /* handle of application task  */

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/

static void init_bluetooth_controller()
{
    // We only uses the functions of Classical Bluetooth.
    // So release the controller memory for Bluetooth Low Energy.
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    /* initialize Bluetooth Controller with default configuration */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
};

static void enable_bluetooth_controller()
{
    /* enable Bluetooth Controller in Classic Bluetooth mode */
    esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
};

static void init_bluedroid_host()
{
    /* initialize Bluedroid Host */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_cfg.ssp_en = false;
    esp_err_t err = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
};

static void enable_bluedroid_host()
{
    /* enable Bluedroid Host */
    esp_err_t err = esp_bluedroid_enable();
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s enable bluedroid failed", __func__);
        ESP_ERROR_CHECK(err);
    }
};

static void set_bluetooth_pairing_parameters()
{
    /* set default parameters for Legacy Pairing (use fixed pin code 1234) */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code;
    pin_code[0] = '1';
    pin_code[1] = '2';
    pin_code[2] = '3';
    pin_code[3] = '4';
    esp_bt_gap_set_pin(pin_type, 4, pin_code);
};

// Usefull function for bt_app_work_dispatch
// Add a bluetooth message to the bluetooth message queue
static bool bt_app_send_msg(bt_app_msg_t *msg)
{
    if (msg == NULL)
    {
        return false;
    }

    /* send the message to work queue */
    if (xQueueSend(s_bt_app_task_queue, msg, 10 / portTICK_PERIOD_MS) != pdTRUE)
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s xQueue send failed", __func__);
        return false;
    }
    return true;
}

// Usefull function for task__bt_msg_handler
// call the callback function associated with the message
static void bt_app_work_dispatched(bt_app_msg_t *msg)
{
    if (msg->cb)
    {
        msg->cb(msg->event, msg->param);
    }
}

// The bluetooth dispatch function is in charge of calling the callback function associated with the message.
static void task__bt_msg_handler(void *arg)
{
    bt_app_msg_t msg;

    for (;;)
    {
        /* receive message from work queue and handle it */
        if (pdTRUE == xQueueReceive(s_bt_app_task_queue, &msg, (TickType_t)portMAX_DELAY))
        {
            ESP_LOGD(BT_APP_CORE_TAG, "%s, signal: 0x%x, event: 0x%x", __func__, msg.sig, msg.event);

            switch (msg.sig)
            {
            case BT_APP_SIG_WORK_DISPATCH:
                // if we received a message we dispatch it:
                // Dispatch = take a piece of work and send it to the right place to be handled.
                // here = calling the call back function
                bt_app_work_dispatched(&msg);
                break;
            default:
                ESP_LOGW(BT_APP_CORE_TAG, "%s, unhandled signal: %d", __func__, msg.sig);
                break;
            } /* switch (msg.sig) */

            if (msg.param)
            {
                free(msg.param);
            }
        }
    }
}

// Callback function for bluetooth HW events - device events
static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BT_DEV_NAME_RES_EVT:
    {
        if (param->name_res.status == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(BT_APP_CORE_TAG, "Get local device name success: %s", param->name_res.name);
        }
        else
        {
            ESP_LOGE(BT_APP_CORE_TAG, "Get local device name failed, status: %d", param->name_res.status);
        }
        break;
    }
    default:
    {
        ESP_LOGI(BT_APP_CORE_TAG, "event: %d", event);
        break;
    }
    }
}

// Callback function for bluetooth HW events - GAP events
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    uint8_t *bda = NULL;

    switch (event)
    {
    /* when authentication completed, this event comes */
    case ESP_BT_GAP_AUTH_CMPL_EVT:
    {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(BT_APP_CORE_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            ESP_LOG_BUFFER_HEX(BT_APP_CORE_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        }
        else
        {
            ESP_LOGE(BT_APP_CORE_TAG, "authentication failed, status: %d", param->auth_cmpl.stat);
        }
        ESP_LOGI(BT_APP_CORE_TAG, "link key type of current link is: %d", param->auth_cmpl.lk_type);
        break;
    }
    case ESP_BT_GAP_ENC_CHG_EVT:
    {
        char *str_enc[3] = {"OFF", "E0", "AES"};
        bda = (uint8_t *)param->enc_chg.bda;
        ESP_LOGI(BT_APP_CORE_TAG, "Encryption mode to [%02x:%02x:%02x:%02x:%02x:%02x] changed to %s",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], str_enc[param->enc_chg.enc_mode]);
        break;
    }

#if (CONFIG_EXAMPLE_A2DP_SINK_SSP_ENABLED == true)
    /* when Security Simple Pairing user confirmation requested, this event comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(BT_APP_CORE_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %06" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(BT_APP_CORE_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %06" PRIu32, param->key_notif.passkey);
        break;
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(BT_APP_CORE_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(BT_APP_CORE_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d, interval: %.2f ms",
                 param->mode_chg.mode, param->mode_chg.interval * 0.625);
        break;
    /* when ACL connection completed, this event comes */
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        bda = (uint8_t *)param->acl_conn_cmpl_stat.bda;
        ESP_LOGI(BT_APP_CORE_TAG, "ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT Connected to [%02x:%02x:%02x:%02x:%02x:%02x], status: 0x%x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], param->acl_conn_cmpl_stat.stat);
        break;
    /* when ACL disconnection completed, this event comes */
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        bda = (uint8_t *)param->acl_disconn_cmpl_stat.bda;
        ESP_LOGI(BT_APP_CORE_TAG, "ESP_BT_GAP_ACL_DISC_CMPL_STAT_EVT Disconnected from [%02x:%02x:%02x:%02x:%02x:%02x], reason: 0x%x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], param->acl_disconn_cmpl_stat.reason);
        break;
    /* others */
    default:
    {
        ESP_LOGI(BT_APP_CORE_TAG, "event: %d", event);
        break;
    }
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

void bt_app_init(void)
{
    init_bluetooth_controller();
    enable_bluetooth_controller();

    init_bluedroid_host();
    enable_bluedroid_host();

    set_bluetooth_pairing_parameters();
}

// Create a queue for new bluetooth messages.
// Create a task to call the call back functions of the new messages.
void bt_app_task_start_up(void)
{
    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));

    xTaskCreate(task__bt_msg_handler, "BtAppTask", 3072, NULL, 10, &s_bt_app_task_handle);
}

void bt_app_task_shut_down(void)
{
    if (s_bt_app_task_handle)
    {
        vTaskDelete(s_bt_app_task_handle);
        s_bt_app_task_handle = NULL;
    }
    if (s_bt_app_task_queue)
    {
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
    }
}

// Create a bluetooth message and add it to the queue.
bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback)
{
    ESP_LOGD(BT_APP_CORE_TAG, "%s event: 0x%x, param len: %d", __func__, event, param_len);

    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(bt_app_msg_t));

    msg.sig = BT_APP_SIG_WORK_DISPATCH;
    msg.event = event;
    msg.cb = p_cback;

    if (param_len == 0)
    {
        return bt_app_send_msg(&msg);
    }
    else if (p_params && param_len > 0)
    {
        if ((msg.param = malloc(param_len)) != NULL)
        {
            memcpy(msg.param, p_params, param_len);
            /* check if caller has provided a copy callback to do the deep copy */
            if (p_copy_cback)
            {
                p_copy_cback(msg.param, p_params, param_len);
            }
            return bt_app_send_msg(&msg);
        }
    }

    return false;
}

void bt_app_register_callback_function(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_APP_CORE_TAG, "%s event: %d", __func__, event);

    switch (event)
    {
    // This event happens when the Bluetooth stack is initialized
    // The goal is to set all the callback function for the different type of messages
    case BT_APP_EVT_REGISTER_CB_FUNCTION:
    {
        // Set the local device name
        esp_bt_gap_set_device_name("ESP32_SINK");

        // Register the device callback function (handles device-specific events like name resolution)
        esp_bt_dev_register_callback(bt_app_dev_cb);

        // Register the GAP (Generic Access Profile) callback function (handles authentication, encryption, etc.)
        esp_bt_gap_register_callback(bt_app_gap_cb);

        // Initialize and register the A2DP Sink profile (Advanced Audio Distribution Profile)
        assert(esp_a2d_sink_init() == ESP_OK);

        // Register callback for handling A2DP connection events (e.g., connection state, codec configuration)
        esp_a2d_register_callback(&bt_app_a2dp_cb);

        // Register callback for receiving audio data streamed over A2DP and processing it (e.g., forwarding to I2S)
        esp_a2d_sink_register_data_callback(bt_app_a2dp_data_cb);

        /* Get the default value of the delay value */
        esp_a2d_sink_get_delay_value();
        /* Get local device name */
        esp_bt_gap_get_device_name();

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_APP_CORE_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}
