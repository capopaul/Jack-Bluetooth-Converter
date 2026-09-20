/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

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

// Bluetooth
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"

// Include Non Volatile Storage
#include "./drivers/nvs.h"

#include "bt_app_core.h"

/*********************************
 * STATIC FUNCTION DECLARATIONS
 ********************************/

static void bluetooth_controller_init();
static void bluetooth_controller_enable();
static void bluedroid_host_init();
static void bluedroid_host_enable();
static void set_bluetooth_pairing_parameters();
static void task__bt_msg_handler(void *arg);
static bool bt_app_send_msg(bt_app_msg_t *msg);
static void bt_app_work_dispatched(bt_app_msg_t *msg);

/*********************************
 * STATIC VARIABLE DEFINITIONS
 ********************************/

static QueueHandle_t s_bt_app_task_queue = NULL;
static TaskHandle_t s_bt_app_task_handle = NULL;

/*********************************
 * STATIC FUNCTION DEFINITIONS
 ********************************/

static void bluetooth_controller_init()
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

static void bluetooth_controller_enable()
{
    /* enable Bluetooth Controller in Classic Bluetooth mode */
    esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
};

static void bluedroid_host_init()
{
    /* initialize Bluedroid Host */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
};

static void bluedroid_host_enable()
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
    // /* set default parameters for Legacy Pairing (use fixed pin code 1234) */
    // esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    // esp_bt_pin_code_t pin_code;
    // pin_code[0] = '1';
    // pin_code[1] = '2';
    // pin_code[2] = '3';
    // pin_code[3] = '4';
    // esp_bt_gap_set_pin(pin_type, 4, pin_code);

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);
};

// Usefull function for bt_app_work_dispatch
// Add a bluetooth message to the bluetooth message queue
static bool bt_app_send_msg(bt_app_msg_t *msg)
{
    if (msg == NULL || s_bt_app_task_queue == NULL)
    {
        return false;
    }

    if (pdTRUE != xQueueSend(s_bt_app_task_queue, msg, 10 / portTICK_PERIOD_MS))
    {
        ESP_LOGE(BT_APP_CORE_TAG, "%s xQueue send failed", __func__);
        return false;
    }

    return true;
}

// Each bluetooth messages contains a callback function
// The bluetooth dispatch function is in charge of calling this callback function
// associated with any new messages.
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
                if (msg.cb)
                {
                    msg.cb(msg.event, msg.param);
                }
                break;
            default:
                ESP_LOGW(BT_APP_CORE_TAG, "%s, unhandled signal: %d", __func__, msg.sig);
                break;
            }

            if (msg.param)
            {
                if (msg.free_cb)
                {
                    msg.free_cb(msg.param);
                }
                free(msg.param);
            }
        }
    }
}

/*********************************
 * EXTERN FUNCTION DEFINITIONS
 ********************************/

void bt_app_init(void)
{
    nvs_init();

    bluetooth_controller_init();
    bluetooth_controller_enable();

    bluedroid_host_init();
    bluedroid_host_enable();

    /* set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    set_bluetooth_pairing_parameters();

    // Create the Bluetooth queue
    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));

    // Create the Queue handler task
    xTaskCreate(task__bt_msg_handler, "BtAppTask", 4096, NULL, 10, &s_bt_app_task_handle);
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
        bt_app_msg_t msg;
        while (xQueueReceive(s_bt_app_task_queue, &msg, 0) == pdTRUE)
        {
            if (msg.param)
            {
                if (msg.free_cb)
                {
                    msg.free_cb(msg.param);
                }
                free(msg.param);
            }
        }
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
    }
}

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len,
                          bt_app_copy_cb_t p_copy_cback, bt_app_free_cb_t p_free_cback)
{
    ESP_LOGD(BT_APP_CORE_TAG, "%s event: 0x%x, param len: %d", __func__, event, param_len);

    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(bt_app_msg_t));

    msg.sig = BT_APP_SIG_WORK_DISPATCH;
    msg.event = event;
    msg.cb = p_cback;
    msg.free_cb = p_free_cback;

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
            if (!bt_app_send_msg(&msg))
            {
                if (p_free_cback)
                {
                    p_free_cback(msg.param);
                }
                free(msg.param);
                return false;
            }
            return true;
        }
    }

    return false;
}
