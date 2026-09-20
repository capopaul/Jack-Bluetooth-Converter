
#include "esp_log.h"
#include "esp_gap_bt_api.h"

#include "bt_app_gap.h"
#include "bt_app_a2dp_source.h"
#include "bt_app_core.h"

#define GAP_TAG "BT_APP_GAP"

/* device name */
#define LOCAL_DEVICE_NAME "Jack2Bluetooth"

static const char remote_device_name[] = "Bose-Paul-2";

static esp_bd_addr_t discovered_address = {0};               /* Bluetooth Device Address of peer device*/
static uint8_t s_peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1]; /* Bluetooth Device Name of peer device*/

/* utils for transfer BLuetooth Deveice Address into string form */
static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18)
    {
        return NULL;
    }

    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
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

        if (bdname)
        {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len)
        {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static void filter_inquiry_scan_result(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    uint32_t cod = 0;    /* class of device */
    int32_t rssi = -129; /* invalid value */
    uint8_t *eir = NULL;
    esp_bt_gap_dev_prop_t *p;

    /* handle the discovery results */
    ESP_LOGI(GAP_TAG, "Scanned device: %s", bda2str(param->disc_res.bda, bda_str, 18));
    for (int i = 0; i < param->disc_res.num_prop; i++)
    {
        p = param->disc_res.prop + i;
        switch (p->type)
        {
        case ESP_BT_GAP_DEV_PROP_COD:
            cod = *(uint32_t *)(p->val);
            ESP_LOGI(GAP_TAG, "--Class of Device: 0x%" PRIx32, cod);
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t *)(p->val);
            ESP_LOGI(GAP_TAG, "--RSSI: %" PRId32, rssi);
            break;
        case ESP_BT_GAP_DEV_PROP_EIR:
            eir = (uint8_t *)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
        default:
            break;
        }
    }

    /* search for device with MAJOR service class as "rendering" in COD */
    if (!esp_bt_gap_is_valid_cod(cod) ||
        !(esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING))
    {
        return;
    }

    /* search for target device in its Extended Inqury Response */
    if (eir)
    {
        get_name_from_eir(eir, s_peer_bdname, NULL);
        if (strcmp((char *)s_peer_bdname, remote_device_name) == 0)
        {
            ESP_LOGI(GAP_TAG, "Found a target device, address %s, name %s", bda_str, s_peer_bdname);
            bt_app_a2dp_source_set_state(APP_AV_STATE_DISCOVERED);
            memcpy(discovered_address, param->disc_res.bda, sizeof(discovered_address));
            ESP_LOGI(GAP_TAG, "Cancel device discovery ...");
            esp_bt_gap_cancel_discovery();
        }
    }
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event)
    {
    /* when device discovered a result, this event comes */
    case ESP_BT_GAP_DISC_RES_EVT:
    {
        if (bt_app_a2dp_source_get_state() == APP_AV_STATE_DISCOVERING)
        {
            filter_inquiry_scan_result(param);
        }
        break;
    }
    /* when discovery state changed, this event comes */
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
    {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
        {
            if (bt_app_a2dp_source_get_state() == APP_AV_STATE_DISCOVERED)
            {
                bt_app_a2dp_source_set_state(APP_AV_STATE_CONNECTING);
                ESP_LOGI(GAP_TAG, "Device discovery stopped.");
                ESP_LOGI(GAP_TAG, "a2dp connecting to peer: %s", s_peer_bdname);
                /* connect source to peer device specified by Bluetooth Device Address */

                bt_app_a2dp_source_connect(discovered_address);
            }
            else
            {
                /* not discovered, continue to discover */
                ESP_LOGI(GAP_TAG, "Device discovery failed, continue to discover...");
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
            }
        }
        else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED)
        {
            ESP_LOGI(GAP_TAG, "Discovery started.");
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
        ESP_LOGI(GAP_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

/* GAP callback function */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

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
    err = esp_bt_gap_register_callback(bt_app_gap_cb);
    if (err != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "esp_bt_gap_register_callback failed with code %x", err);
    }
}

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

esp_err_t bt_app_gap_start(void)
{
    if (!bt_app_work_dispatch(register_gap_callback_function, 0, NULL, 0, NULL, NULL))
    {
        ESP_LOGE(GAP_TAG, "failed to dispatch Bluetooth stack initialization");
        bt_app_task_shut_down();
        return ESP_FAIL;
    }

    return ESP_OK;
}
