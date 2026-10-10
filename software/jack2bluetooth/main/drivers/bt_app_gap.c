
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "bt_app_gap.h"
#include "bt_app_core.h"

#define GAP_TAG "BT_APP_GAP"
#define BT_APP_GAP_DISCOVERY_RESULT_EVT 0x100

static const char remote_device_name[] = "Bose-Paul-2";

static esp_bd_addr_t verified_address = {0}; /* Bluetooth Device Address of peer device*/

static bool discovery_running = false;                      // True when discovery is running
static bool target_found = false;                           // True when a device matching remote_device_name is found.
static bt_app_gap_device_found_cb_t device_found_cb = NULL; // Function to use when a device is found
static bool gap_start_requested = false;
static bool gap_callback_registered = false;
static esp_bd_addr_t remote_name_address = {0};
static bt_app_gap_remote_name_cb_t remote_name_cb = NULL;
static bt_app_gap_discovered_device_t discovered_devices[BT_APP_GAP_MAX_DISCOVERED_DEVICES];
static size_t discovered_device_count;
static SemaphoreHandle_t discovered_devices_mutex;
static bt_app_gap_device_list_changed_cb_t device_list_changed_cb;

typedef struct
{
    esp_bd_addr_t address;
    char name[BT_APP_GAP_DEVICE_NAME_MAX_LEN];
    int8_t rssi;
    uint32_t class_of_device;
} discovery_result_t;

/********************************
 * STATIC FUNCTION DECLARATIONS
 *******************************/

static char *bda2str(const uint8_t *bda, char *str, size_t size);
static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, size_t bdname_size, uint8_t *bdname_len);
static bool discovery_result_matches_target(const discovery_result_t *result);
static void parse_discovery_result(const esp_bt_gap_cb_param_t *param, discovery_result_t *result);
static bool update_discovered_device_list(const discovery_result_t *result, size_t *updated_index);
static void clear_discovered_device_list(void);
static void notify_device_list_changed(void);

static void gap_cb_1(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void gap_cb_2(uint16_t event, void *p_param);

static void register_gap_callback_function(uint16_t event, void *p_param);

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

/* utils for transfer BLuetooth Deveice Address into string form */
static char *bda2str(const uint8_t *bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18)
    {
        return NULL;
    }

    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, size_t bdname_size,
                              uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir)
    {
        return false;
    }

    /* get complete or short local name from eir data */
    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname)
    {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname)
    {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN)
        {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname != NULL && bdname_size > 0)
        {
            size_t copied_len = rmt_bdname_len;
            if (copied_len >= bdname_size)
            {
                copied_len = bdname_size - 1;
            }
            memcpy(bdname, rmt_bdname, copied_len);
            bdname[copied_len] = '\0';
        }
        if (bdname_len)
        {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

// this function is executed by the task: "BTC_TASK"
static bool discovery_result_matches_target(const discovery_result_t *result)
{
    /* search for device with MAJOR service class as "rendering" in COD */
    if (!esp_bt_gap_is_valid_cod(result->class_of_device) ||
        !(esp_bt_gap_get_cod_srvc(result->class_of_device) & ESP_BT_COD_SRVC_RENDERING))
    {
        return false;
    }

    return strcmp(result->name, remote_device_name) == 0;
}

static void parse_discovery_result(const esp_bt_gap_cb_param_t *param, discovery_result_t *result)
{
    memset(result, 0, sizeof(*result));
    memcpy(result->address, param->disc_res.bda, sizeof(result->address));
    result->rssi = -127;

    uint8_t *eir = NULL;
    for (int i = 0; i < param->disc_res.num_prop; ++i)
    {
        esp_bt_gap_dev_prop_t *property = param->disc_res.prop + i;
        switch (property->type)
        {
        case ESP_BT_GAP_DEV_PROP_COD:
            if (property->len >= sizeof(result->class_of_device))
            {
                memcpy(&result->class_of_device, property->val, sizeof(result->class_of_device));
            }
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            if (property->len >= sizeof(result->rssi))
            {
                memcpy(&result->rssi, property->val, sizeof(result->rssi));
            }
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
            if (property->val != NULL && property->len > 0)
            {
                size_t name_len = property->len;
                if (name_len >= sizeof(result->name))
                {
                    name_len = sizeof(result->name) - 1;
                }
                memcpy(result->name, property->val, name_len);
                result->name[name_len] = '\0';
            }
            break;
        case ESP_BT_GAP_DEV_PROP_EIR:
            eir = property->val;
            break;
        default:
            break;
        }
    }

    if (result->name[0] == '\0' && eir != NULL)
    {
        get_name_from_eir(eir, (uint8_t *)result->name, sizeof(result->name), NULL);
    }
}

static bool update_discovered_device_list(const discovery_result_t *result, size_t *updated_index)
{
    bool changed = false;
    size_t index = discovered_device_count;

    xSemaphoreTake(discovered_devices_mutex, portMAX_DELAY);
    for (size_t i = 0; i < discovered_device_count; ++i)
    {
        if (memcmp(discovered_devices[i].address, result->address, sizeof(result->address)) == 0)
        {
            index = i;
            break;
        }
    }

    if (index == discovered_device_count)
    {
        if (discovered_device_count == BT_APP_GAP_MAX_DISCOVERED_DEVICES)
        {
            xSemaphoreGive(discovered_devices_mutex);
            ESP_LOGW(GAP_TAG, "Device list full; ignoring another discovery result");
            return false;
        }
        ++discovered_device_count;
        changed = true;
    }

    bt_app_gap_discovered_device_t *device = &discovered_devices[index];
    if (memcmp(device->address, result->address, sizeof(device->address)) != 0 ||
        strcmp(device->name, result->name) != 0 ||
        device->rssi != result->rssi ||
        device->class_of_device != result->class_of_device)
    {
        memcpy(device->address, result->address, sizeof(device->address));
        memcpy(device->name, result->name, sizeof(device->name));
        device->rssi = result->rssi;
        device->class_of_device = result->class_of_device;
        changed = true;
    }
    xSemaphoreGive(discovered_devices_mutex);

    if (updated_index != NULL)
    {
        *updated_index = index;
    }
    return changed;
}

static void clear_discovered_device_list(void)
{
    xSemaphoreTake(discovered_devices_mutex, portMAX_DELAY);
    memset(discovered_devices, 0, sizeof(discovered_devices));
    discovered_device_count = 0;
    xSemaphoreGive(discovered_devices_mutex);
}

static void notify_device_list_changed(void)
{
    bt_app_gap_device_list_changed_cb_t callback = NULL;

    xSemaphoreTake(discovered_devices_mutex, portMAX_DELAY);
    callback = device_list_changed_cb;
    xSemaphoreGive(discovered_devices_mutex);

    if (callback != NULL)
    {
        callback();
    }
}

// this function is executed by the task: "BTC_TASK"
static void gap_cb_1(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event)
    {
    /* when device discovered a result, this event comes */
    case ESP_BT_GAP_DISC_RES_EVT:
    {
        discovery_result_t result;
        parse_discovery_result(param, &result);

        // The Bluetooth stack owns param->disc_res.prop. Convert it to an
        // independent value before dispatching work to the application task.
        if (!bt_app_work_dispatch(gap_cb_2, BT_APP_GAP_DISCOVERY_RESULT_EVT,
                                  &result, sizeof(result), NULL, NULL))
        {
            ESP_LOGE(GAP_TAG, "dispatch failed.");
        }
        break;
    }
    default:
    {

        if (!bt_app_work_dispatch(gap_cb_2, event, param, sizeof(esp_bt_gap_cb_param_t), NULL, NULL))
        {
            ESP_LOGE(GAP_TAG, "dispatch failed.");
        }
        break;
    }
    }
}

// this function is executed by the task: "bt_app"
static void gap_cb_2(uint16_t event, void *p_param)
{
    esp_bt_gap_cb_param_t *param = (esp_bt_gap_cb_param_t *)p_param;

    switch (event)
    {
    /* when device discovered a result, this event comes */
    case BT_APP_GAP_DISCOVERY_RESULT_EVT:
    {
        const discovery_result_t *result = (const discovery_result_t *)p_param;
        if (!discovery_running)
        {
            break;
        }

        size_t updated_index = 0;
        if (update_discovered_device_list(result, &updated_index))
        {
            char address[18];
            ESP_LOGI(GAP_TAG,
                     "Device list updated (%u/%u): %s, name=\"%s\", RSSI=%d dBm, CoD=0x%08" PRIx32,
                     (unsigned)(updated_index + 1), (unsigned)bt_app_gap_get_discovered_device_count(),
                     bda2str(result->address, address, sizeof(address)),
                     result->name[0] != '\0' ? result->name : "<unknown>",
                     result->rssi, result->class_of_device);
            notify_device_list_changed();
        }

        if (!target_found && discovery_result_matches_target(result))
        {
            target_found = true;
            memcpy(verified_address, result->address, sizeof(verified_address));

            ESP_LOGI(GAP_TAG, "Configured source found; stopping discovery.");
            esp_err_t err = esp_bt_gap_cancel_discovery();
            if (err != ESP_OK)
            {
                ESP_LOGE(GAP_TAG, "Cancel discovery failed: %s", esp_err_to_name(err));
            }
        }
        break;
    }
    /* when discovery state changed, this event comes */
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
    {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
        {
            if (target_found)
            {
                bt_app_gap_device_found_cb_t callback = device_found_cb;

                device_found_cb = NULL;
                discovery_running = false;

                if (callback != NULL)
                {
                    callback(verified_address);
                }
            }
            else
            {
                esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
                if (err != ESP_OK)
                {
                    ESP_LOGE(GAP_TAG, "esp_bt_gap_start_discovery failed with code %x", err);
                    discovery_running = false;
                    device_found_cb = NULL;
                }
            }
        }
        else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED)
        {
            target_found = false;
            ESP_LOGI(GAP_TAG, "Discovery started.");
        }
        break;
    }
    case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
    {
        if (param->config_eir_data.stat != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGW(GAP_TAG, "EIR configuration status: %d", param->config_eir_data.stat);
        }
        else
        {
            ESP_LOGD(GAP_TAG, "EIR configuration complete");
        }
        break;
    }
    /* when authentication completed, this event comes */
    case ESP_BT_GAP_AUTH_CMPL_EVT:
    {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(GAP_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            ESP_LOG_BUFFER_HEX(GAP_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        }
        else
        {
            ESP_LOGE(GAP_TAG, "authentication failed, status: %d", param->auth_cmpl.stat);
        }
        break;
    }
    /* when Legacy Pairing pin code requested, this event comes */
    case ESP_BT_GAP_PIN_REQ_EVT:
    {
        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit: %d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit)
        {
            ESP_LOGI(GAP_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        }
        else
        {
            ESP_LOGI(GAP_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }
    /* when Security Simple Pairing user confirmation requested, this event comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
    {
        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %06" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    }
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
    {
        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %06" PRIu32, param->key_notif.passkey);
        break;
    }
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
    {
        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
    }
    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
    {
        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
        break;
    }
    case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
    {
        if (param->get_dev_name_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(GAP_TAG, "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT device name: %s", param->get_dev_name_cmpl.name);
        }
        else
        {
            ESP_LOGI(GAP_TAG, "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT failed, state: %d", param->get_dev_name_cmpl.status);
        }
        break;
    }
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
    {
        if (remote_name_cb == NULL ||
            memcmp(remote_name_address, param->read_rmt_name.bda, sizeof(remote_name_address)) != 0)
        {
            break;
        }

        bt_app_gap_remote_name_cb_t callback = remote_name_cb;
        remote_name_cb = NULL;

        if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS &&
            param->read_rmt_name.rmt_name[0] != '\0')
        {
            char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
            memcpy(name, param->read_rmt_name.rmt_name, ESP_BT_GAP_MAX_BDNAME_LEN);
            name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
            callback(param->read_rmt_name.bda, name);
        }
        else
        {
            ESP_LOGW(GAP_TAG, "Could not read remote name, status: %d", param->read_rmt_name.stat);
            callback(param->read_rmt_name.bda, NULL);
        }
        break;
    }
    case ESP_BT_GAP_ENC_CHG_EVT:
    {
        char *str_enc[3] = {"OFF", "E0", "AES"};
        uint8_t *bda = (uint8_t *)param->enc_chg.bda;
        ESP_LOGI(GAP_TAG, "Encryption mode to [%02x:%02x:%02x:%02x:%02x:%02x] changed to %s",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], str_enc[param->enc_chg.enc_mode]);
        break;
    }
    /* when ACL connection completed, this event comes */
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
    {
        uint8_t *bda = (uint8_t *)param->acl_conn_cmpl_stat.bda;
        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT Connected to [%02x:%02x:%02x:%02x:%02x:%02x], status: 0x%x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], param->acl_conn_cmpl_stat.stat);
        break;
    }
    /* when ACL disconnection completed, this event comes */
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
    {
        uint8_t *bda = (uint8_t *)param->acl_disconn_cmpl_stat.bda;
        ESP_LOGI(GAP_TAG, "ESP_BT_GAP_ACL_DISC_CMPL_STAT_EVT Disconnected from [%02x:%02x:%02x:%02x:%02x:%02x], reason: 0x%x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], param->acl_disconn_cmpl_stat.reason);
        break;
    }
    default:
    {
        ESP_LOGW(GAP_TAG, "event: %d not handled", event);
        break;
    }
    }
    return;
}

// this function is executed by the task: "bt_app"
static void register_gap_callback_function(uint16_t event, void *p_param)
{
    // both parameters : event and p_param are ignored.

    // Set the local device name
    esp_err_t err = esp_bt_gap_set_device_name(LOCAL_DEVICE_NAME);
    if (err != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "esp_bt_gap_set_device_name failed with code %x", err);
    }

    // Register the GAP (Generic Access Profile) callback function (handles authentication, encryption, etc.)
    err = esp_bt_gap_register_callback(gap_cb_1);
    if (err != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "esp_bt_gap_register_callback failed with code %x", err);
    }
    else
    {
        gap_callback_registered = true;
    }
}

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void bt_app_gap_start(void)
{
    if (gap_start_requested)
    {
        return;
    }
    gap_start_requested = true;

    discovered_devices_mutex = xSemaphoreCreateMutex();
    if (discovered_devices_mutex == NULL)
    {
        ESP_LOGE(GAP_TAG, "Failed to create discovery-list mutex");
        abort();
    }

    if (!bt_app_work_dispatch(register_gap_callback_function,
                              0, NULL, 0, NULL, NULL))
    {
        ESP_LOGE(GAP_TAG, "Failed to dispatch GAP callback registration");
        bt_app_task_shut_down();
        abort();
    }
}

esp_err_t bt_app_gap_start_discovery(bt_app_gap_device_found_cb_t cb_ptr)
{
    if (!discovery_running && cb_ptr != NULL)
    {
        device_found_cb = cb_ptr;
        discovery_running = true;
        clear_discovered_device_list();
        notify_device_list_changed();
        esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(GAP_TAG, "esp_bt_gap_start_discovery failed with code %x", err);
            discovery_running = false;
            device_found_cb = NULL;
        }
        return err;
    }
    return ESP_ERR_INVALID_STATE;
}

size_t bt_app_gap_get_discovered_device_count(void)
{
    if (discovered_devices_mutex == NULL)
    {
        return 0;
    }

    xSemaphoreTake(discovered_devices_mutex, portMAX_DELAY);
    size_t count = discovered_device_count;
    xSemaphoreGive(discovered_devices_mutex);
    return count;
}

esp_err_t bt_app_gap_get_discovered_device(size_t index,
                                           bt_app_gap_discovered_device_t *device)
{
    if (device == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (discovered_devices_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(discovered_devices_mutex, portMAX_DELAY);
    if (index >= discovered_device_count)
    {
        xSemaphoreGive(discovered_devices_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *device = discovered_devices[index];
    xSemaphoreGive(discovered_devices_mutex);
    return ESP_OK;
}

void bt_app_gap_set_device_list_changed_callback(bt_app_gap_device_list_changed_cb_t callback)
{
    if (discovered_devices_mutex == NULL)
    {
        device_list_changed_cb = callback;
        return;
    }

    xSemaphoreTake(discovered_devices_mutex, portMAX_DELAY);
    device_list_changed_cb = callback;
    xSemaphoreGive(discovered_devices_mutex);
}

esp_err_t bt_app_gap_read_remote_name(const uint8_t *address,
                                      bt_app_gap_remote_name_cb_t callback)
{
    if (!gap_callback_registered || address == NULL || callback == NULL || remote_name_cb != NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(remote_name_address, address, sizeof(remote_name_address));
    remote_name_cb = callback;
    esp_err_t err = esp_bt_gap_read_remote_name(remote_name_address);
    if (err != ESP_OK)
    {
        remote_name_cb = NULL;
    }
    return err;
}
